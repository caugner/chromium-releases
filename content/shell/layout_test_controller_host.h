// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_LAYOUT_TEST_CONTROLLER_HOST_H_
#define CONTENT_SHELL_LAYOUT_TEST_CONTROLLER_HOST_H_
#pragma once

#include <map>
#include <string>

#include "content/public/browser/render_view_host_observer.h"

namespace content {

class LayoutTestControllerHost : public RenderViewHostObserver {
 public:
  static LayoutTestControllerHost* FromRenderViewHost(
      RenderViewHost* render_view_host);

  explicit LayoutTestControllerHost(RenderViewHost* render_view_host);
  virtual ~LayoutTestControllerHost();

  bool should_stay_on_page_after_handling_before_unload() const {
    return should_stay_on_page_after_handling_before_unload_;
  }

  // RenderViewHostObserver implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

 private:
  void CaptureDump();

  // Message handlers.
  void OnDidFinishLoad();
  void OnTextDump(const std::string& dump);

  // layoutTestController handlers.
  void OnNotifyDone();
  void OnDumpAsText();
  void OnDumpChildFramesAsText();
  void OnSetPrinting();
  void OnSetShouldStayOnPageAfterHandlingBeforeUnload(bool should_stay_on_page);
  void OnWaitUntilDone();

  static std::map<RenderViewHost*, LayoutTestControllerHost*> controllers_;

  bool dump_as_text_;
  bool dump_child_frames_;
  bool is_printing_;
  bool should_stay_on_page_after_handling_before_unload_;
  bool wait_until_done_;

  DISALLOW_COPY_AND_ASSIGN(LayoutTestControllerHost);
};

}  // namespace content

#endif  // CONTENT_SHELL_LAYOUT_TEST_CONTROLLER_HOST_H_
