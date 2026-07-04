#include "runtime/platform/wii_platform.h"
#include "runtime/ios_kernel.h"
#include <iostream>

namespace nwii::runtime {
    extern std::string read_guest_string(CPUContext &ctx, uint32_t addr, int max_len = 256);
    extern bool valid_callback(uint32_t cb);
}

namespace nwii::runtime::platform {

// Helper functions that might be needed


static const int32_t IPC_OK = 0;

void WiiPlatform::ios_open(CPUContext& ctx) {
  uint32_t path_ptr = ctx.gpr[3];
  std::cout << "[HLE IOS] IOS_Open called with r3=0x" << std::hex << path_ptr
            << " r4=0x" << ctx.gpr[4] << " r5=0x" << ctx.gpr[5] << " r6=0x"
            << ctx.gpr[6] << " pc=0x" << ctx.pc << " lr=0x" << ctx.lr
            << std::dec << std::endl;
  std::string path = nwii::runtime::read_guest_string(ctx, path_ptr);

  if (path.empty()) {
    if (path_ptr == 0x41 || path_ptr == 2) {
      std::cout << "[HLE IOS] IOS_Open: Detected pointer error (0x" << std::hex
                << path_ptr << "). Faking /dev/di fd=2" << std::endl;
      ctx.gpr[3] = 2;
      ctx.pc = ctx.lr;
      return;
    }

    if (path_ptr != 0) {
      std::cout << "[HLE IOS] IOS_Open ERROR: Could not read path at 0x"
                << std::hex << path_ptr << std::dec << std::endl;
      ctx.gpr[3] = -106;
      ctx.pc = ctx.lr;
      return;
    }

    std::cout << "[HLE IOS] IOS_Open: empty path (ptr=0x" << std::hex
              << path_ptr << std::dec << "), faking failure with -106"
              << std::endl;
    ctx.gpr[3] = -106;
    ctx.pc = ctx.lr;
    return;
  }

  int32_t fd = IOSKernel::get().open(ctx, path, 0);

  std::cout << "[HLE IOS] IOS_Open: path='" << path << "' -> fd=" << fd << std::endl;
  ctx.gpr[3] = fd;
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_open_async(CPUContext& ctx) {
  std::string path = nwii::runtime::read_guest_string(ctx, ctx.gpr[3]);
  uint32_t callback = ctx.gpr[5];
  uint32_t userdata = ctx.gpr[6];

  if (path.empty()) {
    std::cout << "[HLE IOS] IOS_OpenAsync: empty path, faking failure"
              << std::endl;
    if (nwii::runtime::valid_callback(callback)) {
      ctx.queue_callback(callback, -106, userdata);
    }
    ctx.gpr[3] = -106;
    ctx.pc = ctx.lr;
    return;
  }

  int32_t fd = IOSKernel::get().open(ctx, path, 0);

  std::cout << "[HLE IOS] IOS_OpenAsync: path='" << path << "' -> fd=" << fd 
            << " cb=0x" << std::hex << callback << std::dec << std::endl;

  if (nwii::runtime::valid_callback(callback)) {
    ctx.queue_callback(callback, fd, userdata);
  }

  ctx.gpr[3] = IPC_OK;
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_close(CPUContext& ctx) {
  int32_t fd = (int32_t)ctx.gpr[3];
  int32_t result = IOSKernel::get().close(ctx, fd);

  std::cout << "[HLE IOS] IOS_Close: fd=" << fd << " -> result=" << result
            << std::endl;
  ctx.gpr[3] = result;
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_close_async(CPUContext& ctx) {
  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t callback = ctx.gpr[4];
  uint32_t userdata = ctx.gpr[5];

  int32_t result = IOSKernel::get().close(ctx, fd);

  std::cout << "[HLE IOS] IOS_CloseAsync: fd=" << fd << " cb=0x" << std::hex
            << callback << std::dec << std::endl;

  if (nwii::runtime::valid_callback(callback)) {
    ctx.queue_callback(callback, result, userdata);
  }
  ctx.gpr[3] = IPC_OK;
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_read(CPUContext& ctx) {
  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t buf = ctx.gpr[4];
  uint32_t len = ctx.gpr[5];

  int32_t result = IOSKernel::get().read(ctx, fd, buf, len);

  std::cout << "[HLE IOS] IOS_Read: fd=" << fd << " buf=0x" << std::hex << buf
            << " len=" << std::dec << len << " -> result=" << result
            << std::endl;
  ctx.gpr[3] = result;
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_read_async(CPUContext& ctx) {
  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t buf = ctx.gpr[4];
  uint32_t len = ctx.gpr[5];
  uint32_t callback = ctx.gpr[6];
  uint32_t userdata = ctx.gpr[7];

  int32_t result = IOSKernel::get().read(ctx, fd, buf, len);

  std::cout << "[HLE IOS] IOS_ReadAsync: fd=" << fd << " buf=0x" << std::hex
            << buf << " len=" << std::dec << len << " -> result=" << result
            << std::endl;

  if (nwii::runtime::valid_callback(callback)) {
    ctx.queue_callback(callback, result, userdata);
  }

  ctx.gpr[3] = IPC_OK;
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_write(CPUContext& ctx) {
  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t buf = ctx.gpr[4];
  uint32_t len = ctx.gpr[5];

  int32_t result = IOSKernel::get().write(ctx, fd, buf, len);
  ctx.gpr[3] = result;
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_write_async(CPUContext& ctx) {
  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t buf = ctx.gpr[4];
  uint32_t len = ctx.gpr[5];
  uint32_t callback = ctx.gpr[6];
  uint32_t userdata = ctx.gpr[7];

  int32_t result = IOSKernel::get().write(ctx, fd, buf, len);

  if (nwii::runtime::valid_callback(callback)) {
    ctx.queue_callback(callback, result, userdata);
  }

  ctx.gpr[3] = IPC_OK;
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_ioctl(CPUContext& ctx) {

  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t arg1 = ctx.gpr[4]; // cmd
  uint32_t arg2 = ctx.gpr[5]; // inbuf

  std::cout << "[HLE IOS] IOS_Ioctl: fd=" << fd << " cmd=0x" << std::hex << arg1
            << std::dec << std::endl;

  IpcRequest req{};
  req.fd = fd;
  req.ioctl_cmd = arg1;
  req.in_buf = arg2;
  req.in_size = ctx.gpr[6];
  req.out_buf = ctx.gpr[7];
  req.out_size = ctx.gpr[8];

  ctx.gpr[3] = IOSKernel::get().ioctl(ctx, req);
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_ioctl_async(CPUContext& ctx) {
  // Real IOS_IoctlAsync: fd=r3, cmd=r4, inbuf=r5, inlen=r6, outbuf=r7,
  // outlen=r8, cb=r9, ud=r10
  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t cmd = ctx.gpr[4];
  uint32_t inbuf = ctx.gpr[5];
  uint32_t callback = ctx.gpr[9];
  uint32_t userdata = ctx.gpr[10];

  std::cout << "[HLE IOS] IOS_IoctlAsync: PC=0x" << std::hex << ctx.pc << " LR=0x" << ctx.lr << " fd=" << std::dec << fd << " cmd=0x" << std::hex
            << cmd << " inbuf=0x" << inbuf << " inlen=0x" << ctx.gpr[6]
            << " outbuf=0x" << ctx.gpr[7] << " outlen=0x" << ctx.gpr[8]
            << " cb=0x" << callback << " ud=0x" << userdata << std::dec
            << std::endl;

  int32_t result = IPC_OK;
  IpcRequest req{};
  req.fd = fd;
  req.ioctl_cmd = cmd;
  req.in_buf = inbuf;
  req.in_size = ctx.gpr[6];
  req.out_buf = ctx.gpr[7];
  req.out_size = ctx.gpr[8];

  result = IOSKernel::get().ioctl(ctx, req);

  if (result != IPC_NO_REPLY && nwii::runtime::valid_callback(callback)) {
    ctx.queue_callback(callback, result, userdata);
  }

  ctx.gpr[3] = IPC_OK;
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_ioctlv(CPUContext& ctx) {
  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t cmd = ctx.gpr[4];
  std::cout << "[HLE IOS] IOS_Ioctlv: fd=" << fd << " cmd=0x" << std::hex << cmd
            << std::dec << std::endl;

  IpcRequest req{};
  req.fd = fd;
  req.ioctl_cmd = cmd;
  ctx.gpr[3] = IOSKernel::get().ioctlv(ctx, req);
  ctx.pc = ctx.lr;
}

void WiiPlatform::ios_ioctlv_async(CPUContext& ctx) {
  int32_t fd = (int32_t)ctx.gpr[3];
  uint32_t cmd = ctx.gpr[4];
  uint32_t callback = ctx.gpr[8];
  uint32_t userdata = ctx.gpr[9];

  std::cout << "[HLE IOS] IOS_IoctlvAsync: fd=" << fd << " cmd=0x" << std::hex
            << cmd << " cb=0x" << callback << std::dec << std::endl;

  int32_t result = IPC_OK;
  IpcRequest req{};
  req.fd = fd;
  req.ioctl_cmd = cmd;

  // We need to parse the vectors here, similar to ios_ioctlv, but using the HLE calling convention
  // Real IOS_IoctlvAsync: fd=r3, cmd=r4, incnt=r5, outcnt=r6, vecs=r7, cb=r8, ud=r9
  uint32_t arg_in = ctx.gpr[5];
  uint32_t arg_out = ctx.gpr[6];
  uint32_t arg_array = ctx.gpr[7];

  req.arg_cnt_in = arg_in;
  req.arg_cnt_out = arg_out;

  for (uint32_t i = 0; i < arg_in + arg_out; i++) {
      IoctlvVector vec;
      vec.addr = ctx.mmu.read32(arg_array + i * 8);
      vec.len = ctx.mmu.read32(arg_array + i * 8 + 4);
      req.ioctlv_vecs.push_back(vec);
  }

  result = IOSKernel::get().ioctlv(ctx, req);

  if (result != IPC_NO_REPLY && nwii::runtime::valid_callback(callback)) {
    ctx.queue_callback(callback, result, userdata);
  }

  ctx.gpr[3] = IPC_OK;
  ctx.pc = ctx.lr;
}

} // namespace nwii::runtime::platform
