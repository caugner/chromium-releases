// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/browser_status_monitor.h"

#include "ash/shell.h"
#include "ash/wm/window_util.h"
#include "chrome/browser/ui/ash/launcher/browser_shortcut_launcher_item_controller.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/web_applications/web_app.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "ui/aura/client/activation_client.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/gfx/screen.h"

BrowserStatusMonitor::BrowserStatusMonitor(
    ChromeLauncherController* launcher_controller)
    : launcher_controller_(launcher_controller),
      observed_activation_clients_(this),
      observed_root_windows_(this) {
  DCHECK(launcher_controller_);
  BrowserList::AddObserver(this);

  // This check needs for win7_aura. Without this, all tests in
  // ChromeLauncherController will fail in win7_aura.
  if (ash::Shell::HasInstance()) {
    // We can't assume all RootWindows have the same ActivationClient.
    // Add a RootWindow and its ActivationClient to the observed list.
    ash::Shell::RootWindowList root_windows = ash::Shell::GetAllRootWindows();
    ash::Shell::RootWindowList::const_iterator iter = root_windows.begin();
    for (; iter != root_windows.end(); ++iter) {
      // |observed_activation_clients_| can have the same activation client
      // multiple times - which would be handled by the used
      // |ScopedObserverWithDuplicatedSources|.
      observed_activation_clients_.Add(
          aura::client::GetActivationClient(*iter));
      observed_root_windows_.Add(static_cast<aura::Window*>(*iter));
    }
    ash::Shell::GetInstance()->GetScreen()->AddObserver(this);
  }
}

BrowserStatusMonitor::~BrowserStatusMonitor() {
  // This check needs for win7_aura. Without this, all tests in
  // ChromeLauncherController will fail in win7_aura.
  if (ash::Shell::HasInstance())
    ash::Shell::GetInstance()->GetScreen()->RemoveObserver(this);

  BrowserList::RemoveObserver(this);

  BrowserList* browser_list =
      BrowserList::GetInstance(chrome::HOST_DESKTOP_TYPE_ASH);
  for (BrowserList::const_iterator i = browser_list->begin();
       i != browser_list->end(); ++i) {
    OnBrowserRemoved(*i);
  }
}

void BrowserStatusMonitor::OnWindowActivated(aura::Window* gained_active,
                                             aura::Window* lost_active) {
  Browser* browser = chrome::FindBrowserWithWindow(lost_active);
  if (browser) {
    UpdateAppAndBrowserState(
        browser->tab_strip_model()->GetActiveWebContents());
  }

  browser = chrome::FindBrowserWithWindow(gained_active);
  if (browser) {
    UpdateAppAndBrowserState(
        browser->tab_strip_model()->GetActiveWebContents());
  }
}

void BrowserStatusMonitor::OnWindowDestroyed(aura::Window* window) {
  // Remove RootWindow and its ActivationClient from observed list.
  observed_root_windows_.Remove(window);
  observed_activation_clients_.Remove(aura::client::GetActivationClient(
      static_cast<aura::RootWindow*>(window)));
}

void BrowserStatusMonitor::OnBrowserAdded(Browser* browser) {
  if (browser->host_desktop_type() != chrome::HOST_DESKTOP_TYPE_ASH)
    return;

  browser->tab_strip_model()->AddObserver(this);

  if (browser->is_type_popup() && browser->is_app()) {
    std::string app_id =
        web_app::GetExtensionIdFromApplicationName(browser->app_name());
    if (!app_id.empty()) {
      browser_to_app_id_map_[browser] = app_id;
      launcher_controller_->LockV1AppWithID(app_id);
    }
  }
}

void BrowserStatusMonitor::OnBrowserRemoved(Browser* browser) {
  if (browser->host_desktop_type() != chrome::HOST_DESKTOP_TYPE_ASH)
    return;

  browser->tab_strip_model()->RemoveObserver(this);

  if (browser_to_app_id_map_.find(browser) != browser_to_app_id_map_.end()) {
    launcher_controller_->UnlockV1AppWithID(browser_to_app_id_map_[browser]);
    browser_to_app_id_map_.erase(browser);
  }
  UpdateBrowserItemState();
}

void BrowserStatusMonitor::OnDisplayBoundsChanged(
    const gfx::Display& display) {
  // Do nothing here.
}

void BrowserStatusMonitor::OnDisplayAdded(const gfx::Display& new_display) {
  // Add a new RootWindow and its ActivationClient to observed list.
  aura::RootWindow* root_window = ash::Shell::GetInstance()->
      display_controller()->GetRootWindowForDisplayId(new_display.id());
  // When the primary root window's display get removed, the existing root
  // window is taken over by the new display and the observer is already set.
  if (!observed_root_windows_.IsObserving(root_window)) {
    observed_root_windows_.Add(static_cast<aura::Window*>(root_window));
    observed_activation_clients_.Add(
        aura::client::GetActivationClient(root_window));
  }
}

void BrowserStatusMonitor::OnDisplayRemoved(const gfx::Display& old_display) {
  // When this is called, RootWindow of |old_display| is already removed.
  // Instead, we can remove RootWindow and its ActivationClient in the
  // OnWindowRemoved().
  // Do nothing here.
}

void BrowserStatusMonitor::ActiveTabChanged(content::WebContents* old_contents,
                                            content::WebContents* new_contents,
                                            int index,
                                            int reason) {
  Browser* browser = NULL;
  if (old_contents)
    browser = chrome::FindBrowserWithWebContents(old_contents);

  if (browser && browser->host_desktop_type() != chrome::HOST_DESKTOP_TYPE_ASH)
    return;

  // Update immediately on a tab change.
  if (browser &&
      (TabStripModel::kNoTab !=
           browser->tab_strip_model()->GetIndexOfWebContents(old_contents))) {
    launcher_controller_->UpdateAppState(
        old_contents,
        ChromeLauncherController::APP_STATE_INACTIVE);
  }

  UpdateAppAndBrowserState(new_contents);
}

void BrowserStatusMonitor::TabInsertedAt(content::WebContents* contents,
                                         int index,
                                         bool foreground) {
  UpdateAppAndBrowserState(contents);
}

void BrowserStatusMonitor::TabDetachedAt(content::WebContents* contents,
                                         int index) {
  launcher_controller_->UpdateAppState(
      contents, ChromeLauncherController::APP_STATE_REMOVED);
  UpdateBrowserItemState();
}

void BrowserStatusMonitor::TabChangedAt(
    content::WebContents* contents,
    int index,
    TabStripModelObserver::TabChangeType change_type) {
  UpdateAppAndBrowserState(contents);
}

void BrowserStatusMonitor::TabReplacedAt(TabStripModel* tab_strip_model,
                                         content::WebContents* old_contents,
                                         content::WebContents* new_contents,
                                         int index) {
  launcher_controller_->UpdateAppState(
      old_contents,
      ChromeLauncherController::APP_STATE_REMOVED);
  UpdateAppAndBrowserState(new_contents);
}

void BrowserStatusMonitor::UpdateAppAndBrowserState(
    content::WebContents* contents) {
  if (contents) {
    ChromeLauncherController::AppState app_state =
        ChromeLauncherController::APP_STATE_INACTIVE;

    Browser* browser = chrome::FindBrowserWithWebContents(contents);
    DCHECK(browser);
    if (browser->host_desktop_type() != chrome::HOST_DESKTOP_TYPE_ASH)
      return;
    if (browser->tab_strip_model()->GetActiveWebContents() == contents) {
      if (browser->window()->IsActive())
        app_state = ChromeLauncherController::APP_STATE_WINDOW_ACTIVE;
      else
        app_state = ChromeLauncherController::APP_STATE_ACTIVE;
    }

    launcher_controller_->UpdateAppState(contents, app_state);
  }
  UpdateBrowserItemState();
}

void BrowserStatusMonitor::UpdateBrowserItemState() {
  launcher_controller_->GetBrowserShortcutLauncherItemController()->
      UpdateBrowserItemState();
}
