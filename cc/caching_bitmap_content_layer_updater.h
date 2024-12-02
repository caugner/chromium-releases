// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CACHING_BITMAP_CANVAS_LAYER_UPDATER_H_
#define CACHING_BITMAP_CANVAS_LAYER_UPDATER_H_

#include "base/compiler_specific.h"
#include "cc/bitmap_content_layer_updater.h"

namespace cc {

class CachingBitmapContentLayerUpdater
    : public BitmapContentLayerUpdater {
 public:
  static scoped_refptr<CachingBitmapContentLayerUpdater> Create(
      scoped_ptr<LayerPainter>);

  virtual void prepareToUpdate(const IntRect& content_rect,
                               const IntSize& tile_size,
                               float contents_width_scale,
                               float contents_height_scale,
                               IntRect& resulting_opaque_rect,
                               RenderingStats&) OVERRIDE;

  bool pixelsDidChange() const;

 private:
  explicit CachingBitmapContentLayerUpdater(
      scoped_ptr<LayerPainter> painter);
  virtual ~CachingBitmapContentLayerUpdater();

  bool pixels_did_change_;
  SkBitmap cached_bitmap_;
};

}  // namespace cc

#endif  // CACHING_BITMAP_CANVAS_LAYER_UPDATER_H_
