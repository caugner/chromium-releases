// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/scrollbar_layer.h"

#include "base/basictypes.h"
#include "base/debug/trace_event.h"
#include "cc/layer_painter.h"
#include "cc/layer_tree_host.h"
#include "cc/resource_update_queue.h"
#include "cc/scrollbar_layer_impl.h"
#include <public/WebRect.h>

using WebKit::WebRect;

namespace cc {

scoped_ptr<LayerImpl> ScrollbarLayer::createLayerImpl()
{
    return ScrollbarLayerImpl::create(id()).PassAs<LayerImpl>();
}

scoped_refptr<ScrollbarLayer> ScrollbarLayer::create(scoped_ptr<WebKit::WebScrollbar> scrollbar, WebKit::WebScrollbarThemePainter painter, scoped_ptr<WebKit::WebScrollbarThemeGeometry> geometry, int scrollLayerId)
{
    return make_scoped_refptr(new ScrollbarLayer(scrollbar.Pass(), painter, geometry.Pass(), scrollLayerId));
}

ScrollbarLayer::ScrollbarLayer(scoped_ptr<WebKit::WebScrollbar> scrollbar, WebKit::WebScrollbarThemePainter painter, scoped_ptr<WebKit::WebScrollbarThemeGeometry> geometry, int scrollLayerId)
    : m_scrollbar(scrollbar.Pass())
    , m_painter(painter)
    , m_geometry(geometry.Pass())
    , m_scrollLayerId(scrollLayerId)
    , m_textureFormat(GL_INVALID_ENUM)
{
}

ScrollbarLayer::~ScrollbarLayer()
{
}

void ScrollbarLayer::pushPropertiesTo(LayerImpl* layer)
{
    Layer::pushPropertiesTo(layer);

    ScrollbarLayerImpl* scrollbarLayer = static_cast<ScrollbarLayerImpl*>(layer);

    if (!scrollbarLayer->scrollbarGeometry())
        scrollbarLayer->setScrollbarGeometry(ScrollbarGeometryFixedThumb::create(make_scoped_ptr(m_geometry->clone())));

    scrollbarLayer->setScrollbarData(m_scrollbar.get());

    if (m_backTrack && m_backTrack->texture()->haveBackingTexture())
        scrollbarLayer->setBackTrackResourceId(m_backTrack->texture()->resourceId());
    else
        scrollbarLayer->setBackTrackResourceId(0);

    if (m_foreTrack && m_foreTrack->texture()->haveBackingTexture())
        scrollbarLayer->setForeTrackResourceId(m_foreTrack->texture()->resourceId());
    else
        scrollbarLayer->setForeTrackResourceId(0);

    if (m_thumb && m_thumb->texture()->haveBackingTexture())
        scrollbarLayer->setThumbResourceId(m_thumb->texture()->resourceId());
    else
        scrollbarLayer->setThumbResourceId(0);
}

ScrollbarLayer* ScrollbarLayer::toScrollbarLayer()
{
    return this;
}

class ScrollbarBackgroundPainter : public LayerPainter {
public:
    static scoped_ptr<ScrollbarBackgroundPainter> create(WebKit::WebScrollbar* scrollbar, WebKit::WebScrollbarThemePainter painter, WebKit::WebScrollbarThemeGeometry* geometry, WebKit::WebScrollbar::ScrollbarPart trackPart)
    {
        return make_scoped_ptr(new ScrollbarBackgroundPainter(scrollbar, painter, geometry, trackPart));
    }

    virtual void paint(SkCanvas* skCanvas, const IntRect& contentRect, FloatRect&) OVERRIDE
    {
        WebKit::WebCanvas* canvas = skCanvas;
        // The following is a simplification of ScrollbarThemeComposite::paint.
        WebKit::WebRect contentWebRect(contentRect.x(), contentRect.y(), contentRect.width(), contentRect.height());
        m_painter.paintScrollbarBackground(canvas, contentWebRect);

        if (m_geometry->hasButtons(m_scrollbar)) {
            WebRect backButtonStartPaintRect = m_geometry->backButtonStartRect(m_scrollbar);
            m_painter.paintBackButtonStart(canvas, backButtonStartPaintRect);

            WebRect backButtonEndPaintRect = m_geometry->backButtonEndRect(m_scrollbar);
            m_painter.paintBackButtonEnd(canvas, backButtonEndPaintRect);

            WebRect forwardButtonStartPaintRect = m_geometry->forwardButtonStartRect(m_scrollbar);
            m_painter.paintForwardButtonStart(canvas, forwardButtonStartPaintRect);

            WebRect forwardButtonEndPaintRect = m_geometry->forwardButtonEndRect(m_scrollbar);
            m_painter.paintForwardButtonEnd(canvas, forwardButtonEndPaintRect);
        }

        WebRect trackPaintRect = m_geometry->trackRect(m_scrollbar);
        m_painter.paintTrackBackground(canvas, trackPaintRect);

        bool thumbPresent = m_geometry->hasThumb(m_scrollbar);
        if (thumbPresent) {
            if (m_trackPart == WebKit::WebScrollbar::ForwardTrackPart)
                m_painter.paintForwardTrackPart(canvas, trackPaintRect);
            else
                m_painter.paintBackTrackPart(canvas, trackPaintRect);
        }

        m_painter.paintTickmarks(canvas, trackPaintRect);
    }
private:
    ScrollbarBackgroundPainter(WebKit::WebScrollbar* scrollbar, WebKit::WebScrollbarThemePainter painter, WebKit::WebScrollbarThemeGeometry* geometry, WebKit::WebScrollbar::ScrollbarPart trackPart)
        : m_scrollbar(scrollbar)
        , m_painter(painter)
        , m_geometry(geometry)
        , m_trackPart(trackPart)
    {
    }

    WebKit::WebScrollbar* m_scrollbar;
    WebKit::WebScrollbarThemePainter m_painter;
    WebKit::WebScrollbarThemeGeometry* m_geometry;
    WebKit::WebScrollbar::ScrollbarPart m_trackPart;

    DISALLOW_COPY_AND_ASSIGN(ScrollbarBackgroundPainter);
};

bool ScrollbarLayer::needsContentsScale() const
{
    return true;
}

IntSize ScrollbarLayer::contentBounds() const
{
    return IntSize(lroundf(bounds().width() * contentsScale()), lroundf(bounds().height() * contentsScale()));
}

class ScrollbarThumbPainter : public LayerPainter {
public:
    static scoped_ptr<ScrollbarThumbPainter> create(WebKit::WebScrollbar* scrollbar, WebKit::WebScrollbarThemePainter painter, WebKit::WebScrollbarThemeGeometry* geometry)
    {
        return make_scoped_ptr(new ScrollbarThumbPainter(scrollbar, painter, geometry));
    }

    virtual void paint(SkCanvas* skCanvas, const IntRect& contentRect, FloatRect& opaque) OVERRIDE
    {
        WebKit::WebCanvas* canvas = skCanvas;

        // Consider the thumb to be at the origin when painting.
        WebRect thumbRect = m_geometry->thumbRect(m_scrollbar);
        thumbRect.x = 0;
        thumbRect.y = 0;
        m_painter.paintThumb(canvas, thumbRect);
    }

private:
    ScrollbarThumbPainter(WebKit::WebScrollbar* scrollbar, WebKit::WebScrollbarThemePainter painter, WebKit::WebScrollbarThemeGeometry* geometry)
        : m_scrollbar(scrollbar)
        , m_painter(painter)
        , m_geometry(geometry)
    {
    }

    WebKit::WebScrollbar* m_scrollbar;
    WebKit::WebScrollbarThemePainter m_painter;
    WebKit::WebScrollbarThemeGeometry* m_geometry;

    DISALLOW_COPY_AND_ASSIGN(ScrollbarThumbPainter);
};

void ScrollbarLayer::setLayerTreeHost(LayerTreeHost* host)
{
    if (!host || host != layerTreeHost()) {
        m_backTrackUpdater = NULL;
        m_backTrack.reset();
        m_thumbUpdater = NULL;
        m_thumb.reset();
    }

    Layer::setLayerTreeHost(host);
}

void ScrollbarLayer::createUpdaterIfNeeded()
{
    m_textureFormat = layerTreeHost()->rendererCapabilities().bestTextureFormat;

    if (!m_backTrackUpdater)
        m_backTrackUpdater = CachingBitmapContentLayerUpdater::Create(ScrollbarBackgroundPainter::create(m_scrollbar.get(), m_painter, m_geometry.get(), WebKit::WebScrollbar::BackTrackPart).PassAs<LayerPainter>());
    if (!m_backTrack)
        m_backTrack = m_backTrackUpdater->createResource(layerTreeHost()->contentsTextureManager());

    // Only create two-part track if we think the two parts could be different in appearance.
    if (m_scrollbar->isCustomScrollbar()) {
        if (!m_foreTrackUpdater)
            m_foreTrackUpdater = CachingBitmapContentLayerUpdater::Create(ScrollbarBackgroundPainter::create(m_scrollbar.get(), m_painter, m_geometry.get(), WebKit::WebScrollbar::ForwardTrackPart).PassAs<LayerPainter>());
        if (!m_foreTrack)
            m_foreTrack = m_foreTrackUpdater->createResource(layerTreeHost()->contentsTextureManager());
    }

    if (!m_thumbUpdater)
        m_thumbUpdater = CachingBitmapContentLayerUpdater::Create(ScrollbarThumbPainter::create(m_scrollbar.get(), m_painter, m_geometry.get()).PassAs<LayerPainter>());
    if (!m_thumb)
        m_thumb = m_thumbUpdater->createResource(layerTreeHost()->contentsTextureManager());
}

void ScrollbarLayer::updatePart(CachingBitmapContentLayerUpdater* painter, LayerUpdater::Resource* texture, const IntRect& rect, ResourceUpdateQueue& queue, RenderingStats& stats)
{
    // Skip painting and uploading if there are no invalidations and
    // we already have valid texture data.
    if (texture->texture()->haveBackingTexture()
            && texture->texture()->size() == rect.size()
            && m_updateRect.isEmpty())
        return;

    // We should always have enough memory for UI.
    DCHECK(texture->texture()->canAcquireBackingTexture());
    if (!texture->texture()->canAcquireBackingTexture())
        return;

    // Paint and upload the entire part.
    float widthScale = static_cast<float>(contentBounds().width()) / bounds().width();
    float heightScale = static_cast<float>(contentBounds().height()) / bounds().height();
    IntRect paintedOpaqueRect;
    painter->prepareToUpdate(rect, rect.size(), widthScale, heightScale, paintedOpaqueRect, stats);
    if (!painter->pixelsDidChange() && texture->texture()->haveBackingTexture()) {
        TRACE_EVENT_INSTANT0("cc","ScrollbarLayer::updatePart no texture upload needed");
        return;
    }

    IntSize destOffset(0, 0);
    texture->update(queue, rect, destOffset, false, stats);
}


void ScrollbarLayer::setTexturePriorities(const PriorityCalculator&)
{
    if (contentBounds().isEmpty())
        return;

    createUpdaterIfNeeded();

    bool drawsToRoot = !renderTarget()->parent();
    if (m_backTrack) {
        m_backTrack->texture()->setDimensions(contentBounds(), m_textureFormat);
        m_backTrack->texture()->setRequestPriority(PriorityCalculator::uiPriority(drawsToRoot));
    }
    if (m_foreTrack) {
        m_foreTrack->texture()->setDimensions(contentBounds(), m_textureFormat);
        m_foreTrack->texture()->setRequestPriority(PriorityCalculator::uiPriority(drawsToRoot));
    }
    if (m_thumb) {
        IntSize thumbSize = layerRectToContentRect(m_geometry->thumbRect(m_scrollbar.get())).size();
        m_thumb->texture()->setDimensions(thumbSize, m_textureFormat);
        m_thumb->texture()->setRequestPriority(PriorityCalculator::uiPriority(drawsToRoot));
    }
}

void ScrollbarLayer::update(ResourceUpdateQueue& queue, const OcclusionTracker*, RenderingStats& stats)
{
    if (contentBounds().isEmpty())
        return;

    createUpdaterIfNeeded();

    IntPoint scrollbarOrigin(m_scrollbar->location().x, m_scrollbar->location().y);
    IntRect contentRect = layerRectToContentRect(WebKit::WebRect(scrollbarOrigin.x(), scrollbarOrigin.y(), bounds().width(), bounds().height()));
    updatePart(m_backTrackUpdater.get(), m_backTrack.get(), contentRect, queue, stats);
    if (m_foreTrack && m_foreTrackUpdater)
        updatePart(m_foreTrackUpdater.get(), m_foreTrack.get(), contentRect, queue, stats);

    // Consider the thumb to be at the origin when painting.
    WebKit::WebRect thumbRect = m_geometry->thumbRect(m_scrollbar.get());
    IntRect originThumbRect = layerRectToContentRect(WebKit::WebRect(0, 0, thumbRect.width, thumbRect.height));
    if (!originThumbRect.isEmpty())
        updatePart(m_thumbUpdater.get(), m_thumb.get(), originThumbRect, queue, stats);
}

}
