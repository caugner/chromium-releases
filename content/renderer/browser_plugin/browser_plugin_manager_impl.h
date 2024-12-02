// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_BROWSER_PLUGIN_BROWSER_PLUGIN_MANAGER_IMPL_H_
#define CONTENT_RENDERER_BROWSER_PLUGIN_BROWSER_PLUGIN_MANAGER_IMPL_H_

#include "content/renderer/browser_plugin/browser_plugin_manager.h"
#include "googleurl/src/gurl.h"

struct BrowserPluginMsg_LoadCommit_Params;
struct BrowserPluginMsg_UpdateRect_Params;

namespace content {

class BrowserPluginManagerImpl : public BrowserPluginManager {
 public:
  BrowserPluginManagerImpl();
  virtual ~BrowserPluginManagerImpl();

  // BrowserPluginManager implementation.
  virtual BrowserPlugin* CreateBrowserPlugin(
      RenderViewImpl* render_view,
      WebKit::WebFrame* frame,
      const WebKit::WebPluginParams& params) OVERRIDE;

  // IPC::Sender implementation.
  virtual bool Send(IPC::Message* msg) OVERRIDE;

  // RenderProcessObserver override. Call on render thread.
  virtual bool OnControlMessageReceived(const IPC::Message& message) OVERRIDE;
 private:
  void OnUpdateRect(int instance_id,
                    int message_id,
                    const BrowserPluginMsg_UpdateRect_Params& params);
  void OnGuestGone(int instance_id, int process_id, int status);
  void OnAdvanceFocus(int instance_id, bool reverse);
  void OnGuestContentWindowReady(int instance_id, int guest_routing_id);
  void OnShouldAcceptTouchEvents(int instance_id, bool accept);
  void OnLoadStart(int instance_id,
                   const GURL& url,
                   bool is_top_level);
  void OnLoadCommit(int instance_id,
                    const BrowserPluginMsg_LoadCommit_Params& params);
  void OnLoadStop(int instance_id);
  void OnLoadAbort(int instance_id,
                   const GURL& url,
                   bool is_top_level,
                   const std::string& type);
  void OnLoadRedirect(int instance_id,
                      const GURL& old_url,
                      const GURL& new_url,
                      bool is_top_level);

  DISALLOW_COPY_AND_ASSIGN(BrowserPluginManagerImpl);
};

}  // namespace content

#endif //  CONTENT_RENDERER_BROWSER_PLUGIN_BROWSER_PLUGIN_MANAGER_IMPL_H_
