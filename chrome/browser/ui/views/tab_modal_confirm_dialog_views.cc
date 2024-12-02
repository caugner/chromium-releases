// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tab_modal_confirm_dialog_views.h"

#include "base/command_line.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/tab_modal_confirm_dialog_delegate.h"
#include "chrome/browser/ui/views/constrained_window_views.h"
#include "chrome/common/chrome_switches.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/message_box_view.h"
#include "ui/views/window/dialog_client_view.h"

// static
TabModalConfirmDialog* TabModalConfirmDialog::Create(
    TabModalConfirmDialogDelegate* delegate,
    TabContents* tab_contents) {
  return new TabModalConfirmDialogViews(
      delegate,
      tab_contents,
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableFramelessConstrainedDialogs));
}

namespace {

const int kChromeStyleInterRowVerticalSpacing = 17;

views::MessageBoxView::InitParams CreateMessageBoxViewInitParams(
    const string16& message,
    bool enable_chrome_style) {
  views::MessageBoxView::InitParams params(message);

  if (enable_chrome_style) {
    params.top_inset = 0;
    params.bottom_inset = 0;
    params.left_inset = 0;
    params.right_inset = 0;

    params.inter_row_vertical_spacing = kChromeStyleInterRowVerticalSpacing;
  }

  return params;
}

}  // namespace

//////////////////////////////////////////////////////////////////////////////
// TabModalConfirmDialogViews, constructor & destructor:

TabModalConfirmDialogViews::TabModalConfirmDialogViews(
    TabModalConfirmDialogDelegate* delegate,
    TabContents* tab_contents,
    bool enable_chrome_style)
    : delegate_(delegate),
      message_box_view_(new views::MessageBoxView(
          CreateMessageBoxViewInitParams(delegate->GetMessage(),
                                         enable_chrome_style))),
      enable_chrome_style_(enable_chrome_style) {
  delegate_->set_window(new ConstrainedWindowViews(
      tab_contents->web_contents(), this, enable_chrome_style,
      ConstrainedWindowViews::DEFAULT_INSETS));
}

TabModalConfirmDialogViews::~TabModalConfirmDialogViews() {
}

void TabModalConfirmDialogViews::AcceptTabModalDialog() {
  GetDialogClientView()->AcceptWindow();
}

void TabModalConfirmDialogViews::CancelTabModalDialog() {
  GetDialogClientView()->CancelWindow();
}

//////////////////////////////////////////////////////////////////////////////
// TabModalConfirmDialogViews, views::DialogDelegate implementation:

string16 TabModalConfirmDialogViews::GetWindowTitle() const {
  return delegate_->GetTitle();
}

string16 TabModalConfirmDialogViews::GetDialogButtonLabel(
    ui::DialogButton button) const {
  if (button == ui::DIALOG_BUTTON_OK)
    return delegate_->GetAcceptButtonTitle();
  if (button == ui::DIALOG_BUTTON_CANCEL)
    return delegate_->GetCancelButtonTitle();
  return string16();
}

bool TabModalConfirmDialogViews::UseChromeStyle() const {
  return enable_chrome_style_;
}

bool TabModalConfirmDialogViews::Cancel() {
  delegate_->Cancel();
  return true;
}

bool TabModalConfirmDialogViews::Accept() {
  delegate_->Accept();
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// TabModalConfirmDialogViews, views::WidgetDelegate implementation:

views::View* TabModalConfirmDialogViews::GetContentsView() {
  return message_box_view_;
}

views::Widget* TabModalConfirmDialogViews::GetWidget() {
  return message_box_view_->GetWidget();
}

const views::Widget* TabModalConfirmDialogViews::GetWidget() const {
  return message_box_view_->GetWidget();
}

void TabModalConfirmDialogViews::DeleteDelegate() {
  delete this;
}
