// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/texture_layer_impl.h"

#include "base/stringprintf.h"
#include "cc/quad_sink.h"
#include "cc/renderer.h"
#include "cc/texture_draw_quad.h"

namespace cc {

TextureLayerImpl::TextureLayerImpl(int id)
    : LayerImpl(id)
    , m_textureId(0)
    , m_externalTextureResource(0)
    , m_premultipliedAlpha(true)
    , m_flipped(true)
    , m_uvRect(0, 0, 1, 1)
{
}

TextureLayerImpl::~TextureLayerImpl()
{
}

void TextureLayerImpl::willDraw(ResourceProvider* resourceProvider)
{
    if (!m_textureId)
        return;
    DCHECK(!m_externalTextureResource);
    m_externalTextureResource = resourceProvider->createResourceFromExternalTexture(m_textureId);
}

void TextureLayerImpl::appendQuads(QuadSink& quadSink, AppendQuadsData& appendQuadsData)
{
    if (!m_externalTextureResource)
        return;

    SharedQuadState* sharedQuadState = quadSink.useSharedQuadState(createSharedQuadState());
    appendDebugBorderQuad(quadSink, sharedQuadState, appendQuadsData);

    IntRect quadRect(IntPoint(), contentBounds());
    quadSink.append(TextureDrawQuad::create(sharedQuadState, quadRect, m_externalTextureResource, m_premultipliedAlpha, m_uvRect, m_flipped).PassAs<DrawQuad>(), appendQuadsData);
}

void TextureLayerImpl::didDraw(ResourceProvider* resourceProvider)
{
    if (!m_externalTextureResource)
        return;
    // FIXME: the following assert will not be true when sending resources to a
    // parent compositor. A synchronization scheme (double-buffering or
    // pipelining of updates) for the client will need to exist to solve this.
    DCHECK(!resourceProvider->inUseByConsumer(m_externalTextureResource));
    resourceProvider->deleteResource(m_externalTextureResource);
    m_externalTextureResource = 0;
}

void TextureLayerImpl::dumpLayerProperties(std::string* str, int indent) const
{
    str->append(indentString(indent));
    base::StringAppendF(str, "texture layer texture id: %u premultiplied: %d\n", m_textureId, m_premultipliedAlpha);
    LayerImpl::dumpLayerProperties(str, indent);
}

void TextureLayerImpl::didLoseContext()
{
    m_textureId = 0;
    m_externalTextureResource = 0;
}

const char* TextureLayerImpl::layerTypeAsString() const
{
    return "TextureLayer";
}

}
