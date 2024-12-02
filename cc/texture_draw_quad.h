// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CCTextureDrawQuad_h
#define CCTextureDrawQuad_h

#include "base/memory/scoped_ptr.h"
#include "cc/draw_quad.h"
#include "ui/gfx/rect_f.h"

namespace cc {

#pragma pack(push, 4)

class TextureDrawQuad : public DrawQuad {
public:
    static scoped_ptr<TextureDrawQuad> create(const SharedQuadState*, const gfx::Rect&, unsigned resourceId, bool premultipliedAlpha, const gfx::RectF& uvRect, bool flipped);
    gfx::RectF uvRect() const { return m_uvRect; }

    unsigned resourceId() const { return m_resourceId; }
    bool premultipliedAlpha() const { return  m_premultipliedAlpha; }
    bool flipped() const { return m_flipped; }

    void setNeedsBlending();

    static const TextureDrawQuad* materialCast(const DrawQuad*);
private:
    TextureDrawQuad(const SharedQuadState*, const gfx::Rect&, unsigned resourceId, bool premultipliedAlpha, const gfx::RectF& uvRect, bool flipped);

    unsigned m_resourceId;
    bool m_premultipliedAlpha;
    gfx::RectF m_uvRect;
    bool m_flipped;
};

#pragma pack(pop)

}

#endif
