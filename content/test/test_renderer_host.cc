// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/test_renderer_host.h"

#include "content/browser/renderer_host/render_view_host_factory.h"
#include "content/browser/renderer_host/test_render_view_host.h"
#include "content/browser/site_instance_impl.h"
#include "content/browser/web_contents/navigation_entry_impl.h"
#include "content/browser/web_contents/test_web_contents.h"
#include "content/public/browser/web_contents.h"
#include "content/test/mock_render_process_host.h"
#include "content/test/test_browser_context.h"
#include "content/test/test_render_view_host_factory.h"

#if defined(USE_AURA)
#include "ui/aura/env.h"
#include "ui/aura/monitor_manager.h"
#include "ui/aura/root_window.h"
#include "ui/aura/single_monitor_manager.h"
#include "ui/aura/test/test_screen.h"
#include "ui/aura/test/test_stacking_client.h"
#include "ui/gfx/screen.h"
#endif

namespace content {

// static
RenderViewHostTester* RenderViewHostTester::For(RenderViewHost* host) {
  return static_cast<TestRenderViewHost*>(host);
}

// static
void RenderViewHostTester::EnableAccessibilityUpdatedNotifications(
    RenderViewHost* host) {
  static_cast<RenderViewHostImpl*>(
      host)->set_send_accessibility_updated_notifications(true);
}

// static
RenderViewHost* RenderViewHostTester::GetPendingForController(
    NavigationController* controller) {
  WebContentsImpl* web_contents = static_cast<WebContentsImpl*>(
      controller->GetWebContents());
  return web_contents->GetRenderManagerForTesting()->pending_render_view_host();
}

// static
bool RenderViewHostTester::IsRenderViewHostSwappedOut(RenderViewHost* rvh) {
  return static_cast<RenderViewHostImpl*>(rvh)->is_swapped_out();
}

// static
bool RenderViewHostTester::TestOnMessageReceived(RenderViewHost* rvh,
                                                 const IPC::Message& msg) {
  return static_cast<RenderViewHostImpl*>(rvh)->OnMessageReceived(msg);
}

RenderViewHostTestEnabler::RenderViewHostTestEnabler()
    : rph_factory_(new MockRenderProcessHostFactory()),
      rvh_factory_(new TestRenderViewHostFactory(rph_factory_.get())) {
}

RenderViewHostTestEnabler::~RenderViewHostTestEnabler() {
}

RenderViewHostTestHarness::RenderViewHostTestHarness()
    : contents_(NULL) {
}

RenderViewHostTestHarness::~RenderViewHostTestHarness() {
}

NavigationController& RenderViewHostTestHarness::controller() {
  return web_contents()->GetController();
}

WebContents* RenderViewHostTestHarness::web_contents() {
  return contents_.get();
}

RenderViewHost* RenderViewHostTestHarness::rvh() {
  return web_contents()->GetRenderViewHost();
}

RenderViewHost* RenderViewHostTestHarness::pending_rvh() {
  return static_cast<TestWebContents*>(web_contents())->
      GetRenderManagerForTesting()->pending_render_view_host();
}

RenderViewHost* RenderViewHostTestHarness::active_rvh() {
  return pending_rvh() ? pending_rvh() : rvh();
}

BrowserContext* RenderViewHostTestHarness::browser_context() {
  return browser_context_.get();
}

MockRenderProcessHost* RenderViewHostTestHarness::process() {
  return static_cast<MockRenderProcessHost*>(active_rvh()->GetProcess());
}

void RenderViewHostTestHarness::DeleteContents() {
  SetContents(NULL);
}

void RenderViewHostTestHarness::SetContents(WebContents* contents) {
  contents_.reset(contents);
}

WebContents* RenderViewHostTestHarness::CreateTestWebContents() {
  // See comment above browser_context_ decl for why we check for NULL here.
  if (!browser_context_.get())
    browser_context_.reset(new TestBrowserContext());

  // This will be deleted when the WebContentsImpl goes away.
  SiteInstance* instance = SiteInstance::Create(browser_context_.get());

  return new TestWebContents(browser_context_.get(), instance);
}

void RenderViewHostTestHarness::NavigateAndCommit(const GURL& url) {
  static_cast<TestWebContents*>(web_contents())->NavigateAndCommit(url);
}

void RenderViewHostTestHarness::Reload() {
  NavigationEntry* entry = controller().GetLastCommittedEntry();
  DCHECK(entry);
  controller().Reload(false);
  static_cast<TestRenderViewHost*>(
      rvh())->SendNavigate(entry->GetPageID(), entry->GetURL());
}

void RenderViewHostTestHarness::SetUp() {
#if defined(USE_AURA)
  aura::Env::GetInstance()->SetMonitorManager(new aura::SingleMonitorManager);
  root_window_.reset(aura::MonitorManager::CreateRootWindowForPrimaryMonitor());
  gfx::Screen::SetInstance(new aura::TestScreen(root_window_.get()));
  test_stacking_client_.reset(
      new aura::test::TestStackingClient(root_window_.get()));
#endif  // USE_AURA
  SetContents(CreateTestWebContents());
}

void RenderViewHostTestHarness::TearDown() {
  SetContents(NULL);
#if defined(USE_AURA)
  test_stacking_client_.reset();
  root_window_.reset();
#endif

  // Make sure that we flush any messages related to WebContentsImpl destruction
  // before we destroy the browser context.
  MessageLoop::current()->RunAllPending();

  // Release the browser context on the UI thread.
  message_loop_.DeleteSoon(FROM_HERE, browser_context_.release());
  message_loop_.RunAllPending();
}

void RenderViewHostTestHarness::SetRenderProcessHostFactory(
    RenderProcessHostFactory* factory) {
    rvh_test_enabler_.rvh_factory_->set_render_process_host_factory(factory);
}

}  // namespace content
