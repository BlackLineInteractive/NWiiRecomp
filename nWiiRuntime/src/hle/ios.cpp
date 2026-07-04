#include "runtime/platform/platform.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include "runtime/hw/hw.h"
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

extern "C" void handle_ios_ipc(CPUContext& ctx, uint32_t request_addr) {
  if (!nwii::runtime::g_mmu)
    return;

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

  // Write result back into the IPC request buffer
  nwii::runtime::g_mmu->write32(virt_addr + 4, (uint32_t)(int32_t)result);
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
    ThreadManager::get().on_vblank();
}

// OSContext layout (GC/RVL SDK ABI):
//   0x000 gpr[32], 0x080 cr, 0x084 lr, 0x088 ctr, 0x08C xer,
//   0x090 fpr[32], 0x194 fpscr, 0x198 srr0, 0x19C srr1,
//   0x1A4 gqr[8], 0x1C8 psf[32]; total 0x2C8
static void hle_save_context_to_guest(CPUContext &ctx, uint32_t c) {
  for (int i = 0; i < 32; i++)
    ctx.mmu.write32(c + i * 4, ctx.gpr[i]);
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
  return true;
}


bool process_pending_callbacks(CPUContext &ctx) {
  // Sentinel return from a context-dispatched handler (blr with LR=0xFFFFFFFC).
  // Act like the tail of __OSDispatchInterrupt: re-install the context WE
  // saved at dispatch time and load it. Handlers may leave their own
  // exception context in __OSCurrentContext (the SDK dispatcher is the one
  // that puts the interrupted context back); trusting 0x800000D4 here used
  // to restore a never-saved stack context with srr0=0.
  if (ctx.pc == 0xFFFFFFFC && !ctx.in_callback) {
    if (ctx.dispatch_saved_ctx) {
      ctx.mmu.write32(0x800000D4, ctx.dispatch_saved_ctx);
      ctx.dispatch_saved_ctx = 0;
    }
    if (hle_load_context_from_guest(ctx)) {
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
                << " intmr=0x" << nwii::runtime::hw::pi_intmr
                << " intsr=0x" << nwii::runtime::hw::pi_intsr
                << " msr=0x" << ctx.msr
                << " dec_h=0x" << ctx.mmu.read32(0x80003000 + 8 * 4)
                << std::dec << "\n";
      // TEMP: dump the OS exception handler table (0x80003000, 16 slots)
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

  if (g_ipc_interrupt_delay > 0) {
      g_ipc_interrupt_delay--;
      if (g_ipc_interrupt_delay == 0) {
          nwii::runtime::hw::trigger_pi_interrupt(0x00004000);
      }
  }

  if (ctx.vblank_pending) {
      if (!ctx.in_callback && (ctx.msr & 0x8000)) {
          ctx.vblank_pending = false;
          hle_drive_thread_queue(ctx);
      }
  }


  if (ctx.in_callback && ctx.pc == 0xFFFFFFFC) {
    // Callback is finishing
    ctx.in_callback = false;
  }

  if (ctx.in_callback)
    return false;

  // Periodically raise VI (PI_INTSR bit 8 = 0x100, Dolphin INT_CAUSE_VI)
  // once the game unmasked it. Substitutes for real video timing.
  // Pre-VIInit wakeups are the decrementer's job, not VI's.
  if (ctx.inst_count % 500000 == 0 && (nwii::runtime::hw::pi_intmr & 0x100)) {
      nwii::runtime::hw::vi_trigger_interrupt();
  }

  // Decrementer exception emulation: OSAlarm/OSSetPeriodicAlarm depend on it.
  // Fire the registered DEC handler (exception 8, table at 0x80003000)
  // periodically so alarm-driven game loops keep ticking.
  if ((ctx.inst_count % 200000) == 0 && (ctx.msr & 0x8000) && !ctx.in_callback) {
      uint32_t dec_handler = ctx.mmu.read32(0x80003000 + 8 * 4);
      uint32_t current_ctx = ctx.mmu.read32(0x800000D4);
      if (dec_handler != 0 && dec_handler != 0xFFFFFFFF && current_ctx != 0) {
          static uint32_t dec_count = 0;
          std::cout << "[HLE DEC] Dispatch #" << std::dec << ++dec_count
                    << " handler=0x" << std::hex << dec_handler
                    << " r1=0x" << ctx.gpr[1] << " pc=0x" << ctx.pc
                    << " ctx=0x" << current_ctx << std::dec << "\n";
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
      int os_intr = -1;
      uint32_t bit_to_clear = 0;
      // PI_INTSR bits are Dolphin ProcessorInterface.h INT_CAUSE_*:
      //   0x04 DI, 0x08 SI, 0x10 EXI, 0x20 AI(stream), 0x40 DSP, 0x80 MEM,
      //   0x100 VI, 0x200 PE_TOKEN, 0x400 PE_FINISH, 0x800 CP, 0x4000 IPC.
      // __OSInterruptTable indices confirmed by observed game tables
      // (SHSM: IPC@27, EXI trio@9-11; NFS: SI@20, VI ISR@24):
      //   DSP_DSP=7 AI=8 EXI_0_EXI=9 CP=17 PE_TOKEN=18 PE_FINISH=19
      //   SI=20 DI=21 VI=24 ACR/IPC=27
      if      (active_ints & 0x00004000) { os_intr = 27; bit_to_clear = 0x00004000; } // IPC (Wii ACR)
      else if (active_ints & 0x00000100) { os_intr = 24; bit_to_clear = 0x00000100; } // VI
      else if (active_ints & 0x00000004) { os_intr = 21; bit_to_clear = 0x00000004; } // DI
      else if (active_ints & 0x00000008) { os_intr = 20; bit_to_clear = 0x00000008; } // SI
      else if (active_ints & 0x00000010) { os_intr =  9; bit_to_clear = 0x00000010; } // EXI 0
      else if (active_ints & 0x00000040) { os_intr =  7; bit_to_clear = 0x00000040; } // DSP
      else if (active_ints & 0x00000020) { os_intr =  8; bit_to_clear = 0x00000020; } // AI streaming
      else if (active_ints & 0x00000400) { os_intr = 19; bit_to_clear = 0x00000400; } // PE_FINISH
      else if (active_ints & 0x00000200) { os_intr = 18; bit_to_clear = 0x00000200; } // PE_TOKEN
      else if (active_ints & 0x00000800) { os_intr = 17; bit_to_clear = 0x00000800; } // CP

      if (os_intr != -1) {
          uint32_t handler = ctx.mmu.read32(0x80003040 + os_intr * 4);

          if (handler != 0 && handler != 0xFFFFFFFF) {
              // VI fires constantly; log only every 64th dispatch
              static uint32_t vi_dispatch_count = 0;
              if (true && vi_dispatch_count++ >= 0) {
                  std::cout << "[HLE PI] Dispatching interrupt " << std::dec << os_intr << " to handler 0x" << std::hex << handler
                            << " r1=0x" << ctx.gpr[1] << " pc=0x" << ctx.pc << std::dec << std::endl;
              }
              // The real 0x500 exception handler clears the PI interrupt cause BEFORE dispatching
              nwii::runtime::hw::pi_intsr &= ~bit_to_clear;

              uint32_t current_ctx = ctx.mmu.read32(0x800000D4);
              if (current_ctx != 0) {
                  // Save the interrupted state into __OSCurrentContext like the
                  // 0x500 exception vector does. The handler either returns via
                  // blr (LR sentinel -> we reload from the context) or context
                  // switches through the guest scheduler; both paths work.
                  hle_save_context_to_guest(ctx, current_ctx);
                  ctx.dispatch_saved_ctx = current_ctx;

                  ctx.srr0 = ctx.pc;
                  ctx.srr1 = ctx.msr;
                  ctx.msr &= ~0x8000; // EE=0 inside the handler

                  ctx.gpr[3] = os_intr;
                  ctx.gpr[4] = current_ctx;

                  // Run the handler on the interrupted stack with r1
                  // untouched, like the real 0x500 vector. Writing to
                  // [r1]/[r1+4] here would clobber the interrupted
                  // function's back chain and saved-LR slot, and handlers
                  // that exit via rfi never undo an r1 shift.
                  ctx.lr = 0xFFFFFFFC;
                  ctx.pc = handler;
                  longjmp(ctx.exception_jmp_buf, 1);
              }

              // Legacy path (thread system not initialised yet): backup-stack
              ctx.msr &= ~0x8000;
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
              bk.msr = ctx.msr | 0x8000; // Restore with EE enabled
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
              // Handler is NULL — OS is mid-reinit (cleared table, re-registering).
              // Leave pi_intsr set so interrupt stays pending.
              // Use a non-static counter so it resets each re-init window.
              static uint32_t last_null_pc = 0;
              static int null_warn_count = 0;
              if (ctx.pc != last_null_pc) {
                  null_warn_count = 0;
                  last_null_pc = ctx.pc;
              }
              if (null_warn_count++ < 3) {
                  std::cout << "[HLE PI] Handler for interrupt " << std::dec << os_intr
                            << " is NULL (OS reinit in progress, deferring)." << std::endl;
              }
          }
      }
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
