// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/browser_plugin/browser_plugin_manager_impl.h"

#include "content/common/browser_plugin_messages.h"
#include "content/renderer/browser_plugin/browser_plugin.h"
#include "content/renderer/render_thread_impl.h"

namespace content {

BrowserPluginManagerImpl::BrowserPluginManagerImpl() {
}

BrowserPluginManagerImpl::~BrowserPluginManagerImpl() {
}

BrowserPlugin* BrowserPluginManagerImpl::CreateBrowserPlugin(
    RenderViewImpl* render_view,
    WebKit::WebFrame* frame,
    const WebKit::WebPluginParams& params) {
  return new BrowserPlugin(browser_plugin_counter_++,
                           render_view,
                           frame,
                           params);
}

bool BrowserPluginManagerImpl::Send(IPC::Message* msg) {
  return RenderThread::Get()->Send(msg);
}

bool BrowserPluginManagerImpl::OnControlMessageReceived(
    const IPC::Message& message) {
  DCHECK(CalledOnValidThread());
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(BrowserPluginManagerImpl, message)
    IPC_MESSAGE_HANDLER(BrowserPluginMsg_UpdateRect, OnUpdateRect)
    IPC_MESSAGE_HANDLER(BrowserPluginMsg_GuestGone, OnGuestGone)
    IPC_MESSAGE_HANDLER(BrowserPluginMsg_AdvanceFocus, OnAdvanceFocus)
    IPC_MESSAGE_HANDLER(BrowserPluginMsg_GuestContentWindowReady,
                        OnGuestContentWindowReady)
    IPC_MESSAGE_HANDLER(BrowserPluginMsg_ShouldAcceptTouchEvents,
                        OnShouldAcceptTouchEvents)
    IPC_MESSAGE_HANDLER(BrowserPluginMsg_LoadStart, OnLoadStart)
    IPC_MESSAGE_HANDLER(BrowserPluginMsg_LoadAbort, OnLoadAbort)
    IPC_MESSAGE_HANDLER(BrowserPluginMsg_LoadRedirect, OnLoadRedirect)
    IPC_MESSAGE_HANDLER(BrowserPluginMsg_LoadCommit, OnLoadCommit)
    IPC_MESSAGE_HANDLER(BrowserPluginMsg_LoadStop, OnLoadStop)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void BrowserPluginManagerImpl::OnUpdateRect(
    int instance_id,
    int message_id,
    const BrowserPluginMsg_UpdateRect_Params& params) {
  BrowserPlugin* plugin = GetBrowserPlugin(instance_id);
  if (plugin)
    plugin->UpdateRect(message_id, params);
}

void BrowserPluginManagerImpl::OnGuestGone(int instance_id,
                                           int process_id,
                                           int status) {
  BrowserPlugin* plugin = GetBrowserPlugin(instance_id);
  if (plugin)
    plugin->GuestGone(process_id, static_cast<base::TerminationStatus>(status));
}

void BrowserPluginManagerImpl::OnAdvanceFocus(int instance_id, bool reverse) {
  BrowserPlugin* plugin = GetBrowserPlugin(instance_id);
  if (plugin)
    plugin->AdvanceFocus(reverse);
}

void BrowserPluginManagerImpl::OnGuestContentWindowReady(int instance_id,
                                                         int guest_routing_id) {
  BrowserPlugin* plugin = GetBrowserPlugin(instance_id);
  if (plugin)
    plugin->GuestContentWindowReady(guest_routing_id);
}

void BrowserPluginManagerImpl::OnShouldAcceptTouchEvents(int instance_id,
                                                         bool accept) {
  BrowserPlugin* plugin = GetBrowserPlugin(instance_id);
  if (plugin)
    plugin->SetAcceptTouchEvents(accept);
}

void BrowserPluginManagerImpl::OnLoadStart(int instance_id,
                                           const GURL& url,
                                           bool is_top_level) {
  BrowserPlugin* plugin = GetBrowserPlugin(instance_id);
  if (plugin)
    plugin->LoadStart(url, is_top_level);
}

void BrowserPluginManagerImpl::OnLoadCommit(
    int instance_id,
    const BrowserPluginMsg_LoadCommit_Params& params) {
  BrowserPlugin* plugin = GetBrowserPlugin(instance_id);
  if (plugin)
    plugin->LoadCommit(params);
}

void BrowserPluginManagerImpl::OnLoadStop(int instance_id) {
  BrowserPlugin* plugin = GetBrowserPlugin(instance_id);
  if (plugin)
    plugin->LoadStop();
}

void BrowserPluginManagerImpl::OnLoadAbort(int instance_id,
                                           const GURL& url,
                                           bool is_top_level,
                                           const std::string& type) {
  BrowserPlugin* plugin = GetBrowserPlugin(instance_id);
  if (plugin)
    plugin->LoadAbort(url, is_top_level, type);
}

void BrowserPluginManagerImpl::OnLoadRedirect(int instance_id,
                                              const GURL& old_url,
                                              const GURL& new_url,
                                              bool is_top_level) {
  BrowserPlugin* plugin = GetBrowserPlugin(instance_id);
  if (plugin)
    plugin->LoadRedirect(old_url, new_url, is_top_level);
}

}  // namespace content
