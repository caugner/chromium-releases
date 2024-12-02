// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_USB_WEB_USB_PERMISSION_PROVIDER_H_
#define CHROME_BROWSER_USB_WEB_USB_PERMISSION_PROVIDER_H_

#include "device/devices_app/usb/public/interfaces/permission_provider.mojom.h"
#include "mojo/common/weak_binding_set.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/array.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"

namespace content {
class RenderFrameHost;
}

class WebUSBPermissionProvider : public device::usb::PermissionProvider {
 public:
  static void Create(
      content::RenderFrameHost* render_frame_host,
      mojo::InterfaceRequest<device::usb::PermissionProvider> request);

  ~WebUSBPermissionProvider() override;

 private:
  WebUSBPermissionProvider(content::RenderFrameHost* render_frame_host,
                           mojo::InterfaceRequest<PermissionProvider> request);

  // device::usb::PermissionProvider implementation.
  void HasDevicePermission(
      mojo::Array<device::usb::DeviceInfoPtr> requested_devices,
      const HasDevicePermissionCallback& callback) override;
  void HasConfigurationPermission(
      uint8_t requested_configuration,
      device::usb::DeviceInfoPtr device,
      const HasInterfacePermissionCallback& callback) override;
  void HasInterfacePermission(
      uint8_t requested_interface,
      uint8_t configuration_value,
      device::usb::DeviceInfoPtr device,
      const HasInterfacePermissionCallback& callback) override;
  void Bind(
      mojo::InterfaceRequest<device::usb::PermissionProvider> request) override;

  void OnConnectionError();

  mojo::WeakBindingSet<PermissionProvider> bindings_;
  content::RenderFrameHost* const render_frame_host_;
};

#endif  // CHROME_BROWSER_USB_WEB_USB_PERMISSION_PROVIDER_H_
