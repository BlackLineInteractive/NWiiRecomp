#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>
#include <memory>
#include <cstring>
#include <algorithm>

using namespace nwii::runtime;

// Forward declaration
extern "C" void run_game(nwii::runtime::CPUContext& ctx);

// Physical to logical address helper
static uint32_t phys_to_virt(uint32_t addr) {
    if (addr == 0) return 0;
    if (addr >= 0x80000000 && addr < 0x94000000) return addr;
    if (addr >= 0x00800000 && addr < 0x04000000) return addr | 0x90000000;
    if (addr < 0x00800000) return addr | 0x80000000;
    if (addr >= 0x10000000 && addr < 0x14000000) return addr | 0x90000000;
    return addr;
}

// Helper to read string from guest memory
static std::string read_guest_string(CPUContext &ctx, uint32_t addr,
                                     int max_len = 256) {
  std::string path;
  if (addr == 0) {
    std::cout << "[HLE IOS] read_guest_string: null address! pc=0x" << std::hex << ctx.pc << " lr=0x" << ctx.lr << std::dec << std::endl;
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
        std::cout << "[HLE IOS] read_guest_string: string at 0x" << std::hex << addr << " (vaddr 0x" << vaddr << ") is empty (first char is 0x" << (int)ctx.mmu.read8(vaddr) << ") pc=0x" << ctx.pc << " lr=0x" << ctx.lr << std::dec << std::endl;
    }
  } else {
    std::cout << "[HLE IOS] read_guest_string: invalid translated address 0x" << std::hex << vaddr << " (orig: 0x" << addr << ") pc=0x" << ctx.pc << " lr=0x" << ctx.lr << std::dec << std::endl;
  }
  return path;
}

// IPC error codes
static constexpr int32_t IPC_OK = 0;
static constexpr int32_t IPC_EIO = -8;
static constexpr int32_t ISFS_ERROR_ENOENT = -106;

static constexpr uint32_t IPC_RESPONSES_ADDR = 0x804B2A80;

static void ipc_init_queue(CPUContext &ctx) {
  ctx.mmu.write32(IPC_RESPONSES_ADDR + 0x00, 0);
  ctx.mmu.write32(IPC_RESPONSES_ADDR + 0x04, 0);
  ctx.mmu.write32(IPC_RESPONSES_ADDR + 0x08, 0);
  ctx.mmu.write32(IPC_RESPONSES_ADDR + 0x0C, 0);
  for (int i = 0; i < 16; ++i)
    ctx.mmu.write32(IPC_RESPONSES_ADDR + 0x10 + i * 4, 0);
}

static void ipc_mark_sent(CPUContext &ctx) {
  uint32_t cnt_sent = ctx.mmu.read32(IPC_RESPONSES_ADDR + 0x00);
  ctx.mmu.write32(IPC_RESPONSES_ADDR + 0x00, cnt_sent + 1);
}

// Fixed IPC buffer pool - 32 slots of 64 bytes each, never reused to avoid
// overlap with file path strings (#5 IPC corruption fix)
static constexpr uint32_t IPC_POOL_BASE  = 0x804B0000;
static constexpr uint32_t IPC_SLOT_SIZE  = 128; // bytes per slot
static constexpr uint32_t IPC_POOL_SLOTS = 64;
static uint32_t ipc_pool_next = 0; // round-robin index

// Bump allocator for guest IPC/heap memory
// Use TWO separate regions to avoid IPC struct corruption by path strings (#5):
//   ios_guest_heap_ptr  = 0x80490000  → misc/NAND/FS allocs (heap=1,2)
//   ios_ipc_heap_ptr    = 0x804A0000  → IPC request buffers  (heap=0, size<=64)
static uint32_t ios_guest_heap_ptr = 0x80490000;
static uint32_t ios_ipc_heap_ptr   = 0x804A0000;

static uint32_t ios_guest_alloc(CPUContext &ctx, uint32_t size, uint32_t align, int heap_id = 0) {
  if (align < 32) align = 32;
  // Route small IPC request buffers (heap=0, size=64) to isolated region
  uint32_t &heap = (heap_id == 0 && size <= 128) ? ios_ipc_heap_ptr : ios_guest_heap_ptr;
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

static int get_device_fd(const std::string &path) {
  if (path == "/dev/di") return 2;
  if (path == "/dev/fs") return 3;
  if (path == "/dev/stm/immediate") return 4;
  if (path == "/dev/stm/eventhook") return 5;
  if (path == "/dev/es") return 9;
  // /dev/usb/oh1/* is Wii Remote Bluetooth HID - not implemented.
  // Return ENOENT so the SDK falls back to GameCube PAD.
  if (path.find("/dev/usb") == 0) return ISFS_ERROR_ENOENT;
  return -6; // Reject network, etc.
}

static bool valid_callback(uint32_t cb) {
  return cb != 0 && cb != 0xFFFFFFFFu && cb >= 0x80000000u && cb < 0x82000000u;
}

// ── Virtual NAND filesystem (#4 fix) ────────────────────────────────────────
// Provides minimal system files so SDK doesn't endlessly retry missing files.

struct VNandFile {
  std::string path;
  std::vector<uint8_t> data;
};

static std::vector<VNandFile> g_vnand_files;
static bool g_vnand_init = false;

// Build a minimal but structurally valid SYSCONF blob (64KB max, padded with 0xFF).
// Format: magic "SCv15" + version + item count (LE16) + item array.
// We emit just one dummy item so the SDK doesn't trip over malformed data.
static std::vector<uint8_t> make_sysconf() {
  std::vector<uint8_t> sc(0x4000, 0xFF); // 16 KB, fill with 0xFF (NAND erase)
  // Header
  sc[0]='S'; sc[1]='C'; sc[2]='v'; sc[3]='0'; // SCv0 magic (4 bytes)
  sc[4] = 1;   // version
  sc[5] = 0; sc[6] = 1; // numItems = 1 (big-endian)
  
  // One item: key="IPL.AR", value="NTSC"
  // Byte 0: type(3 bits) | name_len - 1 (5 bits)
  // type 2 = SmallArray. name_len = 6 ("IPL.AR"). So 0x40 | 5 = 0x45.
  // Wait, type 2 is 010. So 2 << 5 = 0x40. length 6. 6-1=5. 0x40 | 5 = 0x45.
  int off = 8; // data starts at offset 8 (pad byte at 7?) No, SCv0 + ver + numItems = 7 bytes.
  // Actually, item offsets in SYSCONF:
  // 0-3: SCv0
  // 4: version (1)
  // 5-6: numItems (1)
  off = 7;
  sc[off++] = 0x45; // Type 2 (SmallArray), Length 6
  sc[off++] = 'I'; sc[off++] = 'P'; sc[off++] = 'L'; sc[off++] = '.'; sc[off++] = 'A'; sc[off++] = 'R';
  // SmallArray length byte: total array length - 1
  // "NTSC" is 4 bytes. 4 - 1 = 3.
  sc[off++] = 0x03;
  sc[off++] = 'N'; sc[off++] = 'T'; sc[off++] = 'S'; sc[off++] = 'C';
  return sc;
}

// setting.txt for Wii System Settings (text key=value pairs)
static std::vector<uint8_t> make_setting_txt() {
  const char* txt =
    "AREA=USA\r\n"
    "MODEL=RVL-001\r\n"
    "DVD=0\r\n"
    "MPCH=0x7FFE\r\n"
    "CODE=0\r\n"
    "SERNO=123456789\r\n"
    "VIDEO=NTSC\r\n"
    "GAME=US\r\n";
  return std::vector<uint8_t>(txt, txt + strlen(txt));
}

static void vnand_init() {
  if (g_vnand_init) return;
  g_vnand_init = true;
  g_vnand_files.push_back({"/shared2/sys/SYSCONF",                   make_sysconf()});
  g_vnand_files.push_back({"/title/00000001/00000002/data/setting.txt", make_setting_txt()});
  // Provide an empty play_rec.dat so DVDLow stats don't fail
  g_vnand_files.push_back({"/title/00000001/00000002/data/play_rec.dat", std::vector<uint8_t>(0x20, 0)});
  // dvderror.dat - empty is fine
  g_vnand_files.push_back({"/shared2/test2/dvderror.dat", std::vector<uint8_t>(4, 0)});
}

// fd 20..27 = virtual NAND file slots
static constexpr int VNAND_FD_BASE = 20;
static constexpr int VNAND_FD_MAX  = 8;
struct VNandHandle { int file_idx = -1; uint32_t pos = 0; bool open = false; };
static VNandHandle g_vnand_handles[VNAND_FD_MAX];

static int vnand_open(const std::string& path) {
  vnand_init();
  for (int i = 0; i < (int)g_vnand_files.size(); ++i) {
    if (g_vnand_files[i].path == path) {
      // Find a free handle slot
      for (int s = 0; s < VNAND_FD_MAX; ++s) {
        if (!g_vnand_handles[s].open) {
          g_vnand_handles[s] = {i, 0, true};
          return VNAND_FD_BASE + s;
        }
      }
      return ISFS_ERROR_ENOENT; // no free slots
    }
  }
  return ISFS_ERROR_ENOENT;
}
// ─────────────────────────────────────────────────────────────────────────────


// Disc reader
static std::shared_ptr<std::ifstream> g_disc_file;
static bool g_disc_is_wbfs = false;
static uint32_t g_wbfs_block_size = 0;
static std::vector<uint16_t> g_wbfs_bat;

static bool init_wbfs(const std::filesystem::path& path) {
    auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
    if (!file->is_open()) return false;
    
    char magic[4];
    file->read(magic, 4);
    if (magic[0] != 'W' || magic[1] != 'B' || magic[2] != 'F' || magic[3] != 'S') {
        return false;
    }
    
    uint8_t shift;
    file->read(reinterpret_cast<char*>(&shift), 1);
    g_wbfs_block_size = 1u << (shift + 17);
    
    file->seekg(0, std::ios::end);
    size_t file_size = file->tellg();
    file->seekg(0x100, std::ios::beg);
    
    size_t num_entries = (file_size - 0x100) / 2;
    if (num_entries > 65536) num_entries = 65536;
    
    g_wbfs_bat.resize(num_entries);
    for (size_t i = 0; i < num_entries; ++i) {
        uint16_t entry;
        file->read(reinterpret_cast<char*>(&entry), 2);
        g_wbfs_bat[i] = ((entry >> 8) & 0xFF) | ((entry << 8) & 0xFF00);
    }
    
    g_disc_file = file;
    g_disc_is_wbfs = true;
    std::cout << "[HLE IOS] Opened WBFS: " << path << " block_size=" << g_wbfs_block_size << std::endl;
    return true;
}

static void init_iso(const std::filesystem::path& path) {
    g_disc_file = std::make_shared<std::ifstream>(path, std::ios::binary);
    if (g_disc_file->is_open()) {
        g_disc_is_wbfs = false;
        std::cout << "[HLE IOS] Opened ISO: " << path << std::endl;
    }
}

static std::shared_ptr<std::ifstream> get_disc_file() {
    if (!g_disc_file) {
        auto& cfg = Config::get();
        std::filesystem::path game_dir(cfg.game_dir);
        std::filesystem::path p;
        
        p = game_dir / "disc.iso";
        if (std::filesystem::exists(p)) { init_iso(p); return g_disc_file; }
        p = game_dir / "game.iso";
        if (std::filesystem::exists(p)) { init_iso(p); return g_disc_file; }
        p = game_dir.parent_path() / "game.iso";
        if (std::filesystem::exists(p)) { init_iso(p); return g_disc_file; }
        p = game_dir.parent_path() / "disc.iso";
        if (std::filesystem::exists(p)) { init_iso(p); return g_disc_file; }
        
        if (std::filesystem::exists(game_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(game_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".wbfs") {
                    if (init_wbfs(entry.path())) return g_disc_file;
                }
            }
        }
        if (std::filesystem::exists(game_dir.parent_path())) {
            for (const auto& entry : std::filesystem::directory_iterator(game_dir.parent_path())) {
                if (entry.is_regular_file() && entry.path().extension() == ".wbfs") {
                    if (init_wbfs(entry.path())) return g_disc_file;
                }
            }
        }
    }
    return g_disc_file;
}

static void read_disc_data(uint8_t* dst, uint64_t offset, uint32_t length) {
    auto file = get_disc_file();
    if (!file || !file->is_open()) {
        std::memset(dst, 0, length);
        return;
    }
    
    if (!g_disc_is_wbfs) {
        file->seekg(offset);
        file->read(reinterpret_cast<char*>(dst), length);
    } else {
        uint32_t done = 0;
        while (done < length) {
            uint64_t block_idx = (offset + done) / g_wbfs_block_size;
            uint32_t block_off = (offset + done) % g_wbfs_block_size;
            uint32_t chunk = std::min<uint32_t>(length - done, g_wbfs_block_size - block_off);
            
            if (block_idx < g_wbfs_bat.size() && g_wbfs_bat[block_idx] != 0) {
                uint64_t phys = (uint64_t)g_wbfs_bat[block_idx] * g_wbfs_block_size + block_off;
                file->seekg(phys);
                file->read(reinterpret_cast<char*>(dst + done), chunk);
            } else {
                std::memset(dst + done, 0, chunk);
            }
            done += chunk;
        }
    }
}

// Isolated callback execution - saves ALL non-volatile state including FPR/GQR (#9)
void execute_callback(CPUContext &ctx, uint32_t callback, uint32_t arg1, uint32_t arg2) {
  if (callback == 0 || callback == 0xFFFFFFFF) return;

  uint32_t b_gpr[32], b_lr = ctx.lr, b_ctr = ctx.ctr, b_xer = ctx.xer, b_pc = ctx.pc;
  uint32_t b_msr = ctx.msr, b_fpscr = ctx.fpscr;
  ConditionField b_cr[8];
  double b_fpr[32], b_ps1[32];
  uint32_t b_gqr[8];
  for (int i = 0; i < 32; i++) b_gpr[i] = ctx.gpr[i];
  for (int i = 0; i < 8;  i++) b_cr[i]  = ctx.cr[i];
  for (int i = 0; i < 32; i++) { b_fpr[i] = ctx.fpr[i]; b_ps1[i] = ctx.ps1[i]; }
  for (int i = 0; i < 8;  i++) b_gqr[i] = ctx.gqr[i];

  ctx.gpr[3] = arg1;
  ctx.gpr[4] = arg2;
  ctx.lr = 0;
  ctx.pc = callback;

  run_game(ctx);

  for (int i = 0; i < 32; i++) ctx.gpr[i] = b_gpr[i];
  for (int i = 0; i < 8;  i++) ctx.cr[i]  = b_cr[i];
  for (int i = 0; i < 32; i++) { ctx.fpr[i] = b_fpr[i]; ctx.ps1[i] = b_ps1[i]; }
  for (int i = 0; i < 8;  i++) ctx.gqr[i] = b_gqr[i];
  ctx.lr    = b_lr;
  ctx.ctr   = b_ctr;
  ctx.xer   = b_xer;
  ctx.fpscr = b_fpscr;
  ctx.msr   = b_msr;
  ctx.pc    = b_pc;
}

// NEW: Proper DI_Read that handles both sync and async modes
static void di_read_internal(CPUContext &ctx, uint32_t inbuf, uint32_t callback, uint32_t userdata, bool is_async) {
    // Read DICommand structure
    uint32_t cmd = 0, transferSize = 0, addr = 0, length = 0;
    uint64_t offset = 0;
    
    uint32_t ipc_ptr = phys_to_virt(inbuf);
    
    if (ipc_ptr >= 0x80000000 && ipc_ptr < 0x94000000) {
        cmd = ctx.mmu.read32(ipc_ptr + 0x00);
        transferSize = ctx.mmu.read32(ipc_ptr + 0x04);
        addr = ctx.mmu.read32(ipc_ptr + 0x08);
        // callback at 0x0C - already read by caller
        offset = ((uint64_t)ctx.mmu.read32(ipc_ptr + 0x10) << 32) | ctx.mmu.read32(ipc_ptr + 0x14);
        length = ctx.mmu.read32(ipc_ptr + 0x18);
    }
    
    std::cout << "[HLE IOS] DI_Read" << (is_async ? "Async" : "") << ": inbuf=0x" << std::hex << inbuf
              << " cmd=0x" << cmd << " transferSize=" << std::dec << transferSize
              << " addr=0x" << std::hex << addr << " cb=0x" << callback
              << " offset=0x" << offset << " length=" << std::dec << length << std::endl;
    
    // Use defaults if structure not initialized
    bool use_defaults = (cmd != 0x20) || (offset > 0x1FFFFFFFFFFULL);
    
    if (use_defaults) {
        std::cout << "[HLE IOS] DI_Read: Using defaults (invalid structure)" << std::endl;
        cmd = 0x20;
        transferSize = 2;
        offset = 0;
        length = 0x440;
    }
    
    uint32_t buf_ptr = phys_to_virt(addr);
    uint32_t total_length = transferSize * 2048;
    if (length > 0 && length < total_length) total_length = length;
    if (total_length == 0 && length > 0) total_length = length;
    if (total_length == 0) total_length = 0x440;
    
    std::cout << "[HLE IOS] DI_Read final: offset=0x" << std::hex << (uint32_t)offset
              << " total_length=" << std::dec << total_length
              << " buf_ptr=0x" << std::hex << buf_ptr << std::dec << std::endl;
    
    auto file = get_disc_file();
    bool success = false;
    uint32_t bytes_read = 0;
    
    if (file && file->is_open() && buf_ptr != 0 && total_length > 0) {
        std::vector<uint8_t> temp(total_length);
        read_disc_data(temp.data(), (uint32_t)offset, total_length);
        bytes_read = total_length;
        if (!g_disc_is_wbfs) bytes_read = file->gcount();
        
        for (uint32_t i = 0; i < bytes_read; i++) {
            ctx.mmu.write8(buf_ptr + i, temp[i]);
        }
        
        std::cout << "[HLE IOS] DI_Read: Read " << bytes_read << " bytes" << std::endl;
        success = true;
    } else {
        if (buf_ptr != 0 && offset == 0) {
            for (uint32_t i = 0; i < total_length; i++) ctx.mmu.write8(buf_ptr + i, 0);
            // Write proper disc header for SHSM (RSZK)
            ctx.mmu.write8(buf_ptr + 0, 'R');
            ctx.mmu.write8(buf_ptr + 1, 'S');
            ctx.mmu.write8(buf_ptr + 2, 'Z');
            ctx.mmu.write8(buf_ptr + 3, 'K');
            ctx.mmu.write32(buf_ptr + 0x18, 0x5D1C9EA3); // Wii disc magic
            std::cout << "[HLE IOS] DI_Read: Faked disc header (RSZK)" << std::endl;
            success = true;
            bytes_read = total_length;
        }
    }
    
    // Update DVDFileInfo state if inbuf points to one
    // DVDFileInfo has DVDCommandBlock at offset 0
    // state at +0x0C, transferredSize at +0x20
    if (ipc_ptr >= 0x80000000 && ipc_ptr < 0x94000000) {
        ctx.mmu.write32(ipc_ptr + 0x0C, 0); // state = ready
        ctx.mmu.write32(ipc_ptr + 0x20, bytes_read); // transferredSize
    }
    
    // For async mode, the callback will be triggered by IOS_IoctlAsync via HW IPC.
    ctx.gpr[3] = success ? 1 : 0;
}

extern "C" {
void IOS_Open(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
    return;
  }
  uint32_t path_ptr = ctx.gpr[3];
  std::cout << "[HLE IOS] IOS_Open called with r3=0x" << std::hex << path_ptr << " r4=0x" << ctx.gpr[4] << " r5=0x" << ctx.gpr[5] << " r6=0x" << ctx.gpr[6] << " pc=0x" << ctx.pc << " lr=0x" << ctx.lr << std::dec << std::endl;
  std::string path = read_guest_string(ctx, path_ptr);
  
  if (path.empty()) {
    if (path_ptr == 0x41 || path_ptr == 2) {
        std::cout << "[HLE IOS] IOS_Open: Detected pointer error (0x" << std::hex << path_ptr << "). Faking /dev/di fd=2" << std::endl;
        ctx.gpr[3] = 2;
        ctx.pc = ctx.lr;
        return;
    }
    
    if (path_ptr != 0) {
        std::cout << "[HLE IOS] IOS_Open ERROR: Could not read path at 0x" << std::hex << path_ptr << std::dec << std::endl;
        ctx.gpr[3] = -106;
        ctx.pc = ctx.lr;
        return;
    }
    
    std::cout << "[HLE IOS] IOS_Open: empty path (ptr=0x" << std::hex << path_ptr << std::dec << "), faking failure with -106" << std::endl;
    ctx.gpr[3] = -106;
    ctx.pc = ctx.lr;
    return;
  }
  
  int fd_or_error;
  if (path.find("/dev/") == 0) {
    if (path.find("/dev/usb/") == 0) {
      fd_or_error = 10; // Fake FD for USB devices
      std::cout << "[HLE IOS] IOS_Open: faking /dev/usb -> fd=10" << std::endl;
    } else {
      fd_or_error = get_device_fd(path);
    }
  } else {
    fd_or_error = ISFS_ERROR_ENOENT;
  }
  
  std::cout << "[HLE IOS] IOS_Open: path='" << path << "' -> result=" << fd_or_error << std::endl;
  ctx.gpr[3] = fd_or_error;
  ctx.pc = ctx.lr;
}

void IOS_OpenAsync(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
    return;
  }
  std::string path = read_guest_string(ctx, ctx.gpr[3]);
  uint32_t callback = ctx.gpr[5];
  uint32_t userdata = ctx.gpr[6];

  if (path.empty()) {
    std::cout << "[HLE IOS] IOS_OpenAsync: empty path, faking failure" << std::endl;
    if (valid_callback(callback)) {
      execute_callback(ctx, callback, -106, userdata);
    }
    ctx.gpr[3] = -106;
    ctx.pc = ctx.lr;
    return;
  }

  int fd_or_error;
  if (path.find("/dev/") == 0) {
    if (path.find("/dev/usb/") == 0) {
      fd_or_error = 10; // Fake FD for USB devices
      std::cout << "[HLE IOS] IOS_Open: faking /dev/usb -> fd=10" << std::endl;
    } else {
      fd_or_error = get_device_fd(path);
    }
  } else {
    fd_or_error = ISFS_ERROR_ENOENT;
  }

  std::cout << "[HLE IOS] IOS_OpenAsync: path='" << path << "' -> result=" << fd_or_error
            << " cb=0x" << std::hex << callback << std::dec << std::endl;

  if (valid_callback(callback)) {
    execute_callback(ctx, callback, fd_or_error, userdata);
  }

  ctx.gpr[3] = fd_or_error;
  ctx.pc = ctx.lr;
}

void IOS_Close(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }
  std::cout << "[HLE IOS] IOS_Close: fd=" << ctx.gpr[3] << std::endl;
  ctx.gpr[3] = 0;
  ctx.pc = ctx.lr;
}

void IOS_Read(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }
  std::cout << "[HLE IOS] IOS_Read: fd=" << ctx.gpr[3] << " buf=0x" << std::hex
            << ctx.gpr[4] << " len=" << std::dec << ctx.gpr[5] << std::endl;
  ctx.gpr[3] = ctx.gpr[5];
  ctx.pc = ctx.lr;
}

void IOS_Write(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }
  ctx.gpr[3] = ctx.gpr[5];
  ctx.pc = ctx.lr;
}

void IOS_Ioctl(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }

  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t arg1 = ctx.gpr[4]; // cmd
  uint32_t arg2 = ctx.gpr[5]; // inbuf

  std::cout << "[HLE IOS] IOS_Ioctl: fd=" << fd << " cmd=0x" << std::hex << arg1 << std::dec << std::endl;
  
  if (fd == 2) {
    // /dev/di ioctls
    if (arg1 == 0x8E) { // DI_ReadDiskID
      uint32_t buf = arg2;
      if (buf != 0 && buf < 0x80000000) {
        if (buf < 0x01800000 || (buf >= 0x10000000 && buf < 0x14000000))
          buf |= 0x80000000;
      }
      if (buf >= 0x80000000 && buf < 0x94000000) {
        for (int i = 0; i < 32; i++) ctx.mmu.write8(buf + i, 0);
        // SHSM = RSZK (Silent Hill: Shattered Memories)
        ctx.mmu.write8(buf + 0, 'R');
        ctx.mmu.write8(buf + 1, 'S');
        ctx.mmu.write8(buf + 2, 'Z');
        ctx.mmu.write8(buf + 3, 'K');
        ctx.mmu.write8(buf + 4, '0'); // disc number
        ctx.mmu.write8(buf + 5, '1'); // version
        ctx.mmu.write8(buf + 6, '0');
        ctx.mmu.write8(buf + 7, '1');
        ctx.mmu.write32(buf + 0x18, 0x5D1C9EA3); // Wii disc magic
        std::cout << "[HLE IOS] DI_ReadDiskID: wrote RSZK disc ID to 0x" << std::hex << buf << std::dec << std::endl;
      }
      ctx.gpr[3] = 1;
    } else if (arg1 == 0x80) { // DI_Reset
      std::cout << "[HLE IOS] DI_Reset acknowledged" << std::endl;
      ctx.gpr[3] = 1;
    } else if (arg1 == 0x20) { // DI_Read - SYNC version
      uint32_t inbuf = arg2;
      if (inbuf != 0 && inbuf < 0x80000000) {
        if (inbuf < 0x01800000 || (inbuf >= 0x10000000 && inbuf < 0x14000000))
          inbuf |= 0x80000000;
      }
      // For sync ioctl, callback is in the DICommand structure at +0x0C
      uint32_t callback = 0;
      if (inbuf >= 0x80000000 && inbuf < 0x94000000) {
        callback = ctx.mmu.read32(inbuf + 0x0C);
      }
      di_read_internal(ctx, inbuf, callback, 0, false); // sync = no callback invocation
      ctx.pc = ctx.lr;
      return;
    } else if (arg1 == 0xE0) { // DVDGetCoverRegister
      ctx.gpr[3] = 1; // Cover closed, disc present
    } else if (arg1 == 0x60) { // DVDGetError / StopMotor
      ctx.gpr[3] = 0; // No error
    } else {
      std::cout << "[HLE IOS] Unhandled /dev/di ioctl: 0x" << std::hex << arg1 << std::dec << std::endl;
      ctx.gpr[3] = 1;
    }
  } else if (fd == 15) { // Fake USB
    std::cout << "[HLE IOS] Fake USB ioctl: 0x" << std::hex << arg1 << std::dec << std::endl;
    ctx.gpr[3] = 1;
  } else if (fd >= 10 && fd <= 12) { // USB HID
      ctx.gpr[3] = IPC_OK;
  } else {
    ctx.gpr[3] = IPC_OK;
  }
  ctx.pc = ctx.lr;
}

void IOS_Ioctlv(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }
  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t cmd = ctx.gpr[4];
  std::cout << "[HLE IOS] IOS_Ioctlv: fd=" << fd << " cmd=0x" << std::hex << cmd << std::dec << std::endl;
  
  if (fd == 2) {
    ctx.gpr[3] = 1;
  } else {
    ctx.gpr[3] = 0;
  }
  ctx.pc = ctx.lr;
}

// iosFree: frees a block from the IOS heap. Hook this at the real iosFree address.
void iosFree(CPUContext &ctx) {
  uint32_t heap_id = ctx.gpr[3];
  uint32_t ptr = ctx.gpr[4];
  std::cout << "[HLE IOS] iosFree: heap=" << heap_id
            << " ptr=0x" << std::hex << ptr << std::dec << std::endl;
  ctx.gpr[3] = 0; // IPC_OK
  ctx.pc = ctx.lr;
}

void IOS_IoctlAsync(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
    return;
  }
  // Real IOS_IoctlAsync: fd=r3, cmd=r4, inbuf=r5, inlen=r6, outbuf=r7, outlen=r8, cb=r9, ud=r10
  int32_t fd   = (int32_t)ctx.gpr[3];
  uint32_t cmd = ctx.gpr[4];
  uint32_t inbuf   = ctx.gpr[5];
  uint32_t callback = ctx.gpr[9];
  uint32_t userdata = ctx.gpr[10];

  std::cout << "[HLE IOS] IOS_IoctlAsync: fd=" << fd << " cmd=0x" << std::hex << cmd
            << " inbuf=0x" << inbuf << " cb=0x" << callback << std::dec << std::endl;

  int32_t result = IPC_OK;
  if (fd == 2) {
    // /dev/di async ioctls
    if (cmd == 0x86) { // DI_ClearCoverInterrupt
      std::cout << "[HLE IOS] IOS_IoctlAsync -> DI_ClearCoverInterrupt" << std::endl;
      result = IPC_OK;
    } else if (cmd == 0x12) { // DI_Inquiry
      std::cout << "[HLE IOS] IOS_IoctlAsync -> DI_Inquiry" << std::endl;
      result = IPC_OK;
    } else if (cmd == 0x20) { // DI_ReadAsync
      di_read_internal(ctx, inbuf, callback, userdata, true);
      if (valid_callback(callback))
        execute_callback(ctx, callback, ctx.gpr[3], userdata);
      ctx.gpr[3] = IPC_OK;
      ctx.pc = ctx.lr;
      return;
    } else {
      result = IPC_OK;
    }
  } else {
    result = IPC_OK;
  }

  if (valid_callback(callback))
    execute_callback(ctx, callback, result, userdata);

  ctx.gpr[3] = IPC_OK;
  ctx.pc = ctx.lr;
}


void IOS_IoctlvAsync(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }

  uint32_t callback = ctx.gpr[8];
  uint32_t userdata = ctx.gpr[9];

  if (callback < 0x80000000 || callback >= 0x82000000) {
    if (ctx.gpr[5] >= 0x80000000 && ctx.gpr[5] < 0x82000000) {
      callback = ctx.gpr[5];
      userdata = ctx.gpr[6];
    }
  }

  if (valid_callback(callback)) {
    execute_callback(ctx, callback, 0, userdata);
  }

  ctx.gpr[3] = 0;
  ctx.pc = ctx.lr;
}

void IOS_Seek(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }
  ctx.gpr[3] = 0;
  ctx.pc = ctx.lr;
}

void IOS_CloseAsync(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }
  uint32_t callback = ctx.gpr[4];
  uint32_t userdata = ctx.gpr[5];

  if (valid_callback(callback)) {
    execute_callback(ctx, callback, 0, userdata);
  }

  ctx.gpr[3] = 0;
  ctx.pc = ctx.lr;
}

void IOS_ReadAsync(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }
  uint32_t callback = ctx.gpr[6];
  uint32_t userdata = ctx.gpr[7];

  if (valid_callback(callback)) {
    execute_callback(ctx, callback, ctx.gpr[5], userdata);
  }

  ctx.gpr[3] = ctx.gpr[5];
  ctx.pc = ctx.lr;
}

void IOS_WriteAsync(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }
  uint32_t callback = ctx.gpr[6];
  uint32_t userdata = ctx.gpr[7];

  if (valid_callback(callback)) {
    execute_callback(ctx, callback, ctx.gpr[5], userdata);
  }

  ctx.gpr[3] = ctx.gpr[5];
  ctx.pc = ctx.lr;
}

void IOS_SeekAsync(CPUContext &ctx) {
  if (Config::get().platform == Platform::GameCube) {
    ctx.gpr[3] = -1;
    return;
  }
  uint32_t callback = ctx.gpr[6];
  uint32_t userdata = ctx.gpr[7];

  if (valid_callback(callback)) {
    execute_callback(ctx, callback, 0, userdata);
  }

  ctx.gpr[3] = 0;
  ctx.pc = ctx.lr;
}
} // extern "C"

namespace nwii {
namespace runtime {

void init_ipc_client(CPUContext &ctx) {
  ipc_init_queue(ctx);
  ctx.mmu.write32(0x804BA440, 0xFFFFFFFFu); // di fd, unset
  ctx.mmu.write32(0x804BA450, 0);           // mailbox ack counter
  ctx.mmu.write32(0x804BA454, 0);           // ios heap id
}

void handle_syscall(CPUContext &ctx) {
  uint32_t syscall_id = ctx.gpr[0];

  if (syscall_id == 0x61) {
    std::string path = read_guest_string(ctx, ctx.gpr[3]);
    if (path.empty()) {
      std::cout << "[HLE IOS] IOS_Open via sc: empty path, faking failure" << std::endl;
      ctx.gpr[3] = -106;
      return;
    }
    int fd_or_error;
    if (path.find("/dev/") == 0) {
      fd_or_error = get_device_fd(path);
    } else {
      fd_or_error = ISFS_ERROR_ENOENT;
    }
    std::cout << "[HLE IOS] IOS_Open via sc, path='" << path << "' -> result=" << fd_or_error << std::endl;
    ctx.gpr[3] = fd_or_error;

  } else if (syscall_id == 0x62) {
    std::cout << "[HLE IOS] IOS_Close via sc, fd=" << ctx.gpr[3] << std::endl;
    ctx.gpr[3] = 0;

  } else if (syscall_id == 0x6B || syscall_id == 0x6C) {
    std::cout << "[HLE IOS] IOS_Ioctl(v)Async via sc: " << std::hex
              << syscall_id << " fd=" << ctx.gpr[3] << " cmd=" << ctx.gpr[4]
              << std::dec << std::endl;
    uint32_t callback = (syscall_id == 0x6B) ? ctx.gpr[9] : ctx.gpr[8];
    uint32_t userdata = (syscall_id == 0x6B) ? ctx.gpr[10] : ctx.gpr[9];

    if (valid_callback(callback)) {
      execute_callback(ctx, callback, 0, userdata);
    }

    ctx.gpr[3] = 0;
    ctx.pc = ctx.lr;

  } else {
    // Unknown syscall - don't corrupt registers
  }
}

} // namespace runtime
} // namespace nwii
