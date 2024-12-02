// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_alert.h"

#import "base/logging.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_button.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_control_utils.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_window.h"
#import "chrome/browser/ui/cocoa/hover_close_button.h"
#import "chrome/browser/ui/constrained_window.h"
#import "chrome/browser/ui/constrained_window_constants.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/GTM/AppKit/GTMUILocalizerAndLayoutTweaker.h"
#include "ui/base/cocoa/window_size_constants.h"

namespace {

const CGFloat kWindowMinWidth = 500;
const CGFloat kButtonGap = 6;

}  // namespace

@interface ConstrainedWindowAlert ()
// Position the alert buttons within the given window width.
- (void)layoutButtonsWithWindowWidth:(CGFloat)windowWidth;
// Resize the given text field to fit within the window and position it starting
// at |yPos|. Returns the new value of yPos.
- (CGFloat)layoutTextField:(NSTextField*)textField
                      yPos:(CGFloat)yPos
               windowWidth:(CGFloat)windowWidth;
// Positions the accessory view starting at yPos. Returns the new value of yPos.
- (CGFloat)layoutAccessoryViewAtYPos:(CGFloat)yPos;
// Update the position of the close button.
- (void)layoutCloseButtonWithWindowWidth:(CGFloat)windowWidth;
@end

@implementation ConstrainedWindowAlert

- (id)init {
  if ((self = [super init])) {
    window_.reset([[ConstrainedWindowCustomWindow alloc]
        initWithContentRect:ui::kWindowSizeDeterminedLater]);
    NSView* contentView = [window_ contentView];

    informativeTextField_.reset([constrained_window::CreateLabel() retain]);
    [contentView addSubview:informativeTextField_];
    messageTextField_.reset([constrained_window::CreateLabel() retain]);
    [contentView addSubview:messageTextField_];

    closeButton_.reset(
        [[WebUIHoverCloseButton alloc] initWithFrame:NSZeroRect]);
    [contentView addSubview:closeButton_];
  }
  return self;
}

- (NSString*)informativeText {
  return [informativeTextField_ stringValue];
}

- (void)setInformativeText:(NSString*)string {
  [informativeTextField_ setAttributedStringValue:
      constrained_window::GetAttributedLabelString(
          string,
          ConstrainedWindowConstants::kTextFontStyle,
          NSNaturalTextAlignment,
          NSLineBreakByWordWrapping)];
}

- (NSString*)messageText {
  return [messageTextField_ stringValue];
}

- (void)setMessageText:(NSString*)string {
  [messageTextField_ setAttributedStringValue:
      constrained_window::GetAttributedLabelString(
          string,
          ConstrainedWindowConstants::kTitleFontStyle,
          NSNaturalTextAlignment,
          NSLineBreakByWordWrapping)];
}

- (NSView*)accessoryView {
  return accessoryView_;
}

- (void)setAccessoryView:(NSView*)accessoryView {
  [accessoryView_ removeFromSuperview];
  accessoryView_.reset([accessoryView retain]);
  [[window_ contentView] addSubview:accessoryView_];
}

- (NSArray*)buttons {
  return buttons_;
}

- (NSButton*)closeButton {
  return closeButton_;
}

- (NSWindow*)window {
  return window_;
}

- (void)addButtonWithTitle:(NSString*)title
             keyEquivalent:(NSString*)keyEquivalent
                    target:(id)target
                    action:(SEL)action {
  if (!buttons_.get())
    buttons_.reset([[NSMutableArray alloc] init]);
  scoped_nsobject<NSButton> button(
      [[ConstrainedWindowButton alloc] initWithFrame:NSZeroRect]);
  [button setTitle:title];
  [button setKeyEquivalent:keyEquivalent];
  [button setTarget:target];
  [button setAction:action];
  [buttons_ addObject:button];
  [[window_ contentView] addSubview:button];
}

- (void)layout {
  // Button width.
  CGFloat buttonWidth = 0;
  for (NSButton* button in buttons_.get()) {
    [button sizeToFit];
    NSSize size = [button frame].size;
    buttonWidth += size.width;
  }
  if ([buttons_ count])
    buttonWidth += ([buttons_ count] - 1) * kButtonGap;

  // Window width.
  CGFloat windowWidth = buttonWidth;
  if (accessoryView_.get())
    windowWidth = std::max(windowWidth, NSWidth([accessoryView_ frame]));
  windowWidth += ConstrainedWindowConstants::kHorizontalPadding * 2;
  windowWidth = std::max(windowWidth, kWindowMinWidth);

  // Layout controls.
  [self layoutButtonsWithWindowWidth:windowWidth];
  CGFloat curY = [buttons_ count] ? NSMaxY([[buttons_ lastObject] frame])
      : ConstrainedWindowConstants::kClientBottomPadding;
  curY = [self layoutAccessoryViewAtYPos:curY];
  curY = [self layoutTextField:informativeTextField_
                          yPos:curY
                   windowWidth:windowWidth];
  CGFloat availableMessageWidth =
      windowWidth - ConstrainedWindow::GetCloseButtonSize() - kButtonGap;
  curY = [self layoutTextField:messageTextField_
                          yPos:curY
                   windowWidth:availableMessageWidth];
  [self layoutCloseButtonWithWindowWidth:windowWidth];

  // Update window frame.
  curY += ConstrainedWindowConstants::kTitleTopPadding;
  [window_ setFrame:NSMakeRect(0, 0, windowWidth, curY)
            display:NO];
}

- (void)layoutButtonsWithWindowWidth:(CGFloat)windowWidth {
  // Layout first 2 button right to left.
  CGFloat curX = windowWidth - ConstrainedWindowConstants::kHorizontalPadding;
  const int buttonCount = [buttons_ count];
  for (int i = 0; i < std::min(2, buttonCount); ++i) {
    NSButton* button = [buttons_ objectAtIndex:i];
    NSRect rect = [button frame];
    rect.origin.x = curX - NSWidth(rect);
    rect.origin.y = ConstrainedWindowConstants::kClientBottomPadding;
    [button setFrameOrigin:rect.origin];
    curX = NSMinX(rect) - kButtonGap;
  }

  // Layout remaining buttons left to right.
  curX = ConstrainedWindowConstants::kHorizontalPadding;
  for (int i = buttonCount - 1; i >= 2; --i) {
    NSButton* button = [buttons_ objectAtIndex:i];
    [button setFrameOrigin:
        NSMakePoint(curX, ConstrainedWindowConstants::kClientBottomPadding)];
    curX += NSMaxX([button frame]) + kButtonGap;
  }
}

- (CGFloat)layoutTextField:(NSTextField*)textField
                      yPos:(CGFloat)yPos
               windowWidth:(CGFloat)windowWidth {
  if (![[textField stringValue] length]) {
    [textField setHidden:YES];
    return yPos;
  }

  [textField setHidden:NO];
  NSRect rect;
  rect.origin.y = yPos + ConstrainedWindowConstants::kRowPadding;
  rect.origin.x = ConstrainedWindowConstants::kHorizontalPadding;
  rect.size.width = windowWidth -
      ConstrainedWindowConstants::kHorizontalPadding * 2;
  rect.size.height = 1;
  [textField setFrame:rect];
  [GTMUILocalizerAndLayoutTweaker sizeToFitFixedWidthTextField:textField];
  return NSMaxY([textField frame]);
}

- (CGFloat)layoutAccessoryViewAtYPos:(CGFloat)yPos {
  if (!accessoryView_.get())
    return yPos;
  NSRect frame = [accessoryView_ frame];
  frame.origin.y = yPos + ConstrainedWindowConstants::kRowPadding;
  frame.origin.x = ConstrainedWindowConstants::kHorizontalPadding;
  [accessoryView_ setFrameOrigin:frame.origin];
  return NSMaxY(frame);
}

- (void)layoutCloseButtonWithWindowWidth:(CGFloat)windowWidth {
  NSRect frame;
  frame.size.width = ConstrainedWindow::GetCloseButtonSize();
  frame.size.height = ConstrainedWindow::GetCloseButtonSize();
  frame.origin.x = windowWidth -
      ConstrainedWindowConstants::kHorizontalPadding - NSWidth(frame);
  frame.origin.y = NSMaxY([messageTextField_ frame]) - NSHeight(frame);
  [closeButton_ setFrame:frame];
}

@end
