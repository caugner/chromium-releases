// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/scrollbar_animation_controller.h"

#include "cc/scrollbar_layer_impl.h"
#include "base/time.h"

#if OS(ANDROID)
#include "cc/scrollbar_animation_controller_linear_fade.h"
#endif

namespace cc {

#if OS(ANDROID)
scoped_ptr<ScrollbarAnimationController> ScrollbarAnimationController::create(LayerImpl* scrollLayer)
{
    static const double fadeoutDelay = 0.3;
    static const double fadeoutLength = 0.3;
    return ScrollbarAnimationControllerLinearFade::create(scrollLayer, fadeoutDelay, fadeoutLength).PassAs<ScrollbarAnimationController>();
}
#else
scoped_ptr<ScrollbarAnimationController> ScrollbarAnimationController::create(LayerImpl* scrollLayer)
{
    return make_scoped_ptr(new ScrollbarAnimationController(scrollLayer));
}
#endif

ScrollbarAnimationController::ScrollbarAnimationController(LayerImpl* scrollLayer)
    : m_horizontalScrollbarLayer(0)
    , m_verticalScrollbarLayer(0)
{
    ScrollbarAnimationController::updateScrollOffsetAtTime(scrollLayer, 0);
}

ScrollbarAnimationController::~ScrollbarAnimationController()
{
}

bool ScrollbarAnimationController::animate(double)
{
    return false;
}

void ScrollbarAnimationController::didPinchGestureBegin()
{
    didPinchGestureBeginAtTime((base::TimeTicks::Now() - base::TimeTicks()).InSecondsF());
}

void ScrollbarAnimationController::didPinchGestureUpdate()
{
    didPinchGestureUpdateAtTime((base::TimeTicks::Now() - base::TimeTicks()).InSecondsF());
}

void ScrollbarAnimationController::didPinchGestureEnd()
{
    didPinchGestureEndAtTime((base::TimeTicks::Now() - base::TimeTicks()).InSecondsF());
}

void ScrollbarAnimationController::updateScrollOffset(LayerImpl* scrollLayer)
{
    updateScrollOffsetAtTime(scrollLayer, (base::TimeTicks::Now() - base::TimeTicks()).InSecondsF());
}

IntSize ScrollbarAnimationController::getScrollLayerBounds(const LayerImpl* scrollLayer)
{
    if (!scrollLayer->children().size())
        return IntSize();
    // Copy & paste from LayerTreeHostImpl...
    // FIXME: Hardcoding the first child here is weird. Think of
    // a cleaner way to get the contentBounds on the Impl side.
    return scrollLayer->children()[0]->bounds();
}

void ScrollbarAnimationController::updateScrollOffsetAtTime(LayerImpl* scrollLayer, double)
{
    m_currentPos = scrollLayer->scrollPosition() + scrollLayer->scrollDelta();
    m_totalSize = getScrollLayerBounds(scrollLayer);
    m_maximum = scrollLayer->maxScrollPosition();

    if (m_horizontalScrollbarLayer) {
        m_horizontalScrollbarLayer->setCurrentPos(m_currentPos.x());
        m_horizontalScrollbarLayer->setTotalSize(m_totalSize.width());
        m_horizontalScrollbarLayer->setMaximum(m_maximum.width());
    }

    if (m_verticalScrollbarLayer) {
        m_verticalScrollbarLayer->setCurrentPos(m_currentPos.y());
        m_verticalScrollbarLayer->setTotalSize(m_totalSize.height());
        m_verticalScrollbarLayer->setMaximum(m_maximum.height());
    }
}

} // namespace cc
