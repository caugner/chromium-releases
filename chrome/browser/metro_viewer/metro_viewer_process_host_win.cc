// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metro_viewer/metro_viewer_process_host_win.h"

#include "base/logging.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/ui/ash/ash_init.h"
#include "content/public/browser/browser_thread.h"
#include "ipc/ipc_channel_proxy.h"
#include "ui/aura/remote_root_window_host_win.h"
#include "ui/metro_viewer/metro_viewer_messages.h"
#include "ui/surface/accelerated_surface_win.h"

MetroViewerProcessHost::MetroViewerProcessHost() {
  channel_.reset(new IPC::ChannelProxy(
      // TODO(scottmg): Need to have a secure way to randomize and request
      // this name from the viewer-side.
      "viewer",
      IPC::Channel::MODE_NAMED_SERVER,
      this,
      content::BrowserThread::GetMessageLoopProxyForThread(
          content::BrowserThread::IO)));
}

MetroViewerProcessHost::~MetroViewerProcessHost() {
}

bool MetroViewerProcessHost::Send(IPC::Message* msg) {
  return channel_->Send(msg);
}

bool MetroViewerProcessHost::OnMessageReceived(const IPC::Message& message) {
  DCHECK(CalledOnValidThread());
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(MetroViewerProcessHost, message)
    IPC_MESSAGE_HANDLER(MetroViewerHostMsg_SetTargetSurface, OnSetTargetSurface)
    IPC_MESSAGE_HANDLER(MetroViewerHostMsg_MouseMoved, OnMouseMoved)
    IPC_MESSAGE_HANDLER(MetroViewerHostMsg_MouseButton, OnMouseButton)
    IPC_MESSAGE_HANDLER(MetroViewerHostMsg_KeyDown, OnKeyDown)
    IPC_MESSAGE_HANDLER(MetroViewerHostMsg_KeyUp, OnKeyUp)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void  MetroViewerProcessHost::OnChannelError() {
  // TODO(cpu): At some point we only close the browser. Right now this
  // is very convenient for developing.
  DLOG(INFO) << "viewer channel error : Quitting browser";
  browser::CloseAllBrowsers();
}

void MetroViewerProcessHost::OnSetTargetSurface(
    gfx::NativeViewId target_surface) {
  DLOG(INFO) << __FUNCTION__ << ", target_surface = " << target_surface;
  HWND hwnd = reinterpret_cast<HWND>(target_surface);

  chrome::OpenAsh();

  scoped_refptr<AcceleratedPresenter> any_window =
      AcceleratedPresenter::GetForWindow(NULL);
  any_window->SetNewTargetWindow(hwnd);
}

// TODO(cpu): Find a decent way to get to the root window host in the
// next four methods.
void MetroViewerProcessHost::OnMouseMoved(int32 x, int32 y, int32 modifiers) {
  aura::RemoteRootWindowHostWin::Instance()->OnMouseMoved(x, y, modifiers);
}

void MetroViewerProcessHost::OnMouseButton(int32 x, int32 y, int32 modifiers) {
  aura::RemoteRootWindowHostWin::Instance()->OnMouseClick(x, y, modifiers);
}

void MetroViewerProcessHost::OnKeyDown(uint32 vkey,
                                       uint32 repeat_count,
                                       uint32 scan_code) {
  aura::RemoteRootWindowHostWin::Instance()->OnKeyDown(
      vkey, repeat_count, scan_code);
}

void MetroViewerProcessHost::OnKeyUp(uint32 vkey,
                                     uint32 repeat_count,
                                     uint32 scan_code) {
  aura::RemoteRootWindowHostWin::Instance()->OnKeyUp(
      vkey, repeat_count, scan_code);
}
