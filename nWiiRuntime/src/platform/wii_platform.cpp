#include "runtime/platform/wii_platform.h"
#include "runtime/ios_kernel.h"
#include <iostream>

namespace nwii::runtime::platform {

// Helper functions that might be needed
extern std::string read_guest_string(CPUContext &ctx, uint32_t addr);
extern bool valid_callback(uint32_t cb);
static const int32_t IPC_OK = 0;

void WiiPlatform::ios_open(CPUContext& ctx) {
  uint32_t path_ptr = ctx.gpr[3];
  std::cout << "[HLE IOS] IOS_Open called with r3=0x" << std::hex << path_ptr
            << " r4=0x" << ctx.gpr[4] << " r5=0x" << ctx.gpr[5] << " r6=0x"
            << ctx.gpr[6] << " pc=0x" << ctx.pc << " lr=0x" << ctx.lr
            << std::dec << std::endl;
  std::string path = read_guest_string(ctx, path_ptr);

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
  std::string path = read_guest_string(ctx, ctx.gpr[3]);
  uint32_t callback = ctx.gpr[5];
  uint32_t userdata = ctx.gpr[6];

  if (path.empty()) {
    std::cout << "[HLE IOS] IOS_OpenAsync: empty path, faking failure"
              << std::endl;
    if (valid_callback(callback)) {
      ctx.queue_callback(callback, -106, userdata);
    }
    ctx.gpr[3] = -106;
    ctx.pc = ctx.lr;
    return;
  }

  int32_t fd = IOSKernel::get().open(ctx, path, 0);

  std::cout << "[HLE IOS] IOS_OpenAsync: path='" << path << "' -> fd=" << fd 
            << " cb=0x" << std::hex << callback << std::dec << std::endl;

  if (valid_callback(callback)) {
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

  if (valid_callback(callback)) {
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

  if (valid_callback(callback)) {
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

  if (valid_callback(callback)) {
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
  req.out_buf = ctx.gpr[7];
  
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

  std::cout << "[HLE IOS] IOS_IoctlAsync: fd=" << fd << " cmd=0x" << std::hex
            << cmd << " inbuf=0x" << inbuf << " inlen=0x" << ctx.gpr[6]
            << " outbuf=0x" << ctx.gpr[7] << " outlen=0x" << ctx.gpr[8]
            << " cb=0x" << callback << " ud=0x" << userdata << std::dec
            << std::endl;

  int32_t result = IPC_OK;
  IpcRequest req{};
  req.fd = fd;
  req.ioctl_cmd = cmd;
  req.in_buf = inbuf;
  req.out_buf = ctx.gpr[7];
  
  result = IOSKernel::get().ioctl(ctx, req);

  // Do not fire callbacks for STM (eventhook/immediate) to prevent fake
  // Fire completion callback so game doesn't hang waiting for ioctl to finish
  if (valid_callback(callback)) {
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

  static uint16_t last_hci_opcode = 0;

  if (fd == 15) {
    uint32_t incnt = ctx.gpr[5];
    uint32_t outcnt = ctx.gpr[6];
    uint32_t vecs = ctx.gpr[7];

    if (cmd == 11) { // USB_SUBMIT_CTRL_URB
      if (incnt >= 2) {
        uint32_t vec_data_ptr = vecs + 8;
        uint32_t data_ptr = ctx.mmu.read32(vec_data_ptr);
        uint32_t data_len = ctx.mmu.read32(vec_data_ptr + 4);
        if (data_ptr != 0 && data_len >= 3) {
          uint8_t op_lsb = ctx.mmu.read8(data_ptr);
          uint8_t op_msb = ctx.mmu.read8(data_ptr + 1);
          last_hci_opcode = (op_msb << 8) | op_lsb;
          std::cout << "[HLE IOS] Bluetooth HCI Command received: 0x"
                    << std::hex << last_hci_opcode << std::dec << std::endl;
        }
      }
    } else if (cmd == 13) { // USB_SUBMIT_INTR_URB
      if (outcnt >= 1) {
        uint32_t vec_out_ptr = vecs + (incnt * 8);
        uint32_t out_ptr = ctx.mmu.read32(vec_out_ptr);
        uint32_t out_len = ctx.mmu.read32(vec_out_ptr + 4);
        if (out_ptr != 0 && out_len >= 6) {
          std::cout << "[HLE IOS] Responding to HCI Interrupt for opcode: 0x"
                    << std::hex << last_hci_opcode << std::dec << std::endl;
          ctx.mmu.write8(out_ptr + 0, 0x0E); // Command Complete
          ctx.mmu.write8(out_ptr + 2, 0x01); // Num HCI Command Packets
          ctx.mmu.write8(out_ptr + 3, last_hci_opcode & 0xFF);
          ctx.mmu.write8(out_ptr + 4, last_hci_opcode >> 8);
          ctx.mmu.write8(out_ptr + 5, 0x00); // Success

          if (last_hci_opcode == 0x1001) {        // Read Local Version
            ctx.mmu.write8(out_ptr + 1, 0x0C);    // Length
            ctx.mmu.write8(out_ptr + 6, 0x06);    // HCI Version
            ctx.mmu.write8(out_ptr + 7, 0x00);    // HCI Revision LSB
            ctx.mmu.write8(out_ptr + 8, 0x00);    // HCI Revision MSB
            ctx.mmu.write8(out_ptr + 9, 0x00);    // LMP Version
            ctx.mmu.write8(out_ptr + 10, 0x0F);   // Manufacturer LSB
            ctx.mmu.write8(out_ptr + 11, 0x00);   // Manufacturer MSB
            ctx.mmu.write8(out_ptr + 12, 0x00);   // LMP Subversion LSB
            ctx.mmu.write8(out_ptr + 13, 0x00);   // LMP Subversion MSB
          } else if (last_hci_opcode == 0x1009) { // Read BD ADDR
            ctx.mmu.write8(out_ptr + 1, 0x0A);    // Length
            ctx.mmu.write8(out_ptr + 6, 0x11);    // BD ADDR
            ctx.mmu.write8(out_ptr + 7, 0x22);
            ctx.mmu.write8(out_ptr + 8, 0x33);
            ctx.mmu.write8(out_ptr + 9, 0x44);
            ctx.mmu.write8(out_ptr + 10, 0x55);
            ctx.mmu.write8(out_ptr + 11, 0x66);
          } else {
            ctx.mmu.write8(out_ptr + 1, 0x04); // Length
          }
        }
      }
    }
  }

  // Fire completion callback so game doesn't hang waiting for ioctl to finish
  if (valid_callback(callback)) {
    ctx.queue_callback(callback, IPC_OK, userdata);
  }
  ctx.gpr[3] = IPC_OK;
  ctx.pc = ctx.lr;
}

} // namespace nwii::runtime::platform
