// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CCSharedQuadState_h
#define CCSharedQuadState_h

#include "base/memory/scoped_ptr.h"
#include "ui/gfx/rect.h"
#include <public/WebTransformationMatrix.h>

namespace cc {

struct SharedQuadState {
    int id;

    // Transforms from quad's original content space to its target content space.
    WebKit::WebTransformationMatrix quadTransform;
    // This rect lives in the content space for the quad's originating layer.
    gfx::Rect visibleContentRect;
    gfx::Rect clippedRectInTarget;
    float opacity;
    bool opaque;

    static scoped_ptr<SharedQuadState> create(const WebKit::WebTransformationMatrix& quadTransform, const gfx::Rect& visibleContentRect, const gfx::Rect& clippedRectInTarget, float opacity, bool opaque);
    SharedQuadState(const WebKit::WebTransformationMatrix& quadTransform, const gfx::Rect& visibleContentRect, const gfx::Rect& clippedRectInTarget, float opacity, bool opaque);

    scoped_ptr<SharedQuadState> copy() const;
};

}

#endif
