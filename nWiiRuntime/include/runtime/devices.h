#pragma once
#include "runtime/ios_device.h"
#include <memory>

namespace nwii::runtime::devices {

std::unique_ptr<IDevice> create_di_device();
std::unique_ptr<IDevice> create_fs_device();
std::unique_ptr<IDevice> create_stm_device();
std::unique_ptr<IDevice> create_usb_device();
std::unique_ptr<IDevice> create_es_device();

void register_all();

} // namespace nwii::runtime::devices
