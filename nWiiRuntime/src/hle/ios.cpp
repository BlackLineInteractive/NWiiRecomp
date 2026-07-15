#include "runtime/platform/platform.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include "runtime/hw/hw.h"
#include "runtime/event_scheduler.h"
#include "runtime/os_thread.h"
#include "runtime/virtual_disc.h"

using namespace nwii::runtime;

namespace nwii::runtime {
    std::string read_guest_string(CPUContext &ctx, uint32_t addr, int max_len = 256);
    bool valid_callback(uint32_t cb);
}


#include "runtime/ios_device.h"
#include "runtime/ios_kernel.h"
#include "runtime/devices.h"
#include <iostream>
#include <vector>

extern "C" void run_game(nwii::runtime::CPUContext &ctx);

static uint32_t phys_to_virt(uint32_t addr) {
  if (addr == 0)
    return 0;
  if (addr >= 0x80000000 && addr < 0x94000000)
    return addr;
  if (addr >= 0x00800000 && addr < 0x04000000)
    return addr | 0x90000000;
  if (addr < 0x00800000)
    return addr | 0x80000000;
  if (addr >= 0x10000000 && addr < 0x14000000)
    return addr | 0x90000000;
  return addr;
}

// Helper to read string from guest memory
std::string nwii::runtime::read_guest_string(CPUContext &ctx, uint32_t addr,
                                     int max_len) {
  std::string path;
  if (addr == 0) {
    std::cout << "[HLE IOS] read_guest_string: null address! pc=0x" << std::hex
              << ctx.pc << " lr=0x" << ctx.lr << std::dec << std::endl;
    return path;
  }
  uint32_t vaddr = phys_to_virt(addr);
  if (vaddr >= 0x80000000 && vaddr < 0x94000000) {
    for (int i = 0; i < max_len; ++i) {
      char c = ctx.mmu.read8(vaddr + i);
      if (c == '\0')
        break;
      if (c >= 32 && c <= 126)
        path += c;
    }
    if (path.empty()) {
      std::cout << "[HLE IOS] read_guest_string: string at 0x" << std::hex
                << addr << " (vaddr 0x" << vaddr
                << ") is empty (first char is 0x" << (int)ctx.mmu.read8(vaddr)
                << ") pc=0x" << ctx.pc << " lr=0x" << ctx.lr << std::dec
                << std::endl;
    }
  } else {
    std::cout << "[HLE IOS] read_guest_string: invalid translated address 0x"
              << std::hex << vaddr << " (orig: 0x" << addr << ") pc=0x"
              << ctx.pc << " lr=0x" << ctx.lr << std::dec << std::endl;
  }
  return path;
}

static constexpr int32_t IPC_EIO = -8;

// All HLE-side IOS allocations live in the IOS-reserved top of MEM2
// (0x93E00000-0x94000000 on retail Wii), never inside game sections.
static constexpr uint32_t IPC_POOL_BASE = 0x93EE0000;
static constexpr uint32_t IPC_SLOT_SIZE = 128; // bytes per slot
static constexpr uint32_t IPC_POOL_SLOTS = 64;
static uint32_t ipc_pool_next = 0; // round-robin index

// Bump allocator for guest IPC/heap memory
// Two separate regions so IPC request structs can't be overwritten by
// larger NAND/FS buffers:
//   ios_guest_heap_ptr  -> misc/NAND/FS allocs (heap=1,2)
//   ios_ipc_heap_ptr    -> IPC request buffers (heap=0, size<=128)
static uint32_t ios_guest_heap_ptr = 0x93E40000;
static uint32_t ios_ipc_heap_ptr = 0x93E00000;

static uint32_t ios_guest_alloc(CPUContext &ctx, uint32_t size, uint32_t align,
                                int heap_id = 0) {
  if (align < 32)
    align = 32;
  // Route small IPC request buffers (heap=0, size=64) to isolated region
  uint32_t &heap =
      (heap_id == 0 && size <= 128) ? ios_ipc_heap_ptr : ios_guest_heap_ptr;
  heap = (heap + align - 1) & ~(align - 1);
  uint32_t ptr = heap;
  heap += (size + align - 1) & ~(align - 1);
  for (uint32_t i = 0; i < size; ++i)
    ctx.mmu.write8(ptr + i, 0);
  return ptr;
}

static uint32_t ipc_pool_alloc(CPUContext &ctx) {
  uint32_t slot = ipc_pool_next;
  ipc_pool_next = (ipc_pool_next + 1) % IPC_POOL_SLOTS;
  uint32_t ptr = IPC_POOL_BASE + slot * IPC_SLOT_SIZE;
  // Zero out the slot
  for (uint32_t i = 0; i < IPC_SLOT_SIZE; ++i)
    ctx.mmu.write8(ptr + i, 0);
  return ptr;
}



bool nwii::runtime::valid_callback(uint32_t cb) {
  return cb != 0 && cb != 0xFFFFFFFFu && cb >= 0x80000000u && cb < 0x82000000u;
}

// Disc reader
static std::shared_ptr<std::ifstream> g_disc_file;
static bool g_disc_is_wbfs = false;
static uint32_t g_wbfs_block_size = 0;
static std::vector<uint16_t> g_wbfs_bat;

static bool init_wbfs(const std::filesystem::path &path) {
  auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
  if (!file->is_open())
    return false;

  char magic[4];
  file->read(magic, 4);
  if (magic[0] != 'W' || magic[1] != 'B' || magic[2] != 'F' ||
      magic[3] != 'S') {
    return false;
  }

  uint8_t shift;
  file->read(reinterpret_cast<char *>(&shift), 1);
  g_wbfs_block_size = 1u << (shift + 17);

  file->seekg(0, std::ios::end);
  size_t file_size = file->tellg();
  file->seekg(0x100, std::ios::beg);

  size_t num_entries = (file_size - 0x100) / 2;
  if (num_entries > 65536)
    num_entries = 65536;

  g_wbfs_bat.resize(num_entries);
  for (size_t i = 0; i < num_entries; ++i) {
    uint16_t entry;
    file->read(reinterpret_cast<char *>(&entry), 2);
    g_wbfs_bat[i] = ((entry >> 8) & 0xFF) | ((entry << 8) & 0xFF00);
  }

  g_disc_file = file;
  g_disc_is_wbfs = true;
  std::cout << "[HLE IOS] Opened WBFS: " << path
            << " block_size=" << g_wbfs_block_size << std::endl;
  return true;
}

static void init_iso(const std::filesystem::path &path) {
  g_disc_file = std::make_shared<std::ifstream>(path, std::ios::binary);
  if (g_disc_file->is_open()) {
    g_disc_is_wbfs = false;
    std::cout << "[HLE IOS] Opened ISO: " << path << std::endl;
  }
}

static std::shared_ptr<std::ifstream> get_disc_file() {
  if (!g_disc_file) {
    auto &cfg = Config::get();
    std::filesystem::path game_dir(cfg.game_dir);
    std::filesystem::path p;

    p = game_dir / "disc.iso";
    if (std::filesystem::exists(p)) {
      init_iso(p);
      return g_disc_file;
    }
    p = game_dir / "game.iso";
    if (std::filesystem::exists(p)) {
      init_iso(p);
      return g_disc_file;
    }
    p = game_dir.parent_path() / "game.iso";
    if (std::filesystem::exists(p)) {
      init_iso(p);
      return g_disc_file;
    }
    p = game_dir.parent_path() / "disc.iso";
    if (std::filesystem::exists(p)) {
      init_iso(p);
      return g_disc_file;
    }

    if (std::filesystem::exists(game_dir)) {
      for (const auto &entry : std::filesystem::directory_iterator(game_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".wbfs") {
          if (init_wbfs(entry.path()))
            return g_disc_file;
        }
      }
    }
    if (std::filesystem::exists(game_dir.parent_path())) {
      for (const auto &entry :
           std::filesystem::directory_iterator(game_dir.parent_path())) {
        if (entry.is_regular_file() && entry.path().extension() == ".wbfs") {
          if (init_wbfs(entry.path()))
            return g_disc_file;
        }
      }
    }
  }
  return g_disc_file;
}

static void read_disc_data(uint8_t *dst, uint64_t offset, uint32_t length) {
  // Extracted game directory takes precedence over iso/wbfs images
  auto &vd = VirtualDisc::get();
  if (!vd.valid())
    vd.init(Config::get().game_dir);
  if (vd.valid() && vd.read(offset, length, dst))
    return;

  auto file = get_disc_file();
  if (!file || !file->is_open()) {
    std::memset(dst, 0, length);
    return;
  }

  if (!g_disc_is_wbfs) {
    file->seekg(offset);
    file->read(reinterpret_cast<char *>(dst), length);
  } else {
    uint32_t done = 0;
    while (done < length) {
      uint64_t block_idx = (offset + done) / g_wbfs_block_size;
      uint32_t block_off = (offset + done) % g_wbfs_block_size;
      uint32_t chunk =
          std::min<uint32_t>(length - done, g_wbfs_block_size - block_off);

      if (block_idx < g_wbfs_bat.size() && g_wbfs_bat[block_idx] != 0) {
        uint64_t phys =
            (uint64_t)g_wbfs_bat[block_idx] * g_wbfs_block_size + block_off;
        file->seekg(phys);
        file->read(reinterpret_cast<char *>(dst + done), chunk);
      } else {
        std::memset(dst + done, 0, chunk);
      }
      done += chunk;
    }
  }
}

namespace nwii {
namespace runtime {

struct PendingCallback {
  uint32_t callback;
  uint32_t arg1;
  uint32_t arg2;
};

static std::vector<PendingCallback> g_pending_callbacks;

// Isolated callback execution - saves ALL non-volatile state including FPR/GQR
// (#9)

} // namespace runtime
} // namespace nwii

// NEW: Proper DI_Read that handles both sync and async modes
static void di_read_internal(nwii::runtime::MMU *mmu, uint32_t inbuf, uint32_t outbuf,
                             uint32_t callback, uint32_t userdata,
                             bool is_async, uint32_t out_size = 0) {
  // Read DICommand structure
  uint32_t cmd = 0, transferSize = 0, addr = 0, length = 0;
  uint64_t offset = 0;

  uint32_t ipc_ptr = phys_to_virt(inbuf);

  if (ipc_ptr >= 0x80000000 && ipc_ptr < 0x94000000) {
    uint32_t raw_cmd = mmu->read32(ipc_ptr + 0x00);
    cmd = raw_cmd >> 24; // Command is the first byte
    if (cmd == 1) {
      cmd = 0x12; // Handle as DVDLowInquiry
    } else if (cmd == 0) {
      // Some homebrew passes the whole 32-bit word, fallback if needed
      if (raw_cmd == 0x20)
        cmd = 0x20;
    }

    transferSize = mmu->read32(ipc_ptr + 0x04);
    addr = mmu->read32(ipc_ptr + 0x08);

    uint32_t val10 = mmu->read32(ipc_ptr + 0x10);
    uint32_t val14 = mmu->read32(ipc_ptr + 0x14);

    if (nwii::runtime::Config::get().platform ==
        nwii::runtime::Platform::GameCube) {
      offset = ((uint64_t)val14) << 2; // GC: 32-bit word offset
    } else {
      // Wii: 48-bit word offset
      offset = (((uint64_t)(val10 & 0xFFFF) << 32) | val14) << 2;
    }

    length = mmu->read32(ipc_ptr + 0x18);
  }
  
  if (addr == 0 && outbuf != 0) {
      addr = outbuf;
  }

  std::cout << "[HLE IOS] DI_Read" << (is_async ? "Async" : "") << ": inbuf=0x"
            << std::hex << inbuf << " cmd=0x" << cmd
            << " transferSize=" << std::dec << transferSize << " addr=0x"
            << std::hex << addr << " cb=0x" << callback << " offset=0x"
            << offset << " length=" << std::dec << length << " outbuf=0x" << std::hex << outbuf << std::endl;

  bool use_defaults =
      (cmd != 0x20 && cmd != 0x12 && cmd != 0x95 && cmd != 0x71 && cmd != 0x72 && cmd != 0x96 && cmd != 0xe3) || (offset > 0x1FFFFFFFFFFULL);

  if (use_defaults) {
    std::cout << "[HLE IOS] DI_Read: Using defaults (invalid structure)"
              << std::endl;
    cmd = 0x20;
    transferSize = 2;
    offset = 0;
    length = 0x440;
  }

  uint32_t buf_ptr = phys_to_virt(addr);
  uint32_t total_length = transferSize * 2048;
  if (length > 0 && length < total_length)
    total_length = length;
  if (total_length == 0 && length > 0)
    total_length = length;
  if (total_length == 0)
    total_length = 0x440;
  // Never write past the caller's output buffer
  if (out_size > 0 && total_length > out_size)
    total_length = out_size;

  std::cout << "[HLE IOS] DI_Read final: offset=0x" << std::hex
            << (uint32_t)offset << " total_length=" << std::dec << total_length
            << " buf_ptr=0x" << std::hex << buf_ptr << std::dec << std::endl;

  auto file = get_disc_file();
  bool success = false;
  uint32_t bytes_read = 0;
  bool have_media = (file && file->is_open()) || VirtualDisc::get().valid();

  if (have_media && buf_ptr != 0 && total_length > 0) {
    std::vector<uint8_t> temp(total_length);
    read_disc_data(temp.data(), (uint32_t)offset, total_length);
    bytes_read = total_length;
    for (uint32_t i = 0; i < bytes_read; i++)
      mmu->write8(buf_ptr + i, temp[i]);
    std::cout << "[DI] Read " << bytes_read << " bytes" << std::endl;
    success = true;
  } else if (buf_ptr != 0) {
    if (offset == 0) {
      const std::string& gid = Config::get().game_id;
      for (uint32_t i = 0; i < total_length; i++) mmu->write8(buf_ptr + i, 0);
      for (int i = 0; i < (int)gid.size() && i < 4; i++)
        mmu->write8(buf_ptr + i, (uint8_t)gid[i]);
      mmu->write32(buf_ptr + 0x18, 0x5D1C9EA3);
      std::cout << "[DI] Faked disc header (" << gid << ")" << std::endl;
      success = true;
      bytes_read = total_length;
    } else {
      // No disc file, but reading non-zero offset (e.g. 0x95 reading TMD/Ticket)
      std::cout << "[DI] No disc file, zeroing out read buffer of size " << total_length << std::endl;
      for (uint32_t i = 0; i < total_length; i++) mmu->write8(buf_ptr + i, 0);
      success = true;
      bytes_read = total_length;
    }
  }
}

// Public alias used by di_handle_ioctl_shared (declared above)
void di_read_internal_shared(nwii::runtime::MMU* mmu, uint32_t inbuf, uint32_t outbuf,
                              uint32_t callback, uint32_t userdata, bool is_async,
                              uint32_t out_size) {
    di_read_internal(mmu, inbuf, outbuf, callback, userdata, is_async, out_size);
}

extern "C" {
void IOS_Open(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_open(ctx);
}

void IOS_OpenAsync(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_open_async(ctx);
}

void IOS_Close(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_close(ctx);
}

void IOS_Read(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_read(ctx);
}

void IOS_Write(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_write(ctx);
}

void IOS_Ioctl(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_ioctl(ctx);
}

void IOS_Ioctlv(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_ioctlv(ctx);
}

void iosAlloc(CPUContext &ctx) {
  uint32_t heap_id = ctx.gpr[3];
  uint32_t size = ctx.gpr[4];
  uint32_t align = ctx.gpr[5];
  uint32_t ptr = ios_guest_alloc(ctx, size, align, heap_id);
  std::cout << "[HLE IOS] iosAlloc: heap=" << heap_id << " size=" << size
            << " align=" << align << " -> ptr=0x" << std::hex << ptr << " LR=0x" << ctx.lr << std::dec
            << std::endl;
  ctx.gpr[3] = ptr;
  ctx.pc = ctx.lr;
}

// iosFree: frees a block from the IOS heap. Hook this at the real iosFree
// address.
void iosFree(CPUContext &ctx) {
  uint32_t heap_id = ctx.gpr[3];
  uint32_t ptr = ctx.gpr[4];
  std::cout << "[HLE IOS] iosFree: heap=" << heap_id << " ptr=0x" << std::hex
            << ptr << " LR=0x" << ctx.lr << std::dec << std::endl;
  ctx.gpr[3] = 0; // IPC_OK
  ctx.pc = ctx.lr;
}

void IOS_IoctlAsync(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_ioctl_async(ctx);
}

void IOS_IoctlvAsync(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_ioctlv_async(ctx);
}

void IOS_Seek(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
    return;
  }
  int32_t fd = (int32_t)ctx.gpr[3];
  int32_t offset = (int32_t)ctx.gpr[4];
  int32_t whence = (int32_t)ctx.gpr[5];

  int32_t result = IOSKernel::get().seek(ctx, fd, offset, whence);
  ctx.gpr[3] = result;
  ctx.pc = ctx.lr;
}

void IOS_CloseAsync(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_close_async(ctx);
}

void IOS_ReadAsync(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_read_async(ctx);
}

void IOS_WriteAsync(CPUContext &ctx) {
    nwii::runtime::platform::IPlatform::get().ios_write_async(ctx);
}

void IOS_SeekAsync(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
    return;
  }
  int32_t fd = (int32_t)ctx.gpr[3];
  int32_t offset = (int32_t)ctx.gpr[4];
  int32_t whence = (int32_t)ctx.gpr[5];
  uint32_t callback = ctx.gpr[6];
  uint32_t userdata = ctx.gpr[7];

  int32_t result = IOSKernel::get().seek(ctx, fd, offset, whence);

  if (valid_callback(callback)) {
    ctx.queue_callback(callback, result, userdata);
  }

  ctx.gpr[3] = IPC_OK;
  ctx.pc = ctx.lr;
}

} // extern "C"

namespace nwii {
namespace runtime {
extern MMU *g_mmu;
}
} // namespace nwii

extern "C" int32_t handle_ios_ipc(CPUContext& ctx, uint32_t request_addr) {
  if (!nwii::runtime::g_mmu)
    return 0;

  // request_addr is now a virtual address (translated by ipc_fake_ack).
  // If called directly from the old path it could still be physical, so
  // normalise: MEM1 phys 0x00-0x017FFFFF -> 0x80+, MEM2 phys 0x10-0x13FFFFFF ->
  // 0x90+
  uint32_t virt_addr = request_addr;
  if (virt_addr < 0x01800000) {
    virt_addr |= 0x80000000;
  } else if (virt_addr >= 0x10000000 && virt_addr < 0x14000000) {
    virt_addr = (virt_addr & 0x03FFFFFF) | 0x90000000;
  }

  // IOS IPC request layout (Dolphin IOS::HLE):
  //   +0x00 cmd, +0x04 result, +0x08 fd, +0x0C.. args
  uint32_t cmd = nwii::runtime::g_mmu->read32(virt_addr);
  int32_t result = IPC_OK;

  if (cmd == 1) { // IOS_Open via HW IPC
    // args[0] = path pointer, args[1] = open mode
    uint32_t path_addr = nwii::runtime::g_mmu->read32(virt_addr + 0x0C);
    uint32_t mode = nwii::runtime::g_mmu->read32(virt_addr + 0x10);
    // path_addr may be virtual or physical; normalise
    if (path_addr < 0x01800000)
      path_addr |= 0x80000000;
    else if (path_addr >= 0x10000000 && path_addr < 0x14000000)
      path_addr = (path_addr & 0x03FFFFFF) | 0x90000000;

    std::string path;
    for (int i = 0; i < 64; ++i) {
      char c = (char)ctx.mmu.read8(path_addr + i);
      if (c == '\0') break;
      path += c;
    }
    result = IOSKernel::get().open(ctx, path, mode);
    std::cout << "[HW IPC] Open path='" << path << "' -> fd=" << result << "\n";
  } else if (cmd == 2) { // IOS_Close
    uint32_t fd = ctx.mmu.read32(virt_addr + 0x08);
    result = IOSKernel::get().close(ctx, fd);
  } else if (cmd == 3) { // IOS_Read: args[0] = buffer, args[1] = length
    uint32_t fd = ctx.mmu.read32(virt_addr + 0x08);
    uint32_t buf = ctx.mmu.read32(virt_addr + 0x0C);
    uint32_t len = ctx.mmu.read32(virt_addr + 0x10);
    result = IOSKernel::get().read(ctx, fd, buf, len);
  } else if (cmd == 4) { // IOS_Write: args[0] = buffer, args[1] = length
    uint32_t fd = ctx.mmu.read32(virt_addr + 0x08);
    uint32_t buf = ctx.mmu.read32(virt_addr + 0x0C);
    uint32_t len = ctx.mmu.read32(virt_addr + 0x10);
    result = IOSKernel::get().write(ctx, fd, buf, len);
  } else if (cmd == 5) { // IOS_Seek: args[0] = offset, args[1] = whence
    uint32_t fd = ctx.mmu.read32(virt_addr + 0x08);
    int32_t offset = (int32_t)ctx.mmu.read32(virt_addr + 0x0C);
    int32_t whence = (int32_t)ctx.mmu.read32(virt_addr + 0x10);
    result = IOSKernel::get().seek(ctx, fd, offset, whence);
  } else if (cmd == 6) { // IOS_Ioctl (async via HW IPC)
    uint32_t fd = ctx.mmu.read32(virt_addr + 0x08);
    uint32_t ioctl_cmd = ctx.mmu.read32(virt_addr + 0x0C);
    uint32_t in_buf = ctx.mmu.read32(virt_addr + 0x10);
    uint32_t in_size = ctx.mmu.read32(virt_addr + 0x14);
    uint32_t out_buf = ctx.mmu.read32(virt_addr + 0x18);
    uint32_t out_size = ctx.mmu.read32(virt_addr + 0x1C);

    std::cout << "[HW IPC] Ioctl fd=" << fd << " cmd=0x" << std::hex << ioctl_cmd << std::dec << "\n";

    IpcRequest req{};
    req.fd = fd;
    req.req_addr = virt_addr;
    req.ioctl_cmd = ioctl_cmd;
    req.in_buf = in_buf;
    req.in_size = in_size;
    req.out_buf = out_buf;
    req.out_size = out_size;
    result = IOSKernel::get().ioctl(ctx, req);
  } else if (cmd == 7) { // IOS_Ioctlv
    uint32_t fd = ctx.mmu.read32(virt_addr + 0x08);
    uint32_t ioctl_cmd = ctx.mmu.read32(virt_addr + 0x0C);
    uint32_t arg_in = ctx.mmu.read32(virt_addr + 0x10);
    uint32_t arg_out = ctx.mmu.read32(virt_addr + 0x14);
    uint32_t arg_array = ctx.mmu.read32(virt_addr + 0x18);

    IpcRequest req{};
    req.cmd = cmd;
    req.fd = fd;
    req.req_addr = virt_addr;
    req.ioctl_cmd = ioctl_cmd;
    req.arg_cnt_in = arg_in;
    req.arg_cnt_out = arg_out;

    std::cout << "[HW IPC] Parsed IOCTLV fd=" << fd << " cmd=" << std::hex << ioctl_cmd << std::dec 
              << " in=" << arg_in << " out=" << arg_out << "\n";

    for (uint32_t i = 0; i < arg_in + arg_out; i++) {
        IoctlvVector vec;
        vec.addr = ctx.mmu.read32(arg_array + i * 8);
        vec.len = ctx.mmu.read32(arg_array + i * 8 + 4);
        req.ioctlv_vecs.push_back(vec);
    }

    result = IOSKernel::get().ioctlv(ctx, req);
  }

  std::cout << "[HW IPC] cmd=" << cmd << " virt=0x" << std::hex << virt_addr
            << " -> result=" << (int32_t)result << std::dec << std::endl;

  // Write result back into the IPC request buffer only if we are actually replying
  if (result != -0x70000001) { // IPC_NO_REPLY
      nwii::runtime::g_mmu->write32(virt_addr + 4, (uint32_t)(int32_t)result);
  }
  return result;
}

namespace nwii {
namespace runtime {

void init_ipc_client(CPUContext &ctx) {
  IOSKernel::get().init();
  nwii::runtime::devices::register_all();
}

bool handle_syscall(CPUContext &ctx) {
  uint32_t syscall_id = ctx.gpr[0];
  uint32_t old_pc = ctx.pc; // Preserve PC so 'sc' falls through correctly

  switch (syscall_id) {
  case 0x61:
    IOS_Open(ctx);
    break;
  case 0x62:
    IOS_Close(ctx);
    break;
  case 0x63:
    IOS_Read(ctx);
    break;
  case 0x64:
    IOS_Write(ctx);
    break;
  case 0x65:
    IOS_Seek(ctx);
    break;
  case 0x66:
    IOS_Ioctl(ctx);
    break;
  case 0x67:
    IOS_Ioctlv(ctx);
    break;
  case 0x68:
    IOS_OpenAsync(ctx);
    break;
  case 0x69:
    IOS_CloseAsync(ctx);
    break;
  case 0x6A:
    IOS_ReadAsync(ctx);
    break;
  case 0x6B:
    IOS_IoctlAsync(ctx);
    break;
  case 0x6C:
    IOS_IoctlvAsync(ctx);
    break;
  default:
    // Treat unknown syscalls as OS traps (e.g. OSYieldThread) and process
    // pending callbacks. Silenced to prevent massive log spam from standard
    // threading loops.
    if (process_pending_callbacks(ctx))
      return true;
    break;
  }

  ctx.pc = old_pc; // Restore PC
  return false;
}

} // namespace runtime
} // namespace nwii

#include "runtime/cpu_context.h"

namespace nwii::runtime {

// Set by os.cpp when an HW IPC request is processed.
// Consumed by process_pending_callbacks after the ISR callback finishes to
// re-apply the OS wake bit that the scheduler clears in single-threaded mode.
bool g_ipc_reply_pending = false;

int g_ipc_interrupt_delay = 0;

// Default at end of MEM1. Override per-game in assets/games/<id>.toml:
// [quirks] callback_stack_top = 0x...
static constexpr uint32_t CALLBACK_STACK_TOP_DEFAULT = 0x816F0000;

static inline uint32_t get_callback_stack_top() {
    uint32_t v = Config::get().game_profile.callback_stack_top;
    return v ? v : CALLBACK_STACK_TOP_DEFAULT;
}

void hle_drive_thread_queue(CPUContext& ctx) {
    // ThreadManager is deprecated in favor of native guest OS thread structures.
}

// OSContext layout (GC/RVL SDK ABI):
//   0x000 gpr[32], 0x080 cr, 0x084 lr, 0x088 ctr, 0x08C xer,
//   0x090 fpr[32], 0x194 fpscr, 0x198 srr0, 0x19C srr1,
//   0x1A4 gqr[8], 0x1C8 psf[32]; total 0x2C8
static void hle_save_context_to_guest(CPUContext &ctx, uint32_t c) {
  if (std::getenv("NWII_SAMPLE")) {
      static int n = 0;
      if (n++ < 40)
          std::cout << "[CtxSave] #" << n << " ctx=0x" << std::hex << c
                    << " srr0<=0x" << ctx.pc << " msr=0x" << ctx.msr
                    << " lr=0x" << ctx.lr << std::dec << "\n";
  }
  for (int i = 0; i < 32; i++)
    ctx.mmu.write32(c + i * 4, ctx.gpr[i]);
  // Mark this as an exception-saved (full) context, like the real 0x500
  // prologue does: OSLoadContext keys on OS_CONTEXT_STATE_EXC (bit 1 of the
  // state halfword at +0x1A2) to restore r5-r12 too; without it the light
  // path reloads only r13-r31 and the interrupted code resumes with junk in
  // the volatile registers.
  ctx.mmu.write16(c + 0x1A2, ctx.mmu.read16(c + 0x1A2) | 0x0002);
  uint32_t cr_val = 0;
  for (int i = 0; i < 8; i++) {
    cr_val |= (ctx.cr[i].lt ? 8u : 0) << (28 - i * 4);
    cr_val |= (ctx.cr[i].gt ? 4u : 0) << (28 - i * 4);
    cr_val |= (ctx.cr[i].eq ? 2u : 0) << (28 - i * 4);
    cr_val |= (ctx.cr[i].so ? 1u : 0) << (28 - i * 4);
  }
  ctx.mmu.write32(c + 0x80, cr_val);
  ctx.mmu.write32(c + 0x84, ctx.lr);
  ctx.mmu.write32(c + 0x88, ctx.ctr);
  ctx.mmu.write32(c + 0x8C, ctx.xer);
  for (int i = 0; i < 32; i++) {
    uint64_t d;
    std::memcpy(&d, &ctx.fpr[i], 8);
    ctx.mmu.write64(c + 0x90 + i * 8, d);
  }
  ctx.mmu.write32(c + 0x194, ctx.fpscr);
  ctx.mmu.write32(c + 0x198, ctx.pc);  // srr0 = interrupted PC
  ctx.mmu.write32(c + 0x19C, ctx.msr); // srr1 = interrupted MSR
  for (int i = 0; i < 8; i++)
    ctx.mmu.write32(c + 0x1A4 + i * 4, ctx.gqr[i]);
  for (int i = 0; i < 32; i++) {
    uint64_t d;
    std::memcpy(&d, &ctx.ps1[i], 8);
    ctx.mmu.write64(c + 0x1C8 + i * 8, d);
  }
}

// Restore CPU state from __OSCurrentContext; equivalent to the guest's own
// OSLoadContext. Returns false when no context is installed yet.
static bool hle_load_context_from_guest(CPUContext &ctx) {
  uint32_t c = ctx.mmu.read32(0x800000D4);
  if (c == 0)
    return false;
  for (int i = 0; i < 32; i++)
    ctx.gpr[i] = ctx.mmu.read32(c + i * 4);
  uint32_t cr_val = ctx.mmu.read32(c + 0x80);
  for (int i = 0; i < 8; i++) {
    uint32_t nib = (cr_val >> (28 - i * 4)) & 0xF;
    ctx.cr[i].lt = (nib & 8) != 0;
    ctx.cr[i].gt = (nib & 4) != 0;
    ctx.cr[i].eq = (nib & 2) != 0;
    ctx.cr[i].so = (nib & 1) != 0;
  }
  ctx.lr = ctx.mmu.read32(c + 0x84);
  ctx.ctr = ctx.mmu.read32(c + 0x88);
  ctx.xer = ctx.mmu.read32(c + 0x8C);
  for (int i = 0; i < 32; i++) {
    uint64_t d = ctx.mmu.read64(c + 0x90 + i * 8);
    std::memcpy(&ctx.fpr[i], &d, 8);
  }
  ctx.fpscr = ctx.mmu.read32(c + 0x194);
  for (int i = 0; i < 8; i++)
    ctx.gqr[i] = ctx.mmu.read32(c + 0x1A4 + i * 4);
  for (int i = 0; i < 32; i++) {
    uint64_t d = ctx.mmu.read64(c + 0x1C8 + i * 8);
    std::memcpy(&ctx.ps1[i], &d, 8);
  }
  ctx.pc = ctx.mmu.read32(c + 0x198);  // srr0
  ctx.msr = ctx.mmu.read32(c + 0x19C); // srr1
  // Consume OS_CONTEXT_STATE_EXC exactly like the guest's OSLoadContext
  // full-restore path does. Leaving it set poisons the context: a later
  // light OSSaveContext (r13-r31 only) followed by OSLoadContext would see
  // the stale flag and reload r5-r12 from exception-era slots — random
  // volatile-register garbage that surfaced as jump-into-format-string and
  // negative memcpy lengths.
  ctx.mmu.write16(c + 0x1A2, ctx.mmu.read16(c + 0x1A2) & ~0x0002);
  return true;
}


int g_trace_after_dec = 0;

bool process_pending_callbacks(CPUContext &ctx) {
  // EE rising-edge probe: logs where interrupts get re-enabled. An idle
  // system must reach the SDK idle spin with EE=1; if this never fires
  // after boot, every resumed context is stuck in EE=0 scheduler code.
  if (std::getenv("NWII_SAMPLE")) {
      static uint32_t last_ee = 0, edges = 0;
      uint32_t ee = ctx.msr & 0x8000;
      if (ee && !last_ee && edges < 8)
          std::cout << "[EEup] #" << ++edges << " pc=0x" << std::hex << ctx.pc
                    << " lr=0x" << ctx.lr << std::dec << "\n";
      last_ee = ee;
  }
  // One-shot control-flow trace armed by the first DEC fire: prints the pc
  // seen at every backedge/dispatcher check so the handler's exit path
  // (sentinel vs OSLoadContext/rfi vs a structural loop) becomes visible.
  if (g_trace_after_dec > 0) {
      static int skip = 20000; // land the trace window in the steady-state ring
      if (skip > 0) { skip--; }
      else {
          g_trace_after_dec--;
          std::cout << "[T] 0x" << std::hex << ctx.pc << " lr=0x" << ctx.lr
                    << " r1=0x" << ctx.gpr[1] << " r3=0x" << ctx.gpr[3]
                    << " r29=0x" << ctx.gpr[29] << " sr0=0x" << ctx.srr0
                    << std::dec << "\n";
      }
  }
  // Targeted loop tracer: NWII_LOOPTRACE=hexpc logs GPRs each time the
  // guest hits that PC (recompiled backedges call us with ctx.pc set).
  {
    static uint32_t trace_pc = []() -> uint32_t {
      const char *e = std::getenv("NWII_LOOPTRACE");
      return e ? (uint32_t)std::strtoul(e, nullptr, 16) : 0;
    }();
    static uint64_t hits = 0;
    if (trace_pc && ctx.pc == trace_pc) {
      if ((hits++ % 200000) == 0)
        std::cout << "[Loop 0x" << std::hex << trace_pc << "] #" << std::dec
                  << hits << " r3=0x" << std::hex << ctx.gpr[3] << " r4=0x"
                  << ctx.gpr[4] << " r5=0x" << ctx.gpr[5] << " r30=0x"
                  << ctx.gpr[30] << " r31=0x" << ctx.gpr[31] << " lr=0x"
                  << ctx.lr << std::dec << "\n";
    }
  }
  // Hot-PC sampler: NWII_SAMPLE=1 prints ctx.pc every 400000 backedge
  // checks. A PC that keeps repeating is where the guest is spinning.
  {
    static bool sample = std::getenv("NWII_SAMPLE") != nullptr;
    static uint64_t n = 0;
    if (sample && (++n % 400000) == 0)
      std::cout << "[Sample] pc=0x" << std::hex << ctx.pc << " lr=0x" << ctx.lr
                << " r3=0x" << ctx.gpr[3] << std::dec << "\n";
  }
  // Sentinel return from a context-dispatched handler (blr with LR=0xFFFFFFFC).
  // Act like the tail of __OSDispatchInterrupt: re-install the context WE
  // saved at dispatch time and load it. Handlers may leave their own
  // exception context in __OSCurrentContext (the SDK dispatcher is the one
  // that puts the interrupted context back); trusting 0x800000D4 here used
  // to restore a never-saved stack context with srr0=0.
  if (ctx.pc == 0xFFFFFFFC && !ctx.in_callback) {
    // Post-ISR reschedule (see cpu_context.h): after an external-interrupt leaf
    // handler returns, hand off to the guest __OSReschedule(0) once so a
    // higher-priority thread the handler just readied can preempt the
    // interrupted one, exactly as the real __OSDispatchInterrupt does. The
    // outgoing context is already saved by our dispatch; __OSReschedule either
    // switches away (OSLoadContext) or, if the interrupted thread is still
    // highest priority, returns here (flag now clear) to resume it.
    static uint32_t g_resched_addr = []() -> uint32_t {
      const char *e = std::getenv("NWII_RESCHED");
      return e ? (uint32_t)std::strtoul(e, nullptr, 16) : 0;
    }();
    if (ctx.ext_resched_pending && g_resched_addr) {
      ctx.ext_resched_pending = false;
      if (std::getenv("NWII_SAMPLE")) {
        static int n = 0;
        if (n++ < 12) {
          uint32_t cur = ctx.mmu.read32(0x800000E4);
          std::cout << "[Resched] cur=0x" << std::hex << cur
                    << " prio=" << std::dec << ctx.mmu.read32(cur + 0x2D0)
                    << " r1=0x" << std::hex << ctx.gpr[1] << std::dec << "\n";
        }
      }
      ctx.gpr[3] = 0;
      ctx.lr = 0xFFFFFFFC;
      ctx.pc = g_resched_addr;
      return true;
    }
    ctx.ext_resched_pending = false;
    if (ctx.dispatch_saved_ctx) {
      uint32_t current_thread = ctx.mmu.read32(0x800000E4);
      if (current_thread != 0 && current_thread != ctx.dispatch_saved_ctx) {
        ctx.mmu.write32(0x800000D4, current_thread);
      } else {
        ctx.mmu.write32(0x800000D4, ctx.dispatch_saved_ctx);
      }
      ctx.dispatch_saved_ctx = 0;
    }
    if (hle_load_context_from_guest(ctx)) {
      // An interrupt/exception handler runs in what the guest models as a
      // few microseconds, but in our inst_count timebase it burns thousands
      // of ticks (interpreted context save + recompiled reschedule). Restart
      // the decrementer countdown from the moment we resume the interrupted
      // thread, so a short OSAlarm period (e.g. dec=114) can't re-expire
      // before that thread executes a single instruction and starve it.
      ctx.dec_written_tb = ctx.read_timebase();
      static uint32_t restore_count = 0;
      restore_count++;
      if (ctx.pc == 0 || (restore_count % 64) == 1) {
          std::cout << "[HLE PI] Context restore #" << std::dec << restore_count
                    << " pc=0x" << std::hex << ctx.pc << " r1=0x" << ctx.gpr[1]
                    << " msr=0x" << ctx.msr << std::dec << "\n";
      }
      return true;
    }
  }

  if ((ctx.inst_count % 20000000) == 0) {
      std::cout << "[Heartbeat] PC: 0x" << std::hex << ctx.pc << " LR: 0x" << ctx.lr
                << " intmr=0x" << nwii::runtime::hw::pi_intmr.load()
                << " intsr=0x" << nwii::runtime::hw::pi_intsr.load()
                << " msr=0x" << ctx.msr
                << " dec_h=0x" << ctx.mmu.read32(0x80003020) << "\n";
      {
          std::cout << "  [ExcTable]";
          for (int i = 0; i < 16; i++) {
              uint32_t h = ctx.mmu.read32(0x80003000 + i * 4);
              if (h) std::cout << " " << i << ":0x" << std::hex << h << std::dec;
          }
          std::cout << "\n";
      }
      // Dump every thread: __OSCurrentThread (0x800000E4) plus the whole
      // active queue (head 0x800000DC). OSThread layout: state u16 @0x2C8,
      // priority @0x2D0, wait queue ptr @0x2DC, linkActive next @0x2FC,
      // linkActive prev @0x300, srr0 in context @0x198
      auto dump_thread = [&](uint32_t th, const char *tag) {
          uint16_t state = ctx.mmu.read16(th + 0x2C8);
          uint32_t prio = ctx.mmu.read32(th + 0x2D0);
          uint32_t wqueue = ctx.mmu.read32(th + 0x2DC);
          uint32_t srr0 = ctx.mmu.read32(th + 0x198);
          uint32_t slr = ctx.mmu.read32(th + 0x84);
          std::cout << "  [" << tag << "] 0x" << std::hex << th << " state=" << state
                    << " prio=" << std::dec << prio << " waitq=0x" << std::hex
                    << wqueue << " srr0=0x" << srr0 << " lr=0x" << slr;
          // Walk stack frames: [sp] = back chain, [sp+4] = saved LR
          uint32_t sp = ctx.mmu.read32(th + 4);
          for (int f = 0; f < 6 && sp != 0 && sp < 0x94000000; ++f) {
              uint32_t ret = ctx.mmu.read32(sp + 4);
              if (ret) std::cout << " <-0x" << std::hex << ret;
              sp = ctx.mmu.read32(sp);
          }
          std::cout << std::dec << "\n";
      };
      // NWII_PEEK=hexaddr[,words]: dump guest memory each heartbeat
      {
          static uint32_t peek_addr = 0, peek_words = 8;
          static bool peek_parsed = false;
          if (!peek_parsed) {
              peek_parsed = true;
              if (const char *env = std::getenv("NWII_PEEK")) {
                  peek_addr = (uint32_t)std::strtoul(env, nullptr, 16);
                  if (const char *c = std::strchr(env, ','))
                      peek_words = (uint32_t)std::strtoul(c + 1, nullptr, 10);
              }
          }
          if (peek_addr) {
              std::cout << "  [Peek 0x" << std::hex << peek_addr << "]";
              for (uint32_t i = 0; i < peek_words; i++)
                  std::cout << " " << ctx.mmu.read32(peek_addr + i * 4);
              std::cout << std::dec << "\n";
          }
      }
      uint32_t cur = ctx.mmu.read32(0x800000E4);
      if (cur) dump_thread(cur, "CurThread");
      uint32_t th = ctx.mmu.read32(0x800000DC);
      int guard = 0;
      while (th != 0 && guard++ < 16) {
          if (th != cur) dump_thread(th, "Thread");
          th = ctx.mmu.read32(th + 0x2FC);
      }
  }

  // One-shot idle-deadlock dump: when the CPU falls to pc==0 (no runnable
  // thread) repeatedly, report every thread and what it waits on plus the
  // interrupt-enable state, so the missing wakeup source is visible.
  if (ctx.pc == 0) {
      static uint64_t idle_hits = 0;
      if (++idle_hits == 200000) {
          std::cout << "[IDLE] no runnable thread. intmr=0x" << std::hex
                    << nwii::runtime::hw::pi_intmr << " intsr=0x"
                    << nwii::runtime::hw::pi_intsr << " dec_armed="
                    << std::dec << ctx.dec_irq_pending << "\n";
          uint32_t cur = ctx.mmu.read32(0x800000E4);
          uint32_t th = ctx.mmu.read32(0x800000DC);
          int guard = 0;
          while (th != 0 && guard++ < 24) {
              uint16_t state = ctx.mmu.read16(th + 0x2C8);
              uint32_t prio = ctx.mmu.read32(th + 0x2D0);
              uint32_t wq = ctx.mmu.read32(th + 0x2DC);
              uint32_t srr0 = ctx.mmu.read32(th + 0x198);
              std::cout << "  [IdleThread] 0x" << std::hex << th
                        << (th == cur ? "*" : " ") << " state=" << std::dec
                        << state << " prio=" << prio << " waitq=0x" << std::hex
                        << wq << " srr0=0x" << srr0 << std::dec << "\n";
              th = ctx.mmu.read32(th + 0x2FC);
          }
      }
  }

  if (g_ipc_interrupt_delay > 0) {
      g_ipc_interrupt_delay--;
      if (g_ipc_interrupt_delay == 0) {
          nwii::runtime::hw::trigger_pi_interrupt(0x00004000);
          if (std::getenv("NWII_SAMPLE"))
              std::cout << "[IPC] irq raised intmr=0x" << std::hex
                        << nwii::runtime::hw::pi_intmr << " msr=0x" << ctx.msr
                        << " pc=0x" << ctx.pc << std::dec << "\n";
      }
  }

  // Drive-command completion: di_execute() arms a short delay so the OS can
  // return from the TSTART write before the DI interrupt preempts it.
  nwii::runtime::hw::di_tick();

  if (ctx.vblank_pending) {
      if (!ctx.in_callback && (ctx.msr & 0x8000)) {
          ctx.vblank_pending = false;
          nwii::runtime::hw::vi_trigger_interrupt();
          // PE token/finish are no longer faked here: the GX write-gather
          // capture signals them when the game's own command stream carries
          // BP 0x45 (draw-done) / 0x47/0x48 (draw-sync token), so GXDrawDone
          // and token waits complete with the right token value.
          hle_drive_thread_queue(ctx);
      }
  }


  if (ctx.in_callback && ctx.pc == 0xFFFFFFFC) {
    // Callback is finishing
    ctx.in_callback = false;
  }

  if (ctx.in_callback) {
    if (std::getenv("NWII_SAMPLE") && (nwii::runtime::hw::pi_intsr & 0x4000)) {
        static uint64_t n = 0;
        if ((n++ % 100000) == 0)
            std::cout << "[IPC] irq pending but in_callback pc=0x" << std::hex
                      << ctx.pc << " lr=0x" << ctx.lr << " depth="
                      << std::dec << ctx.callback_depth << "\n";
    }
    return false;
  }

  // Periodically raise VI (PI_INTSR bit 8 = 0x100, Dolphin INT_CAUSE_VI)
  // once the game unmasked it. Substitutes for real video timing.
  // Pre-VIInit wakeups are the decrementer's job, not VI's.
  // Guard against a retrace storm: a long VI ISR that re-enables EE mid-way
  // would let the next periodic tick assert a fresh VI and nest the handler
  // (each nesting pushes the guest stack down ~0x300 until it wraps past 0 and
  // control jumps through a null pointer). Only assert VI when no handler is in
  // flight (dispatch_saved_ctx==0) and the line is not already pending — real
  // hardware keeps a single retrace asserted until the ISR acknowledges it.
  // Threshold (not exact-modulo) so it still fires inside a tight guest idle
  // loop: the __OSReschedule idle spin is 3 instructions, and inst_count
  // stepping past an exact multiple of 500000 skipped the `% == 0` check
  // entirely — VI never fired during idle, the frame counter never advanced,
  // and a thread sleeping for retrace (e.g. MP7's main) hung forever.
  // VI retrace is now a recurring scheduler event (registered once below);
  // advancing the scheduler here fires it — and every other scheduled
  // hardware event (audio DMA, DI completion) — at the right tick.
  {
      static bool vi_registered = false;
      if (!vi_registered) {
          vi_registered = true;
          // One field time in timebase ticks. In wall-clock mode this is a
          // real ~60Hz retrace; in execution mode it is TB_freq/60 guest
          // instructions, matching the old ~500k threshold.
          uint64_t vi_period = ctx.tb_freq / 60;
          if (vi_period == 0) vi_period = 500000;
          nwii::runtime::EventScheduler::get().schedule_recurring(
              vi_period, [](CPUContext& c, uint64_t) {
                  // Same storm guard as before: assert a retrace only when no
                  // handler is in flight (or the guest is idle) and the line is
                  // not already pending — real hardware keeps one retrace
                  // asserted until the ISR acknowledges it.
                  if ((nwii::runtime::hw::pi_intmr & 0x100) && !c.in_callback &&
                      !(nwii::runtime::hw::pi_intsr & 0x100) &&
                      (c.dispatch_saved_ctx == 0 || c.mmu.read32(0x800000E4) == 0)) {
                      nwii::runtime::hw::vi_trigger_interrupt();
                  }
              });
      }
      nwii::runtime::EventScheduler::get().advance(ctx, ctx.read_timebase());
  }

  // Fire DEC exception only on actual underflow (game arms via mtdec).
  // Avoids starving recompiled tight loops with premature reschedules.
  if (ctx.dec_expired() && (ctx.msr & 0x8000) && !ctx.in_callback) {
      ctx.dec_irq_pending = false; // consumed; re-arms on next mtdec
      if (std::getenv("NWII_SAMPLE")) {
          static uint32_t df = 0;
          if (df++ < 12)
              std::cout << "[DECfire] #" << df << " pc=0x" << std::hex << ctx.pc
                        << " lr=0x" << ctx.lr << " inst=" << std::dec
                        << ctx.inst_count << "\n";
          extern int g_trace_after_dec;
          if (df == 1) g_trace_after_dec = 300;
      }


      // Alarm-spin deadlock breaker: if DEC fires 64 times at the same PC,
      // the thread is stuck waiting for an OSAlarm that can't self-advance.
      // Scan SDA below r13 for OSAlarm structs and force-expire pending ones.
      // OSAlarm layout: +0x08 fire_hi, +0x0C fire_lo, +0x18 handler.
      {
          static uint32_t dec_same_pc_count = 0;
          static uint32_t dec_last_pc = 0;
          if (ctx.pc == dec_last_pc) {
              dec_same_pc_count++;
          } else {
              dec_same_pc_count = 0;
              dec_last_pc = ctx.pc;
          }

          if (dec_same_pc_count == 64) {
              dec_same_pc_count = 0;
              if (ctx.gpr[13] != 0) {
                  uint64_t now_tb = ctx.read_timebase();
                  uint32_t now_hi = (uint32_t)(now_tb >> 32);
                  uint32_t now_lo = (uint32_t)(now_tb & 0xFFFFFFFF);
                  uint32_t base = ctx.gpr[13];
                  for (int i = 0; i < 512; i++) {
                      uint32_t addr = base - (uint32_t)(i * 4);
                      if (addr < 0x80000000u || addr > 0x81FFFFFFu) continue;
                      uint32_t handler = ctx.mmu.read32(addr + 0x18);
                      if (handler < 0x80000000u || handler >= 0x82000000u) continue;
                      uint32_t fire_hi = ctx.mmu.read32(addr + 0x08);
                      uint32_t fire_lo = ctx.mmu.read32(addr + 0x0C);
                      if (fire_hi == 0 && fire_lo == 0) continue;
                      if (fire_hi < now_hi || (fire_hi == now_hi && fire_lo <= now_lo))
                          continue;
                      ctx.mmu.write32(addr + 0x08, now_hi);
                      ctx.mmu.write32(addr + 0x0C, now_lo);
                      static uint32_t forced_count = 0;
                      if ((forced_count++ % 32) == 0)
                          std::cout << "[HLE DEC] force-expired alarm 0x"
                                    << std::hex << addr << " handler=0x"
                                    << handler << std::dec << "\n";
                  }
              }
              return false;
          }
      }

      // Every 500th DEC, dump all threads + their wait objects so a
      // scheduler-churn deadlock (all threads blocked) is visible.
      static uint32_t dcount = 0;
      if ((dcount++ % 500) == 0) {
          uint32_t cur = ctx.mmu.read32(0x800000E4);
          uint32_t th = ctx.mmu.read32(0x800000DC);
          std::cout << "[Sched] cur=0x" << std::hex << cur << std::dec << "\n";
          int guard = 0;
          while (th != 0 && guard++ < 24) {
              uint16_t state = ctx.mmu.read16(th + 0x2C8);
              uint32_t prio = ctx.mmu.read32(th + 0x2D0);
              uint32_t wq = ctx.mmu.read32(th + 0x2DC);
              uint32_t srr0 = ctx.mmu.read32(th + 0x198);
              std::cout << "  th=0x" << std::hex << th << (th==cur?"*":" ")
                        << " state=" << std::dec << state << " prio=" << prio
                        << " waitq=0x" << std::hex << wq << " srr0=0x" << srr0
                        << std::dec << "\n";
              // Peek at the wait object so an empty queue (producer never
              // posts) is distinguishable from a stuck wakeup (data present
              // but the sleeper never runs). The waitq pointer aims at a
              // thread-queue embedded in an OSMessageQueue/OSSemaphore, so
              // dumping the words around it shows count/first/used fields.
              if (wq >= 0x80000000u && wq < 0x81800000u &&
                  std::getenv("NWII_SAMPLE")) {
                  std::cout << "    wq[-2..5]:" << std::hex;
                  for (int i = -2; i < 6; ++i)
                      std::cout << " " << ctx.mmu.read32(wq + i * 4);
                  std::cout << std::dec << "\n";
              }
              th = ctx.mmu.read32(th + 0x2FC);
          }
      }
      uint32_t dec_handler = ctx.mmu.read32(0x80003000 + 8 * 4);
      uint32_t current_ctx = ctx.mmu.read32(0x800000D4);
      if (dec_handler != 0 && dec_handler != 0xFFFFFFFF && current_ctx != 0) {
          hle_save_context_to_guest(ctx, current_ctx);
          ctx.dispatch_saved_ctx = current_ctx;
          ctx.srr0 = ctx.pc;
          ctx.srr1 = ctx.msr;
          ctx.msr &= ~0x8000;
          ctx.gpr[3] = 8;           // __OS_EXCEPTION_DECREMENTER
          ctx.gpr[4] = current_ctx; // OSContext*
          // Real exception vectors run the handler on the interrupted
          // stack without moving r1 and without touching guest memory:
          // handlers that exit via a light rfi never restore GPRs, so any
          // r1 adjustment here would leak stack on every dispatch.
          ctx.lr = 0xFFFFFFFC;
          ctx.pc = dec_handler;
          longjmp(ctx.exception_jmp_buf, 1);
      }
  }


  uint32_t active_ints = nwii::runtime::hw::pi_intsr & nwii::runtime::hw::pi_intmr;

  // External interrupts are only taken when MSR.EE=1, like real hardware.
  // Pending bits stay set in pi_intsr until the OS re-enables interrupts.
  if (active_ints != 0 && (ctx.msr & 0x8000)) {

      // §6.3 Guard: do not interrupt the guest scheduler in its critical section.
      // If the OS scheduler is disabled (disable-count > 0 at [r13-0x6e60]) or
      // another handler is already in flight (dispatch_saved_ctx != 0), defer
      // the interrupt. This prevents "worker parked mid-reschedule" corruption
      // where HLE interrupts __OSReschedule itself, leaving it in an
      // inconsistent state and eventually causing PC=0 wild jumps.
      bool sched_disabled = false;
      uint32_t r13 = ctx.gpr[13];
      uint32_t lock_off = nwii::runtime::Config::get().game_profile.sched_lock_offset;
      if (lock_off != 0 && r13 >= 0x80200000u && r13 < 0x81800000u) {
          uint32_t lock_addr = r13 - lock_off;
          if (lock_addr >= 0x80000000u && lock_addr < 0x82000000u) {
              uint32_t lock_val = ctx.mmu.read32(lock_addr);
              // Scheduler lock count is a small integer (typically 0, 1, or 2).
              // Values > 255 indicate uninitialized memory — ignore.
              sched_disabled = (lock_val > 0 && lock_val < 256);
          }
      }
      bool handler_in_flight = (ctx.dispatch_saved_ctx != 0);

      // The §6.3 guard and the whole __OSDispatchInterrupt reschedule path are
      // only enabled for games that explicitly set [hle] ext_interrupt_dispatch
      // (currently MP7). For legacy-leaf games (NFS/SHSM) deferring on
      // handler_in_flight starved the first IPC interrupt and stalled the boot,
      // so those games keep the original leaf-dispatch behaviour untouched.
      bool use_new_path =
          (nwii::runtime::Config::get().game_profile.ext_interrupt_dispatch != 0);

      // Exception: when there is no current thread (__OSCurrentThread == 0) the
      // guest is sitting in the __OSReschedule idle loop, which deliberately
      // enables interrupts and spins until an ISR readies a thread. Deferring
      // there would hang the idle loop forever (the interrupt that would wake a
      // sleeper never fires), so the scheduler lock is ignored when idle.
      bool idle = (ctx.mmu.read32(0x800000E4) == 0);

      // handler_in_flight is NOT subject to that exception. Re-entering dispatch
      // while a handler is still in flight makes hle_save_context_to_guest
      // overwrite dispatch_saved_ctx, losing the first handler's context and
      // rfi-ing to a corrupted srr0. The idle loop runs with
      // __OSCurrentThread == 0 for the whole handler, so the old condition let
      // exactly that happen whenever a second interrupt landed inside the
      // handler's EE window — a pure timing race.
      if (use_new_path && (handler_in_flight || (!idle && sched_disabled))) {
          // Interrupt stays pending in pi_intsr; retry next backedge.
          if (std::getenv("NWII_SAMPLE")) {
              static uint64_t defer_count = 0;
              if ((++defer_count % 500000) == 0)
                  std::cout << "[HLE PI] int deferred: sched_disabled="
                            << sched_disabled << " in_flight=" << handler_in_flight
                            << " pc=0x" << std::hex << ctx.pc << std::dec << "\n";
          }
      } else {
          // --- Determine __OSDispatchInterrupt address (lazy, cached) ---
          // Priority: config > ExcTable[4] (games that fill it: NFS, SHSM) >
          // 0 (games like MP7 that use physical 0x500 stub without ExcTable).
          static uint32_t s_dispatch_addr = 0xFFFFFFFF; // sentinel = not yet resolved
          if (s_dispatch_addr == 0xFFFFFFFF) {
              // 1. From game profile config (highest priority, explicit)
              uint32_t from_cfg = nwii::runtime::Config::get().game_profile.ext_interrupt_dispatch;
              if (from_cfg != 0) {
                  s_dispatch_addr = from_cfg;
                  std::cout << "[HLE PI] __OSDispatchInterrupt from config: 0x"
                            << std::hex << s_dispatch_addr << std::dec << "\n";
              } else {
                  // No explicit config → keep the proven leaf-dispatch path.
                  // (Auto-resolving from ExcTable[4] was regressing SHSM: the
                  // reschedule path is opt-in per game via ext_interrupt_dispatch.)
                  s_dispatch_addr = 0;
              }
          }

          // --- Route interrupt ---
          if (s_dispatch_addr != 0) {
              // NEW PATH: forward directly to guest __OSDispatchInterrupt.
              // The guest dispatcher will:
              //   1. Save interrupted context (its own convention)
              //   2. Decode PI_INTSR and clear the cause bit
              //   3. Call the leaf handler
              //   4. Call __OSReschedule(0) → potentially switch threads
              //   5. OSLoadContext / rfi — returns to whoever won the schedule
              //
              // We must NOT pre-save context, NOT pre-clear PI_INTSR, NOT set
              // dispatch_saved_ctx — the guest handles all of this.
              //
              // The only setup we do is what the real 0x500 exception prologue
              // does before jumping to __OSDispatchInterrupt: set srr0/srr1,
              // disable EE, and jump. The dispatcher reads PI_INTSR itself.
              ctx.srr0 = ctx.pc;
              ctx.srr1 = ctx.msr;

              // The hardware exception prologue reads __OSCurrentContext (0x800000D4).
              // If it is NULL (idle), it falls back to __OSDefaultThread's context
              // (or __OSExceptionContext) stored at 0x800000D0.
              uint32_t current_ctx = ctx.mmu.read32(0x800000D4);
              if (current_ctx == 0) {
                  current_ctx = ctx.mmu.read32(0x800000D0);
              }
              if (current_ctx == 0) {
                  std::cout << "[HLE PI] FATAL: current_ctx is 0! pc=0x" << std::hex << ctx.pc << " intmr=0x" << nwii::runtime::hw::pi_intmr << " intsr=0x" << nwii::runtime::hw::pi_intsr << std::dec << "\n";
              }

              // Save the full interrupted CPU state into the OSContext.
              // The hardware exception prologue does this. The guest dispatcher
              // will later call OSLoadContext(current_ctx) to restore it.
              if (current_ctx != 0) {
                  hle_save_context_to_guest(ctx, current_ctx);
              }

              // Set up arguments for the C handler
              // r3 = exception number (0x4 = external interrupt, matches SDK)
              ctx.gpr[3] = 0x04;
              // r4 = pointer to interrupted OSContext (what 0x500 passes)
              ctx.gpr[4] = current_ctx;

              // Now disable EE, just like the real hardware exception vector does
              // BEFORE jumping into the C handler.
              ctx.msr &= ~0x8000u; // EE=0 inside exception handler

              // Sentinel LR so our sentinel-return handler sees the return if
              // the dispatcher ever blr's back (should not happen normally —
              // the real dispatcher exits via OSLoadContext/rfi).
              ctx.lr = 0xFFFFFFFC;
              ctx.pc = s_dispatch_addr;

              if (std::getenv("NWII_SAMPLE")) {
                  static uint32_t dc = 0;
                  if ((dc++ % 1000) == 0)
                      std::cout << "[HLE PI] -> __OSDispatchInterrupt #" << std::dec
                                << dc << " pc=0x" << std::hex << ctx.pc
                                << " cur=0x" << ctx.mmu.read32(0x800000E4)
                                << std::dec << "\n";
              }

              longjmp(ctx.exception_jmp_buf, 1);

          } else {
              // LEGACY LEAF PATH (games without ExcTable[4] and no config override).
              // Decodes os_intr and dispatches directly to the leaf handler.
              // No reschedule after the handler — threads are not preempted.
              int os_intr = -1;
              uint32_t bit_to_clear = 0;
              // PI_INTSR bits → __OSInterruptTable index mapping
              if      (active_ints & 0x00004000) { os_intr = 27; bit_to_clear = 0x00004000; } // IPC (Wii ACR)
              else if (active_ints & 0x00000100) { os_intr = 24; bit_to_clear = 0x00000100; } // VI
              else if (active_ints & 0x00000004) { os_intr = 21; bit_to_clear = 0x00000004; } // DI
              else if (active_ints & 0x00000008) { os_intr = 20; bit_to_clear = 0x00000008; } // SI
              else if (active_ints & 0x00000010) { os_intr =  9; bit_to_clear = 0x00000010; } // EXI 0
              // PI cause bit 6 covers the whole DSP subsystem; the DSPCSR
              // decides whether it is AID (5), ARAM (6) or mailbox (7).
              else if (active_ints & 0x00000040) { os_intr = nwii::runtime::hw::dsp_pending_os_interrupt(); bit_to_clear = 0x00000040; } // DSP/ARAM/AID
              else if (active_ints & 0x00000020) { os_intr =  8; bit_to_clear = 0x00000020; } // AI streaming
              else if (active_ints & 0x00000400) { os_intr = 19; bit_to_clear = 0x00000400; } // PE_FINISH
              else if (active_ints & 0x00000200) { os_intr = 18; bit_to_clear = 0x00000200; } // PE_TOKEN
              else if (active_ints & 0x00000800) { os_intr = 17; bit_to_clear = 0x00000800; } // CP

              if (os_intr != -1) {
                  uint32_t handler = ctx.mmu.read32(0x80003040 + os_intr * 4);

                  if (handler != 0 && handler != 0xFFFFFFFF) {
                      if (std::getenv("NWII_SAMPLE") && os_intr == 24) {
                          static int vn = 0;
                          if (vn++ < 5)
                              std::cout << "[VIdisp] #" << vn << " handler=0x"
                                        << std::hex << handler << " pc=0x"
                                        << ctx.pc << std::dec << "\n";
                      }
                      // VI fires constantly; log only every 256th dispatch
                      static uint32_t vi_dispatch_count = 0;
                      if (os_intr != 24 && (vi_dispatch_count++ % 256) == 0) {
                          std::cout << "[HLE PI] Dispatching interrupt "
                                    << std::dec << os_intr << " to handler 0x"
                                    << std::hex << handler << " r1=0x"
                                    << ctx.gpr[1] << " pc=0x" << ctx.pc
                                    << std::dec << std::endl;
                      }
                      if (std::getenv("NWII_SAMPLE")) {
                          static uint64_t census[32] = {0};
                          static uint64_t total = 0;
                          if (os_intr >= 0 && os_intr < 32) census[os_intr]++;
                          if ((++total % 2000) == 0) {
                              std::cout << "[IntCensus]";
                              for (int i = 0; i < 32; ++i)
                                  if (census[i])
                                      std::cout << " " << std::dec << i
                                                << ":" << census[i];
                              std::cout << "\n";
                          }
                      }
                      uint32_t current_ctx = ctx.mmu.read32(0x800000D4);
                      if (current_ctx == 0) {
                          // No context to save into — defer.
                          if (std::getenv("NWII_SAMPLE")) {
                              static int n = 0;
                              if (n++ < 4)
                                  std::cout << "[HLE PI] int " << std::dec
                                            << os_intr << " deferred: no current context\n";
                          }
                      } else {
                          // Clear cause, save context, run leaf.
                          nwii::runtime::hw::pi_intsr &= ~bit_to_clear;
                          hle_save_context_to_guest(ctx, current_ctx);
                          ctx.dispatch_saved_ctx = current_ctx;
                          ctx.srr0 = ctx.pc;
                          ctx.srr1 = ctx.msr;
                          ctx.msr &= ~0x8000u; // EE=0
                          ctx.gpr[3] = os_intr;
                          ctx.gpr[4] = current_ctx;
                          ctx.lr = 0xFFFFFFFC;
                          ctx.pc = handler;
                          ctx.ext_resched_pending = true;
                          longjmp(ctx.exception_jmp_buf, 1);
                      }

                      // Legacy backup-stack path (pre-thread-system).
                      ctx.msr &= ~0x8000u;
                      CPUContext::BackupState bk;
                      bk.gpr = ctx.gpr;
                      bk.fpr = ctx.fpr;
                      bk.ps1 = ctx.ps1;
                      bk.cr  = ctx.cr;
                      bk.lr  = ctx.lr;
                      bk.ctr = ctx.ctr;
                      bk.xer = ctx.xer;
                      bk.pc  = ctx.pc;
                      bk.srr0 = ctx.srr0;
                      bk.srr1 = ctx.srr1;
                      bk.msr = ctx.msr | 0x8000;
                      bk.fpscr = ctx.fpscr;
                      bk.gqr = ctx.gqr;
                      bk.sprg = ctx.sprg;
                      ctx.backup_stack.push(bk);

                      ctx.in_callback = true;
                      ctx.callback_depth++;
                      ctx.srr0 = ctx.pc;
                      ctx.srr1 = ctx.msr | 0x8000;
                      ctx.gpr[3] = os_intr;
                      ctx.gpr[4] = 0;
                      ctx.lr = 0xFFFFFFFC;
                      ctx.pc = handler;
                      longjmp(ctx.exception_jmp_buf, 1);
                  } else {
                      // Handler is NULL — OS is mid-reinit. Leave pi_intsr set.
                      static uint32_t last_null_pc = 0;
                      static int null_warn_count = 0;
                      if (ctx.pc != last_null_pc) {
                          null_warn_count = 0;
                          last_null_pc = ctx.pc;
                      }
                      if (null_warn_count++ < 3)
                          std::cout << "[HLE PI] Handler for interrupt "
                                    << std::dec << os_intr
                                    << " is NULL (OS reinit in progress, deferring)."
                                    << std::endl;
                  }
              }
          } // end leaf path
      } // end !sched_disabled && !handler_in_flight
  } else if (nwii::runtime::hw::pi_intsr != 0) {
      // intsr has bits not in intmr — masked pending interrupt, no action needed
  }

  // Software callbacks require MSR EE=1 (game must explicitly re-enable interrupts)
  if ((ctx.msr & 0x8000) == 0)
    return false;

  // Lock-free fast path: skip mutex when no callbacks are queued
  if (ctx.pending_cb_count.load(std::memory_order_relaxed) == 0)
    return false;

  CallbackInfo cb;
  {
    std::lock_guard<std::mutex> lock(ctx.cb_mutex);
    if (ctx.pending_callbacks.empty())
      return false;
    cb = ctx.pending_callbacks.front();
    ctx.pending_callbacks.pop();
    ctx.pending_cb_count.fetch_sub(1, std::memory_order_relaxed);
  }

  std::cout << "[HLE Callback] Dispatching cb=0x" << std::hex << cb.cb_addr
            << " arg1=0x" << cb.arg1 << " arg2=0x" << cb.arg2 << " from PC=0x"
            << ctx.pc << " LR=0x" << ctx.lr << " r1=0x" << ctx.gpr[1]
            << std::dec << std::endl;

  uint32_t current_ctx = ctx.mmu.read32(0x800000D4);
  if (current_ctx != 0) {
    // Same model as hardware interrupts: state lives in __OSCurrentContext,
    // the callback runs like it was invoked from the IPC interrupt handler.
    hle_save_context_to_guest(ctx, current_ctx);
    ctx.dispatch_saved_ctx = current_ctx;

    ctx.srr0 = ctx.pc;
    ctx.srr1 = ctx.msr;
    ctx.msr &= ~0x8000; // interrupt context: EE=0

    ctx.gpr[3] = cb.arg1;
    ctx.gpr[4] = cb.arg2;

    // r1 stays untouched; the callback's prologue builds its own frame
    ctx.lr = 0xFFFFFFFC;
    ctx.pc = cb.cb_addr;
    longjmp(ctx.exception_jmp_buf, 1);
  }

  // Legacy path (no OS context installed): backup-stack + dedicated stack
  CPUContext::BackupState bk;
  bk.gpr = ctx.gpr;
  bk.fpr = ctx.fpr;
  bk.ps1 = ctx.ps1;
  bk.cr  = ctx.cr;
  bk.lr  = ctx.lr;
  bk.ctr = ctx.ctr;
  bk.xer = ctx.xer;
  bk.pc  = ctx.pc;
  bk.srr0 = ctx.srr0;
  bk.srr1 = ctx.srr1;
  bk.msr = ctx.msr;
  bk.fpscr = ctx.fpscr;
  bk.gqr = ctx.gqr;
  bk.sprg = ctx.sprg;
  ctx.backup_stack.push(bk);

  ctx.in_callback = true;
  ctx.callback_depth++;

  ctx.gpr[3] = cb.arg1;
  ctx.gpr[4] = cb.arg2;

  uint32_t cb_stack_top = get_callback_stack_top() - (ctx.callback_depth - 1) * 4096;
  ctx.gpr[1] = cb_stack_top - 16;
  ctx.mmu.write32(ctx.gpr[1], 0);
  ctx.mmu.write32(ctx.gpr[1] + 4, 0xFFFFFFFC);

  ctx.lr = 0xFFFFFFFC;
  ctx.pc = cb.cb_addr;
  longjmp(ctx.exception_jmp_buf, 1);
}

} // namespace nwii::runtime
