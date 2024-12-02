// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper/content_renderer_pepper_host_factory.h"

#include "base/logging.h"
#include "content/renderer/pepper/pepper_file_chooser_host.h"
#include "content/renderer/pepper/pepper_flash_host.h"
#include "content/renderer/pepper/pepper_websocket_host.h"
#include "content/renderer/pepper/renderer_ppapi_host_impl.h"
#include "ppapi/host/resource_host.h"
#include "ppapi/proxy/ppapi_messages.h"

using ppapi::host::ResourceHost;

namespace content {

ContentRendererPepperHostFactory::ContentRendererPepperHostFactory(
    RendererPpapiHostImpl* host)
    : host_(host) {
}

ContentRendererPepperHostFactory::~ContentRendererPepperHostFactory() {
}

scoped_ptr<ResourceHost> ContentRendererPepperHostFactory::CreateResourceHost(
    ppapi::host::PpapiHost* host,
    const ppapi::proxy::ResourceMessageCallParams& params,
    PP_Instance instance,
    const IPC::Message& message) {
  DCHECK(host == host_->GetPpapiHost());

  // Make sure the plugin is giving us a valid instance for this resource.
  if (!host_->IsValidInstance(instance))
    return scoped_ptr<ResourceHost>();

  // Stable interfaces.
  switch (message.type()) {
    case PpapiHostMsg_WebSocket_Create::ID:
      return scoped_ptr<ResourceHost>(new PepperWebSocketHost(
          host_, instance, params.pp_resource()));
  }

  // Resources for dev interfaces.
  // TODO(brettw) when we support any public or private interfaces, put them in
  // a separate switch above.

  // TODO(brettw) put back this dev check! This was removed to fix issue 138902
  // where the permissions for bundled Flash (but not Flash that you specify
  // on the command line, making it difficult to test) are incorrect.
  /*if (GetPermissions().HasPermission(ppapi::PERMISSION_DEV))*/ {
    switch (message.type()) {
      case PpapiHostMsg_FileChooser_Create::ID:
        return scoped_ptr<ResourceHost>(new PepperFileChooserHost(
            host_, instance, params.pp_resource()));
    }
  }

  // Resources for Flash interfaces.
  if (GetPermissions().HasPermission(ppapi::PERMISSION_FLASH)) {
    switch (message.type()) {
      case PpapiHostMsg_Flash_Create::ID:
        return scoped_ptr<ResourceHost>(new PepperFlashHost(
            host_, instance, params.pp_resource()));
    }
  }

  return scoped_ptr<ResourceHost>();
}

const ppapi::PpapiPermissions&
ContentRendererPepperHostFactory::GetPermissions() const {
  return host_->GetPpapiHost()->permissions();
}

}  // namespace content
