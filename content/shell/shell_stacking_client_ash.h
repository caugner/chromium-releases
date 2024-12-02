// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_SHELL_STACKING_CLIENT_ASH_H_
#define CONTENT_SHELL_SHELL_STACKING_CLIENT_ASH_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "ui/aura/client/stacking_client.h"

namespace aura {
class RootWindow;
class Window;
namespace shared {
class CompoundEventFilter;
class InputMethodEventFilter;
class RootWindowCaptureClient;
}
namespace test {
class TestActivationClient;
}
}

namespace gfx {
class Rect;
}

namespace content {

// Creates a minimal environment for running the shell. We can't pull in all of
// ash here, but we can create attach several of the same things we'd find in
// the ash parts of the code.
class ShellStackingClientAsh : public aura::client::StackingClient {
 public:
  ShellStackingClientAsh();
  virtual ~ShellStackingClientAsh();

  // Overridden from client::StackingClient:
  virtual aura::Window* GetDefaultParent(aura::Window* window,
                                         const gfx::Rect& bounds) OVERRIDE;

 private:
  scoped_ptr<aura::RootWindow> root_window_;

  // Owned by RootWindow
  aura::shared::CompoundEventFilter* root_window_event_filter_;

  scoped_ptr<aura::shared::RootWindowCaptureClient> capture_client_;
  scoped_ptr<aura::shared::InputMethodEventFilter> input_method_filter_;
  scoped_ptr<aura::test::TestActivationClient> test_activation_client_;

  DISALLOW_COPY_AND_ASSIGN(ShellStackingClientAsh);
};

}  // namespace content;

#endif  // CONTENT_SHELL_SHELL_STACKING_CLIENT_ASH_H_
