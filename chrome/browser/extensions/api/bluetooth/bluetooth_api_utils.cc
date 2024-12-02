// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/bluetooth/bluetooth_api_utils.h"

#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/extensions/api/bluetooth.h"
#include "device/bluetooth/bluetooth_device.h"

namespace extensions {
namespace api {
namespace bluetooth {

// Fill in a Device object from a BluetoothDevice.
void BluetoothDeviceToApiDevice(const device::BluetoothDevice& device,
                                Device* out) {
  out->name = UTF16ToUTF8(device.GetName());
  out->address = device.address();
  out->paired = device.IsPaired();
  out->bonded = device.IsBonded();
  out->connected = device.IsConnected();
}

// The caller takes ownership of the returned pointer.
base::Value* BluetoothDeviceToValue(const device::BluetoothDevice& device) {
  extensions::api::bluetooth::Device api_device;
  BluetoothDeviceToApiDevice(device, &api_device);
  return api_device.ToValue().release();
}

}  // namespace bluetooth
}  // namespace api
}  // namespace extensions
