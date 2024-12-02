// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/panel_layout_manager.h"

#include <algorithm>

#include "ash/launcher/launcher.h"
#include "ash/shell.h"
#include "ash/wm/property_util.h"
#include "base/auto_reset.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/size.h"
#include "ui/views/widget/widget.h"

namespace {
const int kPanelMarginEdge = 4;
const int kPanelMarginMiddle = 8;

const int kMinimizedHeight = 24;

const float kMaxHeightFactor = .80f;
const float kMaxWidthFactor = .50f;
}

namespace ash {
namespace internal {

////////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager public implementation:

PanelLayoutManager::PanelLayoutManager(aura::Window* panel_container)
    : panel_container_(panel_container),
      in_layout_(false),
      dragged_panel_(NULL) {
  DCHECK(panel_container);
}

PanelLayoutManager::~PanelLayoutManager() {
}

void PanelLayoutManager::StartDragging(aura::Window* panel) {
  DCHECK(dragged_panel_ == NULL);
  DCHECK(panel->parent() == panel_container_);
  dragged_panel_ = panel;
}

void PanelLayoutManager::FinishDragging() {
  DCHECK(dragged_panel_ != NULL);
  dragged_panel_ = NULL;
  Relayout();
}

void PanelLayoutManager::ToggleMinimize(aura::Window* panel) {
  DCHECK(panel->parent() == panel_container_);
  if (panel->GetProperty(aura::client::kShowStateKey) ==
      ui::SHOW_STATE_MINIMIZED) {
    const gfx::Rect& old_bounds = panel->bounds();
    panel->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);

    gfx::Rect new_bounds(old_bounds);
    const gfx::Rect* restore_bounds = GetRestoreBounds(panel);
    if (restore_bounds != NULL) {
      new_bounds.set_height(restore_bounds->height());
      new_bounds.set_y(old_bounds.bottom() - restore_bounds->height());
      SetChildBounds(panel, new_bounds);
      ClearRestoreBounds(panel);
    }
  } else {
    const gfx::Rect& old_bounds = panel->bounds();
    panel->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
    SetRestoreBounds(panel, old_bounds);
    SetChildBounds(panel,
                   gfx::Rect(old_bounds.x(),
                             old_bounds.bottom() - kMinimizedHeight,
                             old_bounds.width(),
                             kMinimizedHeight));
  }
  Relayout();
}

////////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager, aura::LayoutManager implementation:

void PanelLayoutManager::OnWindowResized() {
  Relayout();
}

void PanelLayoutManager::OnWindowAddedToLayout(aura::Window* child) {
  panel_windows_.push_back(child);
  Relayout();
}

void PanelLayoutManager::OnWillRemoveWindowFromLayout(aura::Window* child) {
  PanelList::iterator found =
      std::find(panel_windows_.begin(), panel_windows_.end(), child);
  if (found != panel_windows_.end())
    panel_windows_.erase(found);

  if (dragged_panel_ == child)
    dragged_panel_ = NULL;

  Relayout();
}

void PanelLayoutManager::OnChildWindowVisibilityChanged(aura::Window* child,
                                                        bool visible) {
  Relayout();
}

void PanelLayoutManager::SetChildBounds(aura::Window* child,
                                        const gfx::Rect& requested_bounds) {
  gfx::Rect bounds(requested_bounds);
  const gfx::Rect& max_bounds = panel_container_->GetRootWindow()->bounds();
  const int max_width = max_bounds.width() * kMaxWidthFactor;
  const int max_height = max_bounds.height() * kMaxHeightFactor;
  if (bounds.width() > max_width)
    bounds.set_width(max_width);
  if (bounds.height() > max_height)
    bounds.set_height(max_height);

  // Reposition dragged panel in the panel order.
  if (dragged_panel_ == child) {
    PanelList::iterator dragged_panel_iter =
      std::find(panel_windows_.begin(), panel_windows_.end(), dragged_panel_);
    DCHECK(dragged_panel_iter != panel_windows_.end());
    PanelList::iterator new_position;
    for (new_position = panel_windows_.begin();
         new_position != panel_windows_.end();
         ++new_position) {
      const gfx::Rect& bounds = (*new_position)->bounds();
      if (bounds.x() + bounds.width()/2 <= requested_bounds.x()) break;
    }
    if (new_position != dragged_panel_iter) {
      panel_windows_.erase(dragged_panel_iter);
      panel_windows_.insert(new_position, dragged_panel_);
    }
  }

  SetChildBoundsDirect(child, bounds);
  Relayout();
}

////////////////////////////////////////////////////////////////////////////////
// PanelLayoutManager private implementation:

// This is a rough outline of a simple panel layout manager.
void PanelLayoutManager::Relayout() {
  if (in_layout_)
    return;
  AutoReset<bool> auto_reset_in_layout(&in_layout_, true);

  // Panels are currently laid out just above the launcher (if it exists),
  // otherwise at the bottom of the root window.
  int right, bottom;
  ash::Shell* shell = ash::Shell::GetInstance();
  if (shell->launcher() && shell->launcher()->widget()->IsVisible()) {
    const gfx::Rect& bounds =
        shell->launcher()->widget()->GetWindowScreenBounds();
    right = bounds.width() - 1 - kPanelMarginEdge;
    bottom = bounds.y() - 1;
  } else {
    const gfx::Rect& bounds = panel_container_->GetRootWindow()->bounds();
    right = bounds.width() - 1 - kPanelMarginEdge;
    bottom = bounds.bottom() - 1;
  }

  // Layout the panel windows right to left.
  for (PanelList::iterator iter = panel_windows_.begin();
       iter != panel_windows_.end(); ++iter) {
    aura::Window* panel_win = *iter;
    if (!panel_win->IsVisible())
      continue;
    int x = right - panel_win->bounds().width();
    int y = bottom - panel_win->bounds().height();

    // Do not relayout dragged panel, but pretend it is in place
    if (panel_win != dragged_panel_) {
      gfx::Rect bounds(x, y,
                       panel_win->bounds().width(),
                       panel_win->bounds().height());
      SetChildBoundsDirect(panel_win, bounds);
    }
    right = x - kPanelMarginMiddle;
  }
}

}  // namespace internal
}  // namespace ash
