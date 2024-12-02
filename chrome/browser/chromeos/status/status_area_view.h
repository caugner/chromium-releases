// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_VIEW_H_
#pragma once

#include <list>

#include "base/basictypes.h"
#include "base/callback.h"
#include "chrome/browser/chromeos/status/status_area_button.h"
#include "ui/views/accessible_pane_view.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

// This class is used to wrap the small informative widgets in the upper-right
// of the window title bar. It is used on ChromeOS only.
class StatusAreaView : public views::AccessiblePaneView,
                       public views::Widget::Observer,
                       public base::SupportsWeakPtr<StatusAreaView>,
                       public views::WidgetDelegate {
 public:
  enum ButtonBorder {
    NO_BORDER,
    HAS_BORDER
  };

  StatusAreaView();
  virtual ~StatusAreaView();

  void AddButton(StatusAreaButton* button, ButtonBorder border);
  void RemoveButton(StatusAreaButton* button);

  void MakeButtonsActive(bool active);
  void UpdateButtonVisibility();

  // Refresh the style used to paint all buttons' text.  Schedules repaint.
  void UpdateButtonTextStyle();

  // Takes focus and transfers it to the first (last if |reverse| is true).
  // After focus has traversed through all elements, clears focus and calls
  // |return_focus_cb(reverse)| from the message loop.
  typedef base::Callback<void(bool)> ReturnFocusCallback;
  void TakeFocus(bool reverse,
                 const ReturnFocusCallback& return_focus_cb);

  // views::View* overrides.
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual void PreferredSizeChanged() OVERRIDE;
  virtual void ChildPreferredSizeChanged(views::View* child) OVERRIDE;

  // views::WidgetDelegate overrides:
  virtual bool CanActivate() const OVERRIDE;
  virtual void DeleteDelegate() OVERRIDE;
  virtual views::Widget* GetWidget() OVERRIDE;
  virtual const views::Widget* GetWidget() const OVERRIDE;

 private:
  // Overridden from views::FocusChangeListener:
  virtual void OnDidChangeFocus(views::View* focused_before,
                                views::View* focused_now) OVERRIDE;

  // Overriden from views::Widget::Observer:
  virtual void OnWidgetActivationChanged(views::Widget* widget,
                                         bool active) OVERRIDE;

  // Overriden from views::AccessiblePaneView:
  virtual bool AcceleratorPressed(const ui::Accelerator& accelerator)
      OVERRIDE;

  StatusAreaButton::Delegate* delegate_;

  // True if focus needs to be returned via |return_focus_cb_| when it wraps.
  bool need_return_focus_;
  // True if focus return should be skipped for next focus change.
  bool skip_next_focus_return_;
  ReturnFocusCallback return_focus_cb_;

  std::list<StatusAreaButton*> buttons_;

  // Clears focus and calls |return_focus_cb_|.
  void ReturnFocus(bool reverse);

  // Clears focus (called when widget is deactivated).
  void ClearFocus();

  DISALLOW_COPY_AND_ASSIGN(StatusAreaView);
};

#endif  // CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_VIEW_H_
