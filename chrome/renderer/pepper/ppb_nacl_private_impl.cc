// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/pepper/ppb_nacl_private_impl.h"

#ifndef DISABLE_NACL

#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/render_messages.h"
#include "chrome/renderer/chrome_render_process_observer.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/sandbox_init.h"
#include "content/public/renderer/renderer_ppapi_host.h"
#include "content/public/renderer/render_thread.h"
#include "ipc/ipc_sync_message_filter.h"
#include "ppapi/c/pp_bool.h"
#include "ppapi/c/private/pp_file_handle.h"
#include "ppapi/c/private/ppb_nacl_private.h"
#include "ppapi/native_client/src/trusted/plugin/nacl_entry_points.h"
#include "ppapi/shared_impl/ppapi_preferences.h"
#include "webkit/plugins/ppapi/host_globals.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"

namespace {

// This allows us to send requests from background threads.
// E.g., to do LaunchSelLdr for helper nexes (which is done synchronously),
// in a background thread, to avoid jank.
base::LazyInstance<scoped_refptr<IPC::SyncMessageFilter> >
    g_background_thread_sender = LAZY_INSTANCE_INITIALIZER;

struct InstanceInfo {
  InstanceInfo() : plugin_child_id(0) {}
  GURL url;
  int plugin_child_id;
  IPC::ChannelHandle channel_handle;
};

typedef std::map<PP_Instance, InstanceInfo> InstanceInfoMap;

base::LazyInstance<InstanceInfoMap> g_instance_info =
    LAZY_INSTANCE_INITIALIZER;

// Launch NaCl's sel_ldr process.
PP_Bool LaunchSelLdr(PP_Instance instance,
                     const char* alleged_url,
                     int socket_count,
                     void* imc_handles) {
  std::vector<nacl::FileDescriptor> sockets;
  IPC::Sender* sender = content::RenderThread::Get();
  if (sender == NULL)
    sender = g_background_thread_sender.Pointer()->get();

  InstanceInfo instance_info;
  instance_info.url = GURL(alleged_url);
  if (!sender->Send(new ChromeViewHostMsg_LaunchNaCl(
          instance_info.url, socket_count, &sockets,
          &instance_info.channel_handle,
          &instance_info.plugin_child_id))) {
    return PP_FALSE;
  }

  // Don't save instance_info if channel handle is invalid.
  bool invalid_handle = instance_info.channel_handle.name.empty();
#if defined(OS_POSIX)
  if (!invalid_handle)
    invalid_handle = (instance_info.channel_handle.socket.fd == -1);
#endif
  if (!invalid_handle)
    g_instance_info.Get()[instance] = instance_info;

  CHECK(static_cast<int>(sockets.size()) == socket_count);
  for (int i = 0; i < socket_count; i++) {
    static_cast<nacl::Handle*>(imc_handles)[i] =
        nacl::ToNativeHandle(sockets[i]);
  }

  return PP_TRUE;
}

PP_Bool StartPpapiProxy(PP_Instance instance,
                        bool allow_dev_interfaces) {
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableNaClIPCProxy)) {
    InstanceInfoMap& map = g_instance_info.Get();
    InstanceInfoMap::iterator it = map.find(instance);
    if (it == map.end())
      return PP_FALSE;
    InstanceInfo instance_info = it->second;
    map.erase(it);

    webkit::ppapi::PluginInstance* plugin_instance =
        content::GetHostGlobals()->GetInstance(instance);
    if (!plugin_instance)
      return PP_FALSE;

  // Create a new module for each instance of the NaCl plugin that is using
  // the IPC based out-of-process proxy. We can't use the existing module,
  // because it is configured for the in-process NaCl plugin, and we must
  // keep it that way to allow the page to create other instances.
  webkit::ppapi::PluginModule* plugin_module = plugin_instance->module();
  scoped_refptr<webkit::ppapi::PluginModule> nacl_plugin_module(
      plugin_module->CreateModuleForNaClInstance());

    ppapi::PpapiPermissions permissions(
        allow_dev_interfaces ? ppapi::PERMISSION_DEV : 0);
    content::RendererPpapiHost* renderer_ppapi_host =
        content::RendererPpapiHost::CreateExternalPluginModule(
            nacl_plugin_module,
            plugin_instance,
            FilePath().AppendASCII(instance_info.url.spec()),
            permissions,
            instance_info.channel_handle,
            instance_info.plugin_child_id);
    if (renderer_ppapi_host) {
      // Allow the module to reset the instance to the new proxy.
      nacl_plugin_module->InitAsProxiedNaCl(plugin_instance);
      return PP_TRUE;
    }
  }
  return PP_FALSE;
}

int UrandomFD(void) {
#if defined(OS_POSIX)
  return base::GetUrandomFD();
#else
  return -1;
#endif
}

bool Are3DInterfacesDisabled() {
  return CommandLine::ForCurrentProcess()->HasSwitch(switches::kDisable3DAPIs);
}

void EnableBackgroundSelLdrLaunch() {
  g_background_thread_sender.Get() =
      content::RenderThread::Get()->GetSyncMessageFilter();
}

int BrokerDuplicateHandle(void* source_handle,
                          unsigned int process_id,
                          void** target_handle,
                          unsigned int desired_access,
                          unsigned int options) {
#if defined(OS_WIN)
  return content::BrokerDuplicateHandle(source_handle, process_id,
                                        target_handle, desired_access,
                                        options);
#else
  return 0;
#endif
}

PP_FileHandle GetReadonlyPnaclFD(const char* filename) {
  IPC::PlatformFileForTransit out_fd = IPC::InvalidPlatformFileForTransit();
  IPC::Sender* sender = content::RenderThread::Get();
  if (sender == NULL)
    sender = g_background_thread_sender.Pointer()->get();

  if (!sender->Send(new ChromeViewHostMsg_GetReadonlyPnaclFD(
          std::string(filename),
          &out_fd))) {
    return base::kInvalidPlatformFileValue;
  }

  if (out_fd == IPC::InvalidPlatformFileForTransit()) {
    return base::kInvalidPlatformFileValue;
  }

  base::PlatformFile handle =
      IPC::PlatformFileForTransitToPlatformFile(out_fd);
  return handle;
}

PP_FileHandle CreateTemporaryFile(PP_Instance instance) {
  IPC::PlatformFileForTransit transit_fd = IPC::InvalidPlatformFileForTransit();
  IPC::Sender* sender = content::RenderThread::Get();
  if (sender == NULL)
    sender = g_background_thread_sender.Pointer()->get();

  if (!sender->Send(new ChromeViewHostMsg_NaClCreateTemporaryFile(
          &transit_fd))) {
    return base::kInvalidPlatformFileValue;
  }

  if (transit_fd == IPC::InvalidPlatformFileForTransit()) {
    return base::kInvalidPlatformFileValue;
  }

  base::PlatformFile handle = IPC::PlatformFileForTransitToPlatformFile(
      transit_fd);
  return handle;
}

PP_Bool IsOffTheRecord() {
  return PP_FromBool(ChromeRenderProcessObserver::is_incognito_process());
}

PP_Bool IsPnaclEnabled() {
  return PP_FromBool(CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnablePnacl));
}

const PPB_NaCl_Private nacl_interface = {
  &LaunchSelLdr,
  &StartPpapiProxy,
  &UrandomFD,
  &Are3DInterfacesDisabled,
  &EnableBackgroundSelLdrLaunch,
  &BrokerDuplicateHandle,
  &GetReadonlyPnaclFD,
  &CreateTemporaryFile,
  &IsOffTheRecord,
  &IsPnaclEnabled
};

}  // namespace

const PPB_NaCl_Private* PPB_NaCl_Private_Impl::GetInterface() {
  return &nacl_interface;
}

#endif  // DISABLE_NACL
