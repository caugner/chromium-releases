// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/scrollbar_layer_impl.h"

#include "cc/quad_sink.h"
#include "cc/scrollbar_animation_controller.h"
#include "cc/texture_draw_quad.h"

using WebKit::WebRect;
using WebKit::WebScrollbar;

namespace cc {

scoped_ptr<ScrollbarLayerImpl> ScrollbarLayerImpl::create(int id)
{
    return make_scoped_ptr(new ScrollbarLayerImpl(id));
}

ScrollbarLayerImpl::ScrollbarLayerImpl(int id)
    : LayerImpl(id)
    , m_scrollbar(this)
    , m_backTrackResourceId(0)
    , m_foreTrackResourceId(0)
    , m_thumbResourceId(0)
    , m_scrollbarOverlayStyle(WebScrollbar::ScrollbarOverlayStyleDefault)
    , m_orientation(WebScrollbar::Horizontal)
    , m_controlSize(WebScrollbar::RegularScrollbar)
    , m_pressedPart(WebScrollbar::NoPart)
    , m_hoveredPart(WebScrollbar::NoPart)
    , m_isScrollableAreaActive(false)
    , m_isScrollViewScrollbar(false)
    , m_enabled(false)
    , m_isCustomScrollbar(false)
    , m_isOverlayScrollbar(false)
{
}

ScrollbarLayerImpl::~ScrollbarLayerImpl()
{
}

void ScrollbarLayerImpl::setScrollbarGeometry(scoped_ptr<ScrollbarGeometryFixedThumb> geometry)
{
    m_geometry = geometry.Pass();
}

void ScrollbarLayerImpl::setScrollbarData(WebScrollbar* scrollbar)
{
    m_scrollbarOverlayStyle = scrollbar->scrollbarOverlayStyle();
    m_orientation = scrollbar->orientation();
    m_controlSize = scrollbar->controlSize();
    m_pressedPart = scrollbar->pressedPart();
    m_hoveredPart = scrollbar->hoveredPart();
    m_isScrollableAreaActive = scrollbar->isScrollableAreaActive();
    m_isScrollViewScrollbar = scrollbar->isScrollViewScrollbar();
    m_enabled = scrollbar->enabled();
    m_isCustomScrollbar = scrollbar->isCustomScrollbar();
    m_isOverlayScrollbar = scrollbar->isOverlay();

    scrollbar->getTickmarks(m_tickmarks);

    m_geometry->update(scrollbar);
}

static FloatRect toUVRect(const WebRect& r, const IntRect& bounds)
{
    return FloatRect(static_cast<float>(r.x) / bounds.width(), static_cast<float>(r.y) / bounds.height(),
                     static_cast<float>(r.width) / bounds.width(), static_cast<float>(r.height) / bounds.height());
}

void ScrollbarLayerImpl::appendQuads(QuadSink& quadSink, AppendQuadsData& appendQuadsData)
{
    bool premultipledAlpha = false;
    bool flipped = false;
    FloatRect uvRect(0, 0, 1, 1);
    IntRect boundsRect(IntPoint(), bounds());
    IntRect contentBoundsRect(IntPoint(), contentBounds());

    SharedQuadState* sharedQuadState = quadSink.useSharedQuadState(createSharedQuadState());
    appendDebugBorderQuad(quadSink, sharedQuadState, appendQuadsData);

    WebRect thumbRect, backTrackRect, foreTrackRect;
    m_geometry->splitTrack(&m_scrollbar, m_geometry->trackRect(&m_scrollbar), backTrackRect, thumbRect, foreTrackRect);
    if (!m_geometry->hasThumb(&m_scrollbar))
        thumbRect = WebRect();

    if (m_thumbResourceId && !thumbRect.isEmpty()) {
        scoped_ptr<TextureDrawQuad> quad = TextureDrawQuad::create(sharedQuadState, layerRectToContentRect(thumbRect), m_thumbResourceId, premultipledAlpha, uvRect, flipped);
        quad->setNeedsBlending();
        quadSink.append(quad.PassAs<DrawQuad>(), appendQuadsData);
    }

    if (!m_backTrackResourceId)
        return;

    // We only paint the track in two parts if we were given a texture for the forward track part.
    if (m_foreTrackResourceId && !foreTrackRect.isEmpty())
        quadSink.append(TextureDrawQuad::create(sharedQuadState, layerRectToContentRect(foreTrackRect), m_foreTrackResourceId, premultipledAlpha, toUVRect(foreTrackRect, boundsRect), flipped).PassAs<DrawQuad>(), appendQuadsData);

    // Order matters here: since the back track texture is being drawn to the entire contents rect, we must append it after the thumb and
    // fore track quads. The back track texture contains (and displays) the buttons.
    if (!contentBoundsRect.isEmpty())
        quadSink.append(TextureDrawQuad::create(sharedQuadState, IntRect(contentBoundsRect), m_backTrackResourceId, premultipledAlpha, uvRect, flipped).PassAs<DrawQuad>(), appendQuadsData);
}

void ScrollbarLayerImpl::didLoseContext()
{
    m_backTrackResourceId = 0;
    m_foreTrackResourceId = 0;
    m_thumbResourceId = 0;
}

bool ScrollbarLayerImpl::Scrollbar::isOverlay() const
{
    return m_owner->m_isOverlayScrollbar;
}

int ScrollbarLayerImpl::Scrollbar::value() const
{
    return m_owner->m_currentPos;
}

WebKit::WebPoint ScrollbarLayerImpl::Scrollbar::location() const
{
    return WebKit::WebPoint();
}

WebKit::WebSize ScrollbarLayerImpl::Scrollbar::size() const
{
    return WebKit::WebSize(m_owner->bounds().width(), m_owner->bounds().height());
}

bool ScrollbarLayerImpl::Scrollbar::enabled() const
{
    return m_owner->m_enabled;
}

int ScrollbarLayerImpl::Scrollbar::maximum() const
{
    return m_owner->m_maximum;
}

int ScrollbarLayerImpl::Scrollbar::totalSize() const
{
    return m_owner->m_totalSize;
}

bool ScrollbarLayerImpl::Scrollbar::isScrollViewScrollbar() const
{
    return m_owner->m_isScrollViewScrollbar;
}

bool ScrollbarLayerImpl::Scrollbar::isScrollableAreaActive() const
{
    return m_owner->m_isScrollableAreaActive;
}

void ScrollbarLayerImpl::Scrollbar::getTickmarks(WebKit::WebVector<WebRect>& tickmarks) const
{
    tickmarks = m_owner->m_tickmarks;
}

WebScrollbar::ScrollbarControlSize ScrollbarLayerImpl::Scrollbar::controlSize() const
{
    return m_owner->m_controlSize;
}

WebScrollbar::ScrollbarPart ScrollbarLayerImpl::Scrollbar::pressedPart() const
{
    return m_owner->m_pressedPart;
}

WebScrollbar::ScrollbarPart ScrollbarLayerImpl::Scrollbar::hoveredPart() const
{
    return m_owner->m_hoveredPart;
}

WebScrollbar::ScrollbarOverlayStyle ScrollbarLayerImpl::Scrollbar::scrollbarOverlayStyle() const
{
    return m_owner->m_scrollbarOverlayStyle;
}

WebScrollbar::Orientation ScrollbarLayerImpl::Scrollbar::orientation() const
{
    return m_owner->m_orientation;
}

bool ScrollbarLayerImpl::Scrollbar::isCustomScrollbar() const
{
    return m_owner->m_isCustomScrollbar;
}

const char* ScrollbarLayerImpl::layerTypeAsString() const
{
    return "ScrollbarLayer";
}

}
