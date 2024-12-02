// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_EXTENSIONS_SHELL_WINDOW_COCOA_H_
#define CHROME_BROWSER_UI_COCOA_EXTENSIONS_SHELL_WINDOW_COCOA_H_

#import <Cocoa/Cocoa.h>
#include <vector>

#include "base/memory/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#import "chrome/browser/ui/cocoa/browser_command_executor.h"
#include "chrome/browser/ui/cocoa/constrained_window_mac.h"
#include "chrome/browser/ui/extensions/native_shell_window.h"
#include "chrome/browser/ui/extensions/shell_window.h"
#include "chrome/common/extensions/draggable_region.h"
#import "third_party/GTM/AppKit/GTMWindowSheetController.h"
#include "ui/gfx/rect.h"

class ExtensionKeybindingRegistryCocoa;
class Profile;
class ShellWindowCocoa;
@class ShellNSWindow;
class SkRegion;

// A window controller for a minimal window to host a web app view. Passes
// Objective-C notifications to the C++ bridge.
@interface ShellWindowController : NSWindowController
                                  <NSWindowDelegate,
                                   GTMWindowSheetControllerDelegate,
                                   ConstrainedWindowSupport,
                                   BrowserCommandExecutor> {
 @private
  ShellWindowCocoa* shellWindow_;  // Weak; owns self.
  // Manages per-window sheets.
  scoped_nsobject<GTMWindowSheetController> sheetController_;
}

@property(assign, nonatomic) ShellWindowCocoa* shellWindow;

// Consults the Command Registry to see if this |event| needs to be handled as
// an extension command and returns YES if so (NO otherwise).
- (BOOL)handledByExtensionCommand:(NSEvent*)event;

@end

// Cocoa bridge to ShellWindow.
class ShellWindowCocoa : public NativeShellWindow {
 public:
  ShellWindowCocoa(ShellWindow* shell_window,
      const ShellWindow::CreateParams& params);

  // BaseWindow implementation.
  virtual bool IsActive() const OVERRIDE;
  virtual bool IsMaximized() const OVERRIDE;
  virtual bool IsMinimized() const OVERRIDE;
  virtual bool IsFullscreen() const OVERRIDE;
  virtual gfx::NativeWindow GetNativeWindow() OVERRIDE;
  virtual gfx::Rect GetRestoredBounds() const OVERRIDE;
  virtual gfx::Rect GetBounds() const OVERRIDE;
  virtual void Show() OVERRIDE;
  virtual void ShowInactive() OVERRIDE;
  virtual void Hide() OVERRIDE;
  virtual void Close() OVERRIDE;
  virtual void Activate() OVERRIDE;
  virtual void Deactivate() OVERRIDE;
  virtual void Maximize() OVERRIDE;
  virtual void Minimize() OVERRIDE;
  virtual void Restore() OVERRIDE;
  virtual void SetBounds(const gfx::Rect& bounds) OVERRIDE;
  virtual void FlashFrame(bool flash) OVERRIDE;
  virtual bool IsAlwaysOnTop() const OVERRIDE;

  // Called when the window is about to be closed.
  void WindowWillClose();

  // Called when the window is focused.
  void WindowDidBecomeKey();

  // Called when the window is defocused.
  void WindowDidResignKey();

  // Called when the window is resized.
  void WindowDidResize();

  // Called when the window is moved.
  void WindowDidMove();

  // Called to handle a key event.
  bool HandledByExtensionCommand(NSEvent* event);

  // Called to handle a mouse event.
  void HandleMouseEvent(NSEvent* event);

  bool use_system_drag() const { return use_system_drag_; }
  SkRegion* draggable_region() const { return draggable_region_.get(); }

 protected:
  // NativeShellWindow implementation.
  virtual void SetFullscreen(bool fullscreen) OVERRIDE;
  virtual bool IsFullscreenOrPending() const OVERRIDE;
  virtual void UpdateWindowIcon() OVERRIDE;
  virtual void UpdateWindowTitle() OVERRIDE;
  virtual void UpdateDraggableRegions(
      const std::vector<extensions::DraggableRegion>& regions) OVERRIDE;
  virtual void HandleKeyboardEvent(
      const content::NativeWebKeyboardEvent& event) OVERRIDE;
  virtual void RenderViewHostChanged() OVERRIDE {}

 private:
  virtual ~ShellWindowCocoa();

  ShellNSWindow* window() const;

  content::WebContents* web_contents() const {
    return shell_window_->web_contents();
  }
  const extensions::Extension* extension() const {
    return shell_window_->extension();
  }

  void InstallView();
  void UninstallView();
  void InstallDraggableRegionViews();
  void UpdateDraggableRegionsForSystemDrag(
      const std::vector<extensions::DraggableRegion>& regions,
      const extensions::DraggableRegion* draggable_area);
  void UpdateDraggableRegionsForCustomDrag(
      const std::vector<extensions::DraggableRegion>& regions);

  ShellWindow* shell_window_; // weak - ShellWindow owns NativeShellWindow.

  bool has_frame_;

  bool is_fullscreen_;
  NSRect restored_bounds_;

  gfx::Size min_size_;
  gfx::Size max_size_;

  scoped_nsobject<ShellWindowController> window_controller_;
  NSInteger attention_request_id_;  // identifier from requestUserAttention

  // Indicates whether system drag or custom drag should be used, depending on
  // the complexity of draggable regions.
  bool use_system_drag_;

  // For system drag, the whole window is draggable and the non-draggable areas
  // have to been explicitly excluded.
  std::vector<gfx::Rect> system_drag_exclude_areas_;

  // For custom drag, the whole window is non-draggable and the draggable region
  // has to been explicitly provided.
  scoped_ptr<SkRegion> draggable_region_;  // used in custom drag.

  // Mouse location since the last mouse event, in screen coordinates. This is
  // used in custom drag to compute the window movement.
  NSPoint last_mouse_location_;

  // The Extension Command Registry used to determine which keyboard events to
  // handle.
  scoped_ptr<ExtensionKeybindingRegistryCocoa> extension_keybinding_registry_;

  DISALLOW_COPY_AND_ASSIGN(ShellWindowCocoa);
};

#endif  // CHROME_BROWSER_UI_COCOA_EXTENSIONS_SHELL_WINDOW_COCOA_H_
