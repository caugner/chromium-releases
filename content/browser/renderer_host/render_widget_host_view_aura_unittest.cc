// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_widget_host_view_aura.h"

#include "base/basictypes.h"
#include "base/message_loop.h"
#include "content/browser/renderer_host/render_widget_host_delegate.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/common/view_messages.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_browser_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/env.h"
#include "ui/aura/test/aura_test_helper.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/base/events/event.h"
#include "ui/base/ui_base_types.h"

namespace content {
namespace {
class MockRenderWidgetHostDelegate : public RenderWidgetHostDelegate {
 public:
  MockRenderWidgetHostDelegate() {}
  virtual ~MockRenderWidgetHostDelegate() {}
};

// Simple observer that keeps track of changes to a window for tests.
class TestWindowObserver : public aura::WindowObserver {
 public:
  explicit TestWindowObserver(aura::Window* window_to_observe)
      : window_(window_to_observe) {
    window_->AddObserver(this);
  }
  virtual ~TestWindowObserver() {
    if (window_)
      window_->RemoveObserver(this);
  }

  bool destroyed() const { return destroyed_; }

  // aura::WindowObserver overrides:
  virtual void OnWindowDestroyed(aura::Window* window) {
    CHECK_EQ(window, window_);
    destroyed_ = true;
    window_ = NULL;
  }

 private:
  // Window that we're observing, or NULL if it's been destroyed.
  aura::Window* window_;

  // Was |window_| destroyed?
  bool destroyed_;

  DISALLOW_COPY_AND_ASSIGN(TestWindowObserver);
};

class RenderWidgetHostViewAuraTest : public testing::Test {
 public:
  RenderWidgetHostViewAuraTest() {}

  virtual void SetUp() {
    aura_test_helper_.reset(new aura::test::AuraTestHelper(&message_loop_));
    aura_test_helper_->SetUp();

    browser_context_.reset(new TestBrowserContext);
    MockRenderProcessHost* process_host =
        new MockRenderProcessHost(browser_context_.get());
    widget_host_ = new RenderWidgetHostImpl(
        &delegate_, process_host, MSG_ROUTING_NONE);
    view_ = static_cast<RenderWidgetHostViewAura*>(
        RenderWidgetHostView::CreateViewForWidget(widget_host_));
  }

  virtual void TearDown() {
    if (view_)
      view_->Destroy();
    delete widget_host_;

    browser_context_.reset();
    aura_test_helper_->TearDown();

    message_loop_.DeleteSoon(FROM_HERE, browser_context_.release());
    message_loop_.RunAllPending();
  }

 protected:
  MessageLoopForUI message_loop_;
  scoped_ptr<aura::test::AuraTestHelper> aura_test_helper_;
  scoped_ptr<BrowserContext> browser_context_;
  MockRenderWidgetHostDelegate delegate_;

  // Tests should set these to NULL if they've already triggered their
  // destruction.
  RenderWidgetHostImpl* widget_host_;
  RenderWidgetHostViewAura* view_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostViewAuraTest);
};

}  // namespace

// Checks that a fullscreen view has the correct show-state and receives the
// focus.
TEST_F(RenderWidgetHostViewAuraTest, FocusFullscreen) {
  view_->InitAsFullscreen(NULL);
  aura::Window* window = view_->GetNativeView();
  ASSERT_TRUE(window != NULL);
  EXPECT_EQ(ui::SHOW_STATE_FULLSCREEN,
            window->GetProperty(aura::client::kShowStateKey));

  // Check that we requested and received the focus.
  EXPECT_TRUE(window->HasFocus());

  // Check that we'll also say it's okay to activate the window when there's an
  // ActivationClient defined.
  EXPECT_TRUE(view_->ShouldActivate(NULL));
}

// Checks that a fullscreen view is destroyed when it loses the focus.
TEST_F(RenderWidgetHostViewAuraTest, DestroyFullscreenOnBlur) {
  view_->InitAsFullscreen(NULL);
  aura::Window* window = view_->GetNativeView();
  ASSERT_TRUE(window != NULL);
  ASSERT_TRUE(window->HasFocus());

  // After we create and focus another window, the RWHVA's window should be
  // destroyed.
  TestWindowObserver observer(window);
  aura::test::TestWindowDelegate delegate;
  scoped_ptr<aura::Window> sibling(new aura::Window(&delegate));
  sibling->Init(ui::LAYER_TEXTURED);
  sibling->Show();
  window->parent()->AddChild(sibling.get());
  sibling->Focus();
  ASSERT_TRUE(sibling->HasFocus());
  ASSERT_TRUE(observer.destroyed());

  widget_host_ = NULL;
  view_ = NULL;
}

#if !defined(OS_WIN)
// Checks that touch-event state is maintained correctly. A lot of the
// touch-event related functions are not implemented on Windows. So run this
// test only in chromeos. http://crbug.com/157268
TEST_F(RenderWidgetHostViewAuraTest, TouchEventState) {
  view_->InitAsChild(NULL);
  view_->Show();

  // Start with no touch-event handler in the renderer.
  widget_host_->OnMessageReceived(ViewHostMsg_HasTouchEventHandlers(0, false));
  EXPECT_FALSE(widget_host_->ShouldForwardTouchEvent());

  ui::TouchEvent press(ui::ET_TOUCH_PRESSED, gfx::Point(30, 30), 0,
      base::Time::NowFromSystemTime() - base::Time());
  ui::TouchEvent move(ui::ET_TOUCH_MOVED, gfx::Point(20, 20), 0,
      base::Time::NowFromSystemTime() - base::Time());
  ui::TouchEvent release(ui::ET_TOUCH_RELEASED, gfx::Point(20, 20), 0,
      base::Time::NowFromSystemTime() - base::Time());

  EXPECT_EQ(ui::ER_UNHANDLED, view_->OnTouchEvent(&press));
  EXPECT_EQ(WebKit::WebInputEvent::TouchStart, view_->touch_event_.type);
  EXPECT_EQ(1U, view_->touch_event_.touchesLength);
  EXPECT_EQ(WebKit::WebTouchPoint::StatePressed,
            view_->touch_event_.touches[0].state);

  EXPECT_EQ(ui::ER_UNHANDLED, view_->OnTouchEvent(&move));
  EXPECT_EQ(WebKit::WebInputEvent::TouchMove, view_->touch_event_.type);
  EXPECT_EQ(1U, view_->touch_event_.touchesLength);
  EXPECT_EQ(WebKit::WebTouchPoint::StateMoved,
            view_->touch_event_.touches[0].state);

  EXPECT_EQ(ui::ER_UNHANDLED, view_->OnTouchEvent(&release));
  EXPECT_EQ(WebKit::WebInputEvent::TouchEnd, view_->touch_event_.type);
  EXPECT_EQ(0U, view_->touch_event_.touchesLength);

  // Now install some touch-event handlers and do the same steps. The touch
  // events should now be consumed. However, the touch-event state should be
  // updated as before.
  widget_host_->OnMessageReceived(ViewHostMsg_HasTouchEventHandlers(0, true));
  EXPECT_TRUE(widget_host_->ShouldForwardTouchEvent());

  EXPECT_EQ(ui::ER_CONSUMED, view_->OnTouchEvent(&press));
  EXPECT_EQ(WebKit::WebInputEvent::TouchStart, view_->touch_event_.type);
  EXPECT_EQ(1U, view_->touch_event_.touchesLength);
  EXPECT_EQ(WebKit::WebTouchPoint::StatePressed,
            view_->touch_event_.touches[0].state);

  EXPECT_EQ(ui::ER_CONSUMED, view_->OnTouchEvent(&move));
  EXPECT_EQ(WebKit::WebInputEvent::TouchMove, view_->touch_event_.type);
  EXPECT_EQ(1U, view_->touch_event_.touchesLength);
  EXPECT_EQ(WebKit::WebTouchPoint::StateMoved,
            view_->touch_event_.touches[0].state);

  EXPECT_EQ(ui::ER_CONSUMED, view_->OnTouchEvent(&release));
  EXPECT_EQ(WebKit::WebInputEvent::TouchEnd, view_->touch_event_.type);
  EXPECT_EQ(0U, view_->touch_event_.touchesLength);

  // Now start a touch event, and remove the event-handlers before the release.
  EXPECT_EQ(ui::ER_CONSUMED, view_->OnTouchEvent(&press));
  EXPECT_EQ(WebKit::WebInputEvent::TouchStart, view_->touch_event_.type);
  EXPECT_EQ(1U, view_->touch_event_.touchesLength);
  EXPECT_EQ(WebKit::WebTouchPoint::StatePressed,
            view_->touch_event_.touches[0].state);

  widget_host_->OnMessageReceived(ViewHostMsg_HasTouchEventHandlers(0, false));
  EXPECT_FALSE(widget_host_->ShouldForwardTouchEvent());

  EXPECT_EQ(ui::ER_UNHANDLED, view_->OnTouchEvent(&move));
  EXPECT_EQ(WebKit::WebInputEvent::TouchMove, view_->touch_event_.type);
  EXPECT_EQ(1U, view_->touch_event_.touchesLength);
  EXPECT_EQ(WebKit::WebTouchPoint::StateMoved,
            view_->touch_event_.touches[0].state);

  EXPECT_EQ(ui::ER_UNHANDLED, view_->OnTouchEvent(&release));
  EXPECT_EQ(WebKit::WebInputEvent::TouchEnd, view_->touch_event_.type);
  EXPECT_EQ(0U, view_->touch_event_.touchesLength);
}
#endif // !defined(OS_WIN)

}  // namespace content
