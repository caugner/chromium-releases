// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/path_service.h"
#include "base/time.h"
#include "base/timer.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_dll_resource.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/app_list/app_list_controller.h"
#include "chrome/browser/ui/app_list/app_list_view_delegate.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/ui/views/browser_dialogs.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "grit/generated_resources.h"
#include "ui/app_list/app_list_view.h"
#include "ui/app_list/pagination_model.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/win/shell.h"
#include "ui/gfx/display.h"
#include "ui/gfx/screen.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/widget/widget.h"

namespace {

// Offset from the cursor to the point of the bubble arrow. It looks weird
// if the arrow comes up right on top of the cursor, so it is offset by this
// amount.
static const int kAnchorOffset = 25;

class AppListControllerDelegateWin : public AppListControllerDelegate {
 public:
  AppListControllerDelegateWin();
  virtual ~AppListControllerDelegateWin();

 private:
  // AppListController overrides:
  virtual void CloseView() OVERRIDE;
  virtual void ViewClosing() OVERRIDE;
  virtual void ViewActivationChanged(bool active) OVERRIDE;
  virtual bool CanPin() OVERRIDE;
  virtual bool CanShowCreateShortcutsDialog() OVERRIDE;
  virtual void ShowCreateShortcutsDialog(
      Profile* profile,
      const std::string& extension_id) OVERRIDE;
  virtual void ActivateApp(Profile* profile,
                           const std::string& extension_id,
                           int event_flags) OVERRIDE;
  virtual void LaunchApp(Profile* profile,
                         const std::string& extension_id,
                         int event_flags) OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(AppListControllerDelegateWin);
};

// The AppListController class manages global resources needed for the app
// list to operate, and controls when the app list is opened and closed.
class AppListController {
 public:
  AppListController() : current_view_(NULL) {}
  ~AppListController() {}

  void ShowAppList();
  void CloseAppList();
  void AppListClosing();
  void AppListActivationChanged(bool active);

 private:
  // Utility methods for showing the app list.
  void GetArrowLocationAndUpdateAnchor(
      const gfx::Rect& work_area,
      int min_space_x,
      int min_space_y,
      views::BubbleBorder::ArrowLocation* arrow,
      gfx::Point* anchor);
  void UpdateArrowPositionAndAnchorPoint(app_list::AppListView* view);
  CommandLine GetAppListCommandLine();
  string16 GetAppListIconPath();
  string16 GetAppModelId();

  // Check if the app list or the taskbar has focus. The app list is kept
  // visible whenever either of these have focus, which allows it to be
  // pinned but will hide it if it otherwise loses focus. This is checked
  // periodically whenever the app list does not have focus.
  void CheckTaskbarOrViewHasFocus();

  // Weak pointer. The view manages its own lifetime.
  app_list::AppListView* current_view_;

  // Timer used to check if the taskbar or app list is active. Using a timer
  // means we don't need to hook Windows, which is apparently not possible
  // since Vista (and is not nice at any time).
  base::RepeatingTimer<AppListController> timer_;

  app_list::PaginationModel pagination_model_;

  DISALLOW_COPY_AND_ASSIGN(AppListController);
};

base::LazyInstance<AppListController>::Leaky g_app_list_controller =
    LAZY_INSTANCE_INITIALIZER;

AppListControllerDelegateWin::AppListControllerDelegateWin() {
  browser::StartKeepAlive();
}

AppListControllerDelegateWin::~AppListControllerDelegateWin() {
  browser::EndKeepAlive();
}

void AppListControllerDelegateWin::CloseView() {
  g_app_list_controller.Get().CloseAppList();
}

void AppListControllerDelegateWin::ViewActivationChanged(bool active) {
  g_app_list_controller.Get().AppListActivationChanged(active);
}

void AppListControllerDelegateWin::ViewClosing() {
  g_app_list_controller.Get().AppListClosing();
}

bool AppListControllerDelegateWin::CanPin() {
  return false;
}

bool AppListControllerDelegateWin::CanShowCreateShortcutsDialog() {
  return true;
}

void AppListControllerDelegateWin::ShowCreateShortcutsDialog(
    Profile* profile,
    const std::string& extension_id) {
  ExtensionService* service = profile->GetExtensionService();
  DCHECK(service);
  const extensions::Extension* extension = service->GetInstalledExtension(
      extension_id);
  DCHECK(extension);
  chrome::ShowCreateChromeAppShortcutsDialog(NULL, profile, extension);
}

void AppListControllerDelegateWin::ActivateApp(Profile* profile,
                                               const std::string& extension_id,
                                               int event_flags) {
  LaunchApp(profile, extension_id, event_flags);
}

void AppListControllerDelegateWin::LaunchApp(Profile* profile,
                                             const std::string& extension_id,
                                             int event_flags) {
  ExtensionService* service = profile->GetExtensionService();
  DCHECK(service);
  const extensions::Extension* extension = service->GetInstalledExtension(
      extension_id);
  DCHECK(extension);
  application_launch::OpenApplication(application_launch::LaunchParams(
      profile, extension, extension_misc::LAUNCH_TAB, NEW_FOREGROUND_TAB));
}

void AppListController::ShowAppList() {
#if !defined(USE_AURA)
  // If there is already a view visible, activate it.
  if (current_view_) {
    current_view_->Show();
    return;
  }

  // The controller will be owned by the view delegate, and the delegate is
  // owned by the app list view. The app list view manages it's own lifetime.
  current_view_ = new app_list::AppListView(
      new AppListViewDelegate(new AppListControllerDelegateWin()));
  gfx::Point cursor = gfx::Screen::GetNativeScreen()->GetCursorScreenPoint();
  current_view_->InitAsBubble(GetDesktopWindow(),
                              &pagination_model_,
                              NULL,
                              cursor,
                              views::BubbleBorder::BOTTOM_LEFT);

  UpdateArrowPositionAndAnchorPoint(current_view_);
  HWND hwnd =
      current_view_->GetWidget()->GetTopLevelWidget()->GetNativeWindow();
  ui::win::SetAppIdForWindow(GetAppModelId(), hwnd);
  CommandLine relaunch = GetAppListCommandLine();
  ui::win::SetRelaunchDetailsForWindow(
      relaunch.GetCommandLineString(),
      l10n_util::GetStringUTF16(IDS_APP_LIST_SHORTCUT_NAME),
      hwnd);
  string16 icon_path = GetAppListIconPath();
  ui::win::SetAppIconForWindow(icon_path, hwnd);
  current_view_->Show();
#endif
}

void AppListController::CloseAppList() {
  if (current_view_)
    current_view_->GetWidget()->Close();
}

void AppListController::AppListClosing() {
  current_view_ = NULL;
  timer_.Stop();
}

void AppListController::AppListActivationChanged(bool active) {
  if (active) {
    timer_.Stop();
    return;
  }

  timer_.Start(FROM_HERE, base::TimeDelta::FromSeconds(1), this,
               &AppListController::CheckTaskbarOrViewHasFocus);
}

void AppListController::GetArrowLocationAndUpdateAnchor(
    const gfx::Rect& work_area,
    int min_space_x,
    int min_space_y,
    views::BubbleBorder::ArrowLocation* arrow,
    gfx::Point* anchor) {
  // First ensure anchor is within the work area.
  if (!work_area.Contains(*anchor)) {
    anchor->set_x(std::max(anchor->x(), work_area.x()));
    anchor->set_x(std::min(anchor->x(), work_area.right()));
    anchor->set_y(std::max(anchor->y(), work_area.y()));
    anchor->set_y(std::min(anchor->y(), work_area.bottom()));
  }

  // Prefer the bottom as it is the most natural position.
  if (anchor->y() - work_area.y() >= min_space_y) {
    *arrow = views::BubbleBorder::BOTTOM_LEFT;
    anchor->Offset(0, -kAnchorOffset);
    return;
  }

  // The view won't fit above the cursor. Will it fit below?
  if (work_area.bottom() - anchor->y() >= min_space_y) {
    *arrow = views::BubbleBorder::TOP_LEFT;
    anchor->Offset(0, kAnchorOffset);
    return;
  }

  // As the view won't fit above or below, try on the right.
  if (work_area.right() - anchor->x() >= min_space_x) {
    *arrow = views::BubbleBorder::LEFT_TOP;
    anchor->Offset(kAnchorOffset, 0);
    return;
  }

  *arrow = views::BubbleBorder::RIGHT_TOP;
  anchor->Offset(-kAnchorOffset, 0);
}

void AppListController::UpdateArrowPositionAndAnchorPoint(
    app_list::AppListView* view) {
  static const int kArrowSize = 10;
  static const int kPadding = 20;

  gfx::Size preferred = view->GetPreferredSize();
  // Add the size of the arrow to the space needed, as the preferred size is
  // of the view excluding the arrow.
  int min_space_x = preferred.width() + kAnchorOffset + kPadding + kArrowSize;
  int min_space_y = preferred.height() + kAnchorOffset + kPadding + kArrowSize;

  gfx::Point anchor = view->anchor_point();
  gfx::Display display = gfx::Screen::GetScreenFor(
      view->GetWidget()->GetNativeView())->GetDisplayNearestPoint(anchor);
  const gfx::Rect& display_rect = display.work_area();
  views::BubbleBorder::ArrowLocation arrow;
  GetArrowLocationAndUpdateAnchor(display.work_area(),
                                  min_space_x,
                                  min_space_y,
                                  &arrow,
                                  &anchor);
  view->SetBubbleArrowLocation(arrow);
  view->SetAnchorPoint(anchor);
}

CommandLine AppListController::GetAppListCommandLine() {
  CommandLine* current = CommandLine::ForCurrentProcess();
  CommandLine command_line(current->GetProgram());

  if (current->HasSwitch(switches::kUserDataDir)) {
    FilePath user_data_dir = current->GetSwitchValuePath(
        switches::kUserDataDir);
    command_line.AppendSwitchPath(switches::kUserDataDir, user_data_dir);
  }

  command_line.AppendSwitch(switches::kShowAppList);
  return command_line;
}

string16 AppListController::GetAppListIconPath() {
  FilePath icon_path;
  if (!PathService::Get(base::DIR_MODULE, &icon_path))
    return string16();

  icon_path = icon_path.Append(chrome::kBrowserResourcesDll);
  std::stringstream ss;
  ss << ",-" << IDI_APP_LIST;
  string16 result = icon_path.value();
  result.append(UTF8ToUTF16(ss.str()));
  return result;
}

string16 AppListController::GetAppModelId() {
  static const wchar_t kAppListId[] = L"ChromeAppList";
  // The AppModelId should be the same for all profiles in a user data directory
  // but different for different user data directories, so base it on the
  // initial profile in the current user data directory.
  FilePath initial_profile_path =
      g_browser_process->profile_manager()->GetInitialProfileDir();
  return ShellIntegration::GetAppModelIdForProfile(kAppListId,
                                                   initial_profile_path);
}

void AppListController::CheckTaskbarOrViewHasFocus() {
#if !defined(USE_AURA)
  // Don't bother checking if the view has been closed.
  if (!current_view_)
    return;

  // First get the taskbar and jump lists windows (the jump list is the
  // context menu which the taskbar uses).
  HWND jump_list_hwnd = FindWindow(L"DV2ControlHost", NULL);
  HWND taskbar_hwnd = FindWindow(L"Shell_TrayWnd", NULL);
  HWND app_list_hwnd =
      current_view_->GetWidget()->GetTopLevelWidget()->GetNativeWindow();

  // Get the focused window, and check if it is one of these windows. Keep
  // checking it's parent until either we find one of these windows, or there
  // is no parent left.
  HWND focused_hwnd = GetForegroundWindow();
  while (focused_hwnd) {
    if (focused_hwnd == jump_list_hwnd ||
        focused_hwnd == taskbar_hwnd ||
        focused_hwnd == app_list_hwnd) {
      return;
    }
    focused_hwnd = GetParent(focused_hwnd);
  }

  // If we get here, the focused window is not the taskbar, it's context menu,
  // or the app list, so close the app list.
  CloseAppList();
#endif
}

}  // namespace

namespace app_list_controller {

void ShowAppList() {
  g_app_list_controller.Get().ShowAppList();
}

}  // namespace app_list_controller
