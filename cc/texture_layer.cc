// Copyright 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/texture_layer.h"

#include "third_party/khronos/GLES2/gl2.h"
#include "cc/layer_tree_host.h"
#include "cc/texture_layer_client.h"
#include "cc/texture_layer_impl.h"

namespace cc {

scoped_refptr<TextureLayer> TextureLayer::create(TextureLayerClient* client)
{
    return scoped_refptr<TextureLayer>(new TextureLayer(client));
}

TextureLayer::TextureLayer(TextureLayerClient* client)
    : Layer()
    , m_client(client)
    , m_flipped(true)
    , m_uvRect(0, 0, 1, 1)
    , m_premultipliedAlpha(true)
    , m_rateLimitContext(false)
    , m_contextLost(false)
    , m_textureId(0)
    , m_contentCommitted(false)
{
}

TextureLayer::~TextureLayer()
{
    if (layerTreeHost()) {
        if (m_textureId)
            layerTreeHost()->acquireLayerTextures();
        if (m_rateLimitContext && m_client)
            layerTreeHost()->stopRateLimiter(m_client->context());
    }
}

scoped_ptr<LayerImpl> TextureLayer::createLayerImpl()
{
    return TextureLayerImpl::create(m_layerId).PassAs<LayerImpl>();
}

void TextureLayer::setFlipped(bool flipped)
{
    m_flipped = flipped;
    setNeedsCommit();
}

void TextureLayer::setUVRect(const FloatRect& rect)
{
    m_uvRect = rect;
    setNeedsCommit();
}

void TextureLayer::setPremultipliedAlpha(bool premultipliedAlpha)
{
    m_premultipliedAlpha = premultipliedAlpha;
    setNeedsCommit();
}

void TextureLayer::setRateLimitContext(bool rateLimit)
{
    if (!rateLimit && m_rateLimitContext && m_client && layerTreeHost())
        layerTreeHost()->stopRateLimiter(m_client->context());

    m_rateLimitContext = rateLimit;
}

void TextureLayer::setTextureId(unsigned id)
{
    if (m_textureId == id)
        return;
    if (m_textureId && layerTreeHost())
        layerTreeHost()->acquireLayerTextures();
    m_textureId = id;
    setNeedsCommit();
}

void TextureLayer::willModifyTexture()
{
    if (layerTreeHost() && (drawsContent() || m_contentCommitted)) {
        layerTreeHost()->acquireLayerTextures();
        m_contentCommitted = false;
    }
}

void TextureLayer::setNeedsDisplayRect(const FloatRect& dirtyRect)
{
    Layer::setNeedsDisplayRect(dirtyRect);

    if (m_rateLimitContext && m_client && layerTreeHost() && drawsContent())
        layerTreeHost()->startRateLimiter(m_client->context());
}

void TextureLayer::setLayerTreeHost(LayerTreeHost* host)
{
    if (m_textureId && layerTreeHost() && host != layerTreeHost())
        layerTreeHost()->acquireLayerTextures();
    Layer::setLayerTreeHost(host);
}

bool TextureLayer::drawsContent() const
{
    return (m_client || m_textureId) && !m_contextLost && Layer::drawsContent();
}

void TextureLayer::update(ResourceUpdateQueue& queue, const OcclusionTracker*, RenderingStats&)
{
    if (m_client) {
        m_textureId = m_client->prepareTexture(queue);
        m_contextLost = m_client->context()->getGraphicsResetStatusARB() != GL_NO_ERROR;
    }

    m_needsDisplay = false;
}

void TextureLayer::pushPropertiesTo(LayerImpl* layer)
{
    Layer::pushPropertiesTo(layer);

    TextureLayerImpl* textureLayer = static_cast<TextureLayerImpl*>(layer);
    textureLayer->setFlipped(m_flipped);
    textureLayer->setUVRect(m_uvRect);
    textureLayer->setPremultipliedAlpha(m_premultipliedAlpha);
    textureLayer->setTextureId(m_textureId);
    m_contentCommitted = drawsContent();
}

}
