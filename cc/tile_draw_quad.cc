// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/tile_draw_quad.h"

#include "base/logging.h"
#include "third_party/khronos/GLES2/gl2.h"

namespace cc {

scoped_ptr<TileDrawQuad> TileDrawQuad::create(const SharedQuadState* sharedQuadState, const gfx::Rect& quadRect, const gfx::Rect& opaqueRect, unsigned resourceId, const gfx::Point& textureOffset, const gfx::Size& textureSize, GLint textureFilter, bool swizzleContents, bool leftEdgeAA, bool topEdgeAA, bool rightEdgeAA, bool bottomEdgeAA)
{
    return make_scoped_ptr(new TileDrawQuad(sharedQuadState, quadRect, opaqueRect, resourceId, textureOffset, textureSize, textureFilter, swizzleContents, leftEdgeAA, topEdgeAA, rightEdgeAA, bottomEdgeAA));
}

TileDrawQuad::TileDrawQuad(const SharedQuadState* sharedQuadState, const gfx::Rect& quadRect, const gfx::Rect& opaqueRect, unsigned resourceId, const gfx::Point& textureOffset, const gfx::Size& textureSize, GLint textureFilter, bool swizzleContents, bool leftEdgeAA, bool topEdgeAA, bool rightEdgeAA, bool bottomEdgeAA)
    : DrawQuad(sharedQuadState, DrawQuad::TiledContent, quadRect)
    , m_resourceId(resourceId)
    , m_textureOffset(textureOffset)
    , m_textureSize(textureSize)
    , m_textureFilter(textureFilter)
    , m_swizzleContents(swizzleContents)
    , m_leftEdgeAA(leftEdgeAA)
    , m_topEdgeAA(topEdgeAA)
    , m_rightEdgeAA(rightEdgeAA)
    , m_bottomEdgeAA(bottomEdgeAA)
{
    if (isAntialiased())
        m_needsBlending = true;
    m_opaqueRect = opaqueRect;
}

const TileDrawQuad* TileDrawQuad::materialCast(const DrawQuad* quad)
{
    DCHECK(quad->material() == DrawQuad::TiledContent);
    return static_cast<const TileDrawQuad*>(quad);
}

}
