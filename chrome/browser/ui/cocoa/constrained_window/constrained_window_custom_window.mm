// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_window.h"

#import "base/memory/scoped_nsobject.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_sheet_controller.h"
#import "chrome/browser/ui/constrained_window.h"
#import "chrome/browser/ui/constrained_window_constants.h"
#include "skia/ext/skia_utils_mac.h"

// The content view for the custom window.
@interface ConstrainedWindowCustomWindowContentView : NSView
@end

@implementation ConstrainedWindowCustomWindow

- (id)initWithContentRect:(NSRect)contentRect {
  if ((self = [super initWithContentRect:contentRect
                               styleMask:NSBorderlessWindowMask
                                 backing:NSBackingStoreBuffered
                                   defer:NO])) {
    [self setHasShadow:YES];
    [self setBackgroundColor:[NSColor clearColor]];
    [self setOpaque:NO];
    [self setReleasedWhenClosed:NO];
    scoped_nsobject<NSView> contentView(
        [[ConstrainedWindowCustomWindowContentView alloc]
            initWithFrame:NSZeroRect]);
    [self setContentView:contentView];
  }
  return self;
}

- (BOOL)canBecomeKeyWindow {
  return YES;
}

- (NSRect)frameRectForContentRect:(NSRect)windowContent {
  ConstrainedWindowSheetController* sheetController =
      [ConstrainedWindowSheetController controllerForSheet:self];
  NSRect frame;
  frame.origin = [sheetController originForSheet:self
                                  withWindowSize:windowContent.size];
  frame.size = windowContent.size;
  return frame;
}

@end

@implementation ConstrainedWindowCustomWindowContentView

- (void)drawRect:(NSRect)rect {
  NSBezierPath* path = [NSBezierPath
      bezierPathWithRoundedRect:[self bounds]
                        xRadius:ConstrainedWindowConstants::kBorderRadius
                        yRadius:ConstrainedWindowConstants::kBorderRadius];
  [gfx::SkColorToCalibratedNSColor(
      ConstrainedWindow::GetBackgroundColor()) set];
  [path fill];

  [[self window] invalidateShadow];
}

@end
