// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/pepper/chrome_renderer_pepper_host_factory.h"

#include "base/logging.h"
#include "chrome/renderer/pepper/pepper_flash_font_file_host.h"
#include "content/public/renderer/renderer_ppapi_host.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/host/resource_host.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/ppapi_message_utils.h"
#include "ppapi/shared_impl/ppapi_permissions.h"

using ppapi::host::ResourceHost;

namespace chrome {

ChromeRendererPepperHostFactory::ChromeRendererPepperHostFactory(
    content::RendererPpapiHost* host)
    : host_(host) {
}

ChromeRendererPepperHostFactory::~ChromeRendererPepperHostFactory() {
}

scoped_ptr<ResourceHost>
ChromeRendererPepperHostFactory::CreateResourceHost(
    ppapi::host::PpapiHost* host,
    const ppapi::proxy::ResourceMessageCallParams& params,
    PP_Instance instance,
    const IPC::Message& message) {
  DCHECK(host == host_->GetPpapiHost());

  // Make sure the plugin is giving us a valid instance for this resource.
  if (!host_->IsValidInstance(instance))
    return scoped_ptr<ResourceHost>();

  if (host_->GetPpapiHost()->permissions().HasPermission(
      ppapi::PERMISSION_FLASH)) {
    switch (message.type()) {
      case PpapiHostMsg_FlashFontFile_Create::ID:
        ppapi::proxy::SerializedFontDescription description;
        PP_PrivateFontCharset charset;
        if (ppapi::UnpackMessage<PpapiHostMsg_FlashFontFile_Create>(
            message, &description, &charset)) {
          return scoped_ptr<ResourceHost>(new PepperFlashFontFileHost(
              host_, instance, params.pp_resource(), description, charset));
        }
        break;
    }
  }

  return scoped_ptr<ResourceHost>();
}

}  // namespace chrome
