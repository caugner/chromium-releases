// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/layer_tree_host_impl.h"

#include "base/basictypes.h"
#include "base/debug/trace_event.h"
#include "cc/append_quads_data.h"
#include "cc/damage_tracker.h"
#include "cc/debug_rect_history.h"
#include "cc/delay_based_time_source.h"
#include "cc/font_atlas.h"
#include "cc/frame_rate_counter.h"
#include "cc/gl_renderer.h"
#include "cc/heads_up_display_layer_impl.h"
#include "cc/layer_iterator.h"
#include "cc/layer_tree_host.h"
#include "cc/layer_tree_host_common.h"
#include "cc/math_util.h"
#include "cc/overdraw_metrics.h"
#include "cc/page_scale_animation.h"
#include "cc/prioritized_texture_manager.h"
#include "cc/render_pass_draw_quad.h"
#include "cc/rendering_stats.h"
#include "cc/scrollbar_animation_controller.h"
#include "cc/scrollbar_layer_impl.h"
#include "cc/settings.h"
#include "cc/single_thread_proxy.h"
#include "cc/software_renderer.h"
#include "cc/texture_uploader.h"
#include <algorithm>

using WebKit::WebTransformationMatrix;

namespace {

void didVisibilityChange(cc::LayerTreeHostImpl* id, bool visible)
{
    if (visible) {
        TRACE_EVENT_ASYNC_BEGIN1("webkit", "LayerTreeHostImpl::setVisible", id, "LayerTreeHostImpl", id);
        return;
    }

    TRACE_EVENT_ASYNC_END0("webkit", "LayerTreeHostImpl::setVisible", id);
}

} // namespace

namespace cc {

PinchZoomViewport::PinchZoomViewport()
    : m_pageScaleFactor(1)
    , m_pageScaleDelta(1)
    , m_sentPageScaleDelta(1)
    , m_minPageScaleFactor(0)
    , m_maxPageScaleFactor(0)
{
}

float PinchZoomViewport::totalPageScaleFactor() const
{
    return m_pageScaleFactor * m_pageScaleDelta;
}

void PinchZoomViewport::setPageScaleDelta(float delta)
{
    // Clamp to the current min/max limits.
    float totalPageScaleFactor = m_pageScaleFactor * delta;
    if (m_minPageScaleFactor && totalPageScaleFactor < m_minPageScaleFactor)
        delta = m_minPageScaleFactor / m_pageScaleFactor;
    else if (m_maxPageScaleFactor && totalPageScaleFactor > m_maxPageScaleFactor)
        delta = m_maxPageScaleFactor / m_pageScaleFactor;

    if (delta == m_pageScaleDelta)
        return;

    m_pageScaleDelta = delta;
}

bool PinchZoomViewport::setPageScaleFactorAndLimits(float pageScaleFactor, float minPageScaleFactor, float maxPageScaleFactor)
{
    DCHECK(pageScaleFactor);

    if (m_sentPageScaleDelta == 1 && pageScaleFactor == m_pageScaleFactor && minPageScaleFactor == m_minPageScaleFactor && maxPageScaleFactor == m_maxPageScaleFactor)
        return false;

    m_minPageScaleFactor = minPageScaleFactor;
    m_maxPageScaleFactor = maxPageScaleFactor;

    m_pageScaleFactor = pageScaleFactor;
    return true;
}

FloatRect PinchZoomViewport::bounds() const
{
    FloatSize scaledViewportSize = m_layoutViewportSize;
    scaledViewportSize.scale(1 / totalPageScaleFactor());

    FloatRect bounds(FloatPoint(0, 0), scaledViewportSize);
    bounds.setLocation(m_pinchViewportScrollDelta);

    return bounds;
}

FloatSize PinchZoomViewport::applyScroll(FloatSize& delta)
{
    FloatSize overflow;
    FloatRect pinchedBounds = bounds();

    pinchedBounds.move(delta);
    if (pinchedBounds.x() < 0) {
        overflow.setWidth(pinchedBounds.x());
        pinchedBounds.setX(0);
    }

    if (pinchedBounds.y() < 0) {
        overflow.setHeight(pinchedBounds.y());
        pinchedBounds.setY(0);
    }

    if (pinchedBounds.maxX() > m_layoutViewportSize.width()) {
        overflow.setWidth(
            pinchedBounds.maxX() - m_layoutViewportSize.width());
        pinchedBounds.move(
            m_layoutViewportSize.width() - pinchedBounds.maxX(), 0);
    }

    if (pinchedBounds.maxY() > m_layoutViewportSize.height()) {
        overflow.setHeight(
            pinchedBounds.maxY() - m_layoutViewportSize.height());
        pinchedBounds.move(
            0, m_layoutViewportSize.height() - pinchedBounds.maxY());
    }
    m_pinchViewportScrollDelta = pinchedBounds.location();

    return overflow;
}

WebTransformationMatrix PinchZoomViewport::implTransform() const
{
    WebTransformationMatrix transform;
    transform.scale(m_pageScaleDelta);

    // If the pinch state is applied in the impl, then push it to the
    // impl transform, otherwise the scale is handled by WebCore.
    if (Settings::pageScalePinchZoomEnabled()) {
        transform.scale(m_pageScaleFactor);
        transform.translate(-m_pinchViewportScrollDelta.x(),
                            -m_pinchViewportScrollDelta.y());
    }

    return transform;
}

class LayerTreeHostImplTimeSourceAdapter : public TimeSourceClient {
public:
    static scoped_ptr<LayerTreeHostImplTimeSourceAdapter> create(LayerTreeHostImpl* layerTreeHostImpl, scoped_refptr<DelayBasedTimeSource> timeSource)
    {
        return make_scoped_ptr(new LayerTreeHostImplTimeSourceAdapter(layerTreeHostImpl, timeSource));
    }
    virtual ~LayerTreeHostImplTimeSourceAdapter()
    {
        m_timeSource->setClient(0);
        m_timeSource->setActive(false);
    }

    virtual void onTimerTick() OVERRIDE
    {
        // FIXME: We require that animate be called on the impl thread. This
        // avoids asserts in single threaded mode. Ideally background ticking
        // would be handled by the proxy/scheduler and this could be removed.
        DebugScopedSetImplThread impl;

        m_layerTreeHostImpl->animate(base::TimeTicks::Now(), base::Time::Now());
    }

    void setActive(bool active)
    {
        if (active != m_timeSource->active())
            m_timeSource->setActive(active);
    }

private:
    LayerTreeHostImplTimeSourceAdapter(LayerTreeHostImpl* layerTreeHostImpl, scoped_refptr<DelayBasedTimeSource> timeSource)
        : m_layerTreeHostImpl(layerTreeHostImpl)
        , m_timeSource(timeSource)
    {
        m_timeSource->setClient(this);
    }

    LayerTreeHostImpl* m_layerTreeHostImpl;
    scoped_refptr<DelayBasedTimeSource> m_timeSource;

    DISALLOW_COPY_AND_ASSIGN(LayerTreeHostImplTimeSourceAdapter);
};

LayerTreeHostImpl::FrameData::FrameData()
{
}

LayerTreeHostImpl::FrameData::~FrameData()
{
}

scoped_ptr<LayerTreeHostImpl> LayerTreeHostImpl::create(const LayerTreeSettings& settings, LayerTreeHostImplClient* client)
{
    return make_scoped_ptr(new LayerTreeHostImpl(settings, client));
}

LayerTreeHostImpl::LayerTreeHostImpl(const LayerTreeSettings& settings, LayerTreeHostImplClient* client)
    : m_client(client)
    , m_sourceFrameNumber(-1)
    , m_rootScrollLayerImpl(0)
    , m_currentlyScrollingLayerImpl(0)
    , m_hudLayerImpl(0)
    , m_scrollingLayerIdFromPreviousTree(-1)
    , m_scrollDeltaIsInViewportSpace(false)
    , m_settings(settings)
    , m_deviceScaleFactor(1)
    , m_visible(true)
    , m_contentsTexturesPurged(false)
    , m_managedMemoryPolicy(PrioritizedTextureManager::defaultMemoryAllocationLimit(), 
                            PriorityCalculator::allowEverythingCutoff(),
                            0,
                            PriorityCalculator::allowNothingCutoff())
    , m_backgroundColor(0)
    , m_hasTransparentBackground(false)
    , m_needsAnimateLayers(false)
    , m_pinchGestureActive(false)
    , m_fpsCounter(FrameRateCounter::create())
    , m_debugRectHistory(DebugRectHistory::create())
    , m_numImplThreadScrolls(0)
    , m_numMainThreadScrolls(0)
{
    DCHECK(Proxy::isImplThread());
    didVisibilityChange(this, m_visible);
}

LayerTreeHostImpl::~LayerTreeHostImpl()
{
    DCHECK(Proxy::isImplThread());
    TRACE_EVENT0("cc", "LayerTreeHostImpl::~LayerTreeHostImpl()");

    if (m_rootLayerImpl)
        clearRenderSurfaces();
}

void LayerTreeHostImpl::beginCommit()
{
}

void LayerTreeHostImpl::commitComplete()
{
    TRACE_EVENT0("cc", "LayerTreeHostImpl::commitComplete");
    // Recompute max scroll position; must be after layer content bounds are
    // updated.
    updateMaxScrollPosition();
    m_client->sendManagedMemoryStats();
}

bool LayerTreeHostImpl::canDraw()
{
    // Note: If you are changing this function or any other function that might
    // affect the result of canDraw, make sure to call m_client->onCanDrawStateChanged
    // in the proper places and update the notifyIfCanDrawChanged test.

    if (!m_rootLayerImpl) {
        TRACE_EVENT_INSTANT0("cc", "LayerTreeHostImpl::canDraw no root layer");
        return false;
    }
    if (deviceViewportSize().isEmpty()) {
        TRACE_EVENT_INSTANT0("cc", "LayerTreeHostImpl::canDraw empty viewport");
        return false;
    }
    if (!m_renderer) {
        TRACE_EVENT_INSTANT0("cc", "LayerTreeHostImpl::canDraw no renderer");
        return false;
    }
    if (m_contentsTexturesPurged) {
        TRACE_EVENT_INSTANT0("cc", "LayerTreeHostImpl::canDraw contents textures purged");
        return false;
    }
    return true;
}

GraphicsContext* LayerTreeHostImpl::context() const
{
    return m_context.get();
}

void LayerTreeHostImpl::animate(base::TimeTicks monotonicTime, base::Time wallClockTime)
{
    animatePageScale(monotonicTime);
    animateLayers(monotonicTime, wallClockTime);
    animateScrollbars(monotonicTime);
}

void LayerTreeHostImpl::startPageScaleAnimation(const IntSize& targetPosition, bool anchorPoint, float pageScale, base::TimeTicks startTime, base::TimeDelta duration)
{
    if (!m_rootScrollLayerImpl)
        return;

    IntSize scrollTotal = flooredIntSize(m_rootScrollLayerImpl->scrollPosition() + m_rootScrollLayerImpl->scrollDelta());
    scrollTotal.scale(m_pinchZoomViewport.pageScaleDelta());
    float scaleTotal = m_pinchZoomViewport.totalPageScaleFactor();
    IntSize scaledContentSize = contentSize();
    scaledContentSize.scale(m_pinchZoomViewport.pageScaleDelta());

    double startTimeSeconds = (startTime - base::TimeTicks()).InSecondsF();
    m_pageScaleAnimation = PageScaleAnimation::create(scrollTotal, scaleTotal, m_deviceViewportSize, scaledContentSize, startTimeSeconds);

    if (anchorPoint) {
        IntSize windowAnchor(targetPosition);
        windowAnchor.scale(scaleTotal / pageScale);
        windowAnchor -= scrollTotal;
        m_pageScaleAnimation->zoomWithAnchor(windowAnchor, pageScale, duration.InSecondsF());
    } else
        m_pageScaleAnimation->zoomTo(targetPosition, pageScale, duration.InSecondsF());

    m_client->setNeedsRedrawOnImplThread();
    m_client->setNeedsCommitOnImplThread();
}

void LayerTreeHostImpl::scheduleAnimation()
{
    m_client->setNeedsRedrawOnImplThread();
}

void LayerTreeHostImpl::trackDamageForAllSurfaces(LayerImpl* rootDrawLayer, const LayerList& renderSurfaceLayerList)
{
    // For now, we use damage tracking to compute a global scissor. To do this, we must
    // compute all damage tracking before drawing anything, so that we know the root
    // damage rect. The root damage rect is then used to scissor each surface.

    for (int surfaceIndex = renderSurfaceLayerList.size() - 1; surfaceIndex >= 0 ; --surfaceIndex) {
        LayerImpl* renderSurfaceLayer = renderSurfaceLayerList[surfaceIndex];
        RenderSurfaceImpl* renderSurface = renderSurfaceLayer->renderSurface();
        DCHECK(renderSurface);
        renderSurface->damageTracker()->updateDamageTrackingState(renderSurface->layerList(), renderSurfaceLayer->id(), renderSurface->surfacePropertyChangedOnlyFromDescendant(), renderSurface->contentRect(), renderSurfaceLayer->maskLayer(), renderSurfaceLayer->filters(), renderSurfaceLayer->filter());
    }
}

void LayerTreeHostImpl::updateRootScrollLayerImplTransform()
{
    if (m_rootScrollLayerImpl) {
        m_rootScrollLayerImpl->setImplTransform(implTransform());
    }
}

void LayerTreeHostImpl::calculateRenderSurfaceLayerList(LayerList& renderSurfaceLayerList)
{
    DCHECK(renderSurfaceLayerList.empty());
    DCHECK(m_rootLayerImpl);
    DCHECK(m_renderer); // For maxTextureSize.

    {
        updateRootScrollLayerImplTransform();

        TRACE_EVENT0("cc", "LayerTreeHostImpl::calcDrawEtc");
        float pageScaleFactor = m_pinchZoomViewport.pageScaleFactor();
        LayerTreeHostCommon::calculateDrawTransforms(m_rootLayerImpl.get(), deviceViewportSize(), m_deviceScaleFactor, pageScaleFactor, &m_layerSorter, rendererCapabilities().maxTextureSize, renderSurfaceLayerList);

        trackDamageForAllSurfaces(m_rootLayerImpl.get(), renderSurfaceLayerList);
    }
}

void LayerTreeHostImpl::FrameData::appendRenderPass(scoped_ptr<RenderPass> renderPass)
{
    RenderPass* pass = renderPass.get();
    renderPasses.push_back(pass);
    renderPassesById.set(pass->id(), renderPass.Pass());
}

bool LayerTreeHostImpl::calculateRenderPasses(FrameData& frame)
{
    DCHECK(frame.renderPasses.empty());

    calculateRenderSurfaceLayerList(*frame.renderSurfaceLayerList);

    TRACE_EVENT1("cc", "LayerTreeHostImpl::calculateRenderPasses", "renderSurfaceLayerList.size()", static_cast<long long unsigned>(frame.renderSurfaceLayerList->size()));

    // Create the render passes in dependency order.
    for (int surfaceIndex = frame.renderSurfaceLayerList->size() - 1; surfaceIndex >= 0 ; --surfaceIndex) {
        LayerImpl* renderSurfaceLayer = (*frame.renderSurfaceLayerList)[surfaceIndex];
        renderSurfaceLayer->renderSurface()->appendRenderPasses(frame);
    }

    bool recordMetricsForFrame = true; // FIXME: In the future, disable this when about:tracing is off.
    OcclusionTrackerImpl occlusionTracker(m_rootLayerImpl->renderSurface()->contentRect(), recordMetricsForFrame);
    occlusionTracker.setMinimumTrackingSize(m_settings.minimumOcclusionTrackingSize);

    if (settings().showOccludingRects)
        occlusionTracker.setOccludingScreenSpaceRectsContainer(&frame.occludingScreenSpaceRects);

    // Add quads to the Render passes in FrontToBack order to allow for testing occlusion and performing culling during the tree walk.
    typedef LayerIterator<LayerImpl, std::vector<LayerImpl*>, RenderSurfaceImpl, LayerIteratorActions::FrontToBack> LayerIteratorType;

    // Typically when we are missing a texture and use a checkerboard quad, we still draw the frame. However when the layer being
    // checkerboarded is moving due to an impl-animation, we drop the frame to avoid flashing due to the texture suddenly appearing
    // in the future.
    bool drawFrame = true;

    LayerIteratorType end = LayerIteratorType::end(frame.renderSurfaceLayerList);
    for (LayerIteratorType it = LayerIteratorType::begin(frame.renderSurfaceLayerList); it != end; ++it) {
        RenderPass::Id targetRenderPassId = it.targetRenderSurfaceLayer()->renderSurface()->renderPassId();
        RenderPass* targetRenderPass = frame.renderPassesById.get(targetRenderPassId);

        occlusionTracker.enterLayer(it);

        AppendQuadsData appendQuadsData(targetRenderPass->id());

        if (it.representsContributingRenderSurface()) {
            RenderPass::Id contributingRenderPassId = it->renderSurface()->renderPassId();
            RenderPass* contributingRenderPass = frame.renderPassesById.get(contributingRenderPassId);
            targetRenderPass->appendQuadsForRenderSurfaceLayer(*it, contributingRenderPass, &occlusionTracker, appendQuadsData);
        } else if (it.representsItself() && !it->visibleContentRect().isEmpty()) {
            bool hasOcclusionFromOutsideTargetSurface;
            if (occlusionTracker.occluded(*it, it->visibleContentRect(), &hasOcclusionFromOutsideTargetSurface))
                appendQuadsData.hadOcclusionFromOutsideTargetSurface |= hasOcclusionFromOutsideTargetSurface;
            else {
                it->willDraw(m_resourceProvider.get());
                frame.willDrawLayers.push_back(*it);

                if (it->hasContributingDelegatedRenderPasses()) {
                    RenderPass::Id contributingRenderPassId = it->firstContributingRenderPassId();
                    while (frame.renderPassesById.contains(contributingRenderPassId)) {
                        RenderPass* renderPass = frame.renderPassesById.get(contributingRenderPassId);
  
                        AppendQuadsData appendQuadsData(renderPass->id());
                        renderPass->appendQuadsForLayer(*it, &occlusionTracker, appendQuadsData);

                        contributingRenderPassId = it->nextContributingRenderPassId(contributingRenderPassId);
                    }
                }

                targetRenderPass->appendQuadsForLayer(*it, &occlusionTracker, appendQuadsData);
            }
        }

        if (appendQuadsData.hadOcclusionFromOutsideTargetSurface)
            targetRenderPass->setHasOcclusionFromOutsideTargetSurface(true);

        if (appendQuadsData.hadMissingTiles) {
            bool layerHasAnimatingTransform = it->screenSpaceTransformIsAnimating() || it->drawTransformIsAnimating();
            if (layerHasAnimatingTransform || Settings::jankInsteadOfCheckerboard())
                drawFrame = false;
        }

        occlusionTracker.leaveLayer(it);
    }

#ifndef NDEBUG
    for (size_t i = 0; i < frame.renderPasses.size(); ++i) {
      for (size_t j = 0; j < frame.renderPasses[i]->quadList().size(); ++j)
        DCHECK(frame.renderPasses[i]->quadList()[j]->sharedQuadStateId() >= 0);
      DCHECK(frame.renderPassesById.contains(frame.renderPasses[i]->id()));
    }
#endif

    if (!m_hasTransparentBackground) {
        frame.renderPasses.back()->setHasTransparentBackground(false);
        frame.renderPasses.back()->appendQuadsToFillScreen(m_rootLayerImpl.get(), m_backgroundColor, occlusionTracker);
    }

    if (drawFrame)
        occlusionTracker.overdrawMetrics().recordMetrics(this);

    removeRenderPasses(CullRenderPassesWithNoQuads(), frame);
    m_renderer->decideRenderPassAllocationsForFrame(frame.renderPasses);
    removeRenderPasses(CullRenderPassesWithCachedTextures(*m_renderer), frame);

    return drawFrame;
}

void LayerTreeHostImpl::animateLayersRecursive(LayerImpl* current, base::TimeTicks monotonicTime, base::Time wallClockTime, AnimationEventsVector* events, bool& didAnimate, bool& needsAnimateLayers)
{
    bool subtreeNeedsAnimateLayers = false;

    LayerAnimationController* currentController = current->layerAnimationController();

    bool hadActiveAnimation = currentController->hasActiveAnimation();
    double monotonicTimeSeconds = (monotonicTime - base::TimeTicks()).InSecondsF();
    currentController->animate(monotonicTimeSeconds, events);
    bool startedAnimation = events->size() > 0;

    // We animated if we either ticked a running animation, or started a new animation.
    if (hadActiveAnimation || startedAnimation)
        didAnimate = true;

    // If the current controller still has an active animation, we must continue animating layers.
    if (currentController->hasActiveAnimation())
         subtreeNeedsAnimateLayers = true;

    for (size_t i = 0; i < current->children().size(); ++i) {
        bool childNeedsAnimateLayers = false;
        animateLayersRecursive(current->children()[i], monotonicTime, wallClockTime, events, didAnimate, childNeedsAnimateLayers);
        if (childNeedsAnimateLayers)
            subtreeNeedsAnimateLayers = true;
    }

    needsAnimateLayers = subtreeNeedsAnimateLayers;
}

void LayerTreeHostImpl::setBackgroundTickingEnabled(bool enabled)
{
    // Lazily create the timeSource adapter so that we can vary the interval for testing.
    if (!m_timeSourceClientAdapter)
        m_timeSourceClientAdapter = LayerTreeHostImplTimeSourceAdapter::create(this, DelayBasedTimeSource::create(lowFrequencyAnimationInterval(), Proxy::currentThread()));

    m_timeSourceClientAdapter->setActive(enabled);
}

IntSize LayerTreeHostImpl::contentSize() const
{
    // TODO(aelias): Hardcoding the first child here is weird. Think of
    // a cleaner way to get the contentBounds on the Impl side.
    if (!m_rootScrollLayerImpl || m_rootScrollLayerImpl->children().isEmpty())
        return IntSize();
    return m_rootScrollLayerImpl->children()[0]->contentBounds();
}

static inline RenderPass* findRenderPassById(RenderPass::Id renderPassId, const LayerTreeHostImpl::FrameData& frame)
{
    RenderPassIdHashMap::const_iterator it = frame.renderPassesById.find(renderPassId);
    DCHECK(it != frame.renderPassesById.end());
    return it->second;
}

static void removeRenderPassesRecursive(RenderPass::Id removeRenderPassId, LayerTreeHostImpl::FrameData& frame)
{
    RenderPass* removeRenderPass = findRenderPassById(removeRenderPassId, frame);
    RenderPassList& renderPasses = frame.renderPasses;
    RenderPassList::iterator toRemove = std::find(renderPasses.begin(), renderPasses.end(), removeRenderPass);

    // The pass was already removed by another quad - probably the original, and we are the replica.
    if (toRemove == renderPasses.end())
        return;

    const RenderPass* removedPass = *toRemove;
    frame.renderPasses.erase(toRemove);

    // Now follow up for all RenderPass quads and remove their RenderPasses recursively.
    const QuadList& quadList = removedPass->quadList();
    QuadList::constBackToFrontIterator quadListIterator = quadList.backToFrontBegin();
    for (; quadListIterator != quadList.backToFrontEnd(); ++quadListIterator) {
        DrawQuad* currentQuad = (*quadListIterator);
        if (currentQuad->material() != DrawQuad::RenderPass)
            continue;

        RenderPass::Id nextRemoveRenderPassId = RenderPassDrawQuad::materialCast(currentQuad)->renderPassId();
        removeRenderPassesRecursive(nextRemoveRenderPassId, frame);
    }
}

bool LayerTreeHostImpl::CullRenderPassesWithCachedTextures::shouldRemoveRenderPass(const RenderPassDrawQuad& quad, const FrameData&) const
{
    return quad.contentsChangedSinceLastFrame().IsEmpty() && m_renderer.haveCachedResourcesForRenderPassId(quad.renderPassId());
}

bool LayerTreeHostImpl::CullRenderPassesWithNoQuads::shouldRemoveRenderPass(const RenderPassDrawQuad& quad, const FrameData& frame) const
{
    const RenderPass* renderPass = findRenderPassById(quad.renderPassId(), frame);
    const RenderPassList& renderPasses = frame.renderPasses;
    RenderPassList::const_iterator foundPass = std::find(renderPasses.begin(), renderPasses.end(), renderPass);

    bool renderPassAlreadyRemoved = foundPass == renderPasses.end();
    if (renderPassAlreadyRemoved)
        return false;

    // If any quad or RenderPass draws into this RenderPass, then keep it.
    const QuadList& quadList = (*foundPass)->quadList();
    for (QuadList::constBackToFrontIterator quadListIterator = quadList.backToFrontBegin(); quadListIterator != quadList.backToFrontEnd(); ++quadListIterator) {
        DrawQuad* currentQuad = *quadListIterator;

        if (currentQuad->material() != DrawQuad::RenderPass)
            return false;

        const RenderPass* contributingPass = findRenderPassById(RenderPassDrawQuad::materialCast(currentQuad)->renderPassId(), frame);
        RenderPassList::const_iterator foundContributingPass = std::find(renderPasses.begin(), renderPasses.end(), contributingPass);
        if (foundContributingPass != renderPasses.end())
            return false;
    }
    return true;
}

// Defined for linking tests.
template void LayerTreeHostImpl::removeRenderPasses<LayerTreeHostImpl::CullRenderPassesWithCachedTextures>(CullRenderPassesWithCachedTextures, FrameData&);
template void LayerTreeHostImpl::removeRenderPasses<LayerTreeHostImpl::CullRenderPassesWithNoQuads>(CullRenderPassesWithNoQuads, FrameData&);

// static
template<typename RenderPassCuller>
void LayerTreeHostImpl::removeRenderPasses(RenderPassCuller culler, FrameData& frame)
{
    for (size_t it = culler.renderPassListBegin(frame.renderPasses); it != culler.renderPassListEnd(frame.renderPasses); it = culler.renderPassListNext(it)) {
        const RenderPass* currentPass = frame.renderPasses[it];
        const QuadList& quadList = currentPass->quadList();
        QuadList::constBackToFrontIterator quadListIterator = quadList.backToFrontBegin();

        for (; quadListIterator != quadList.backToFrontEnd(); ++quadListIterator) {
            DrawQuad* currentQuad = *quadListIterator;

            if (currentQuad->material() != DrawQuad::RenderPass)
                continue;

            RenderPassDrawQuad* renderPassQuad = static_cast<RenderPassDrawQuad*>(currentQuad);
            if (!culler.shouldRemoveRenderPass(*renderPassQuad, frame))
                continue;

            // We are changing the vector in the middle of iteration. Because we
            // delete render passes that draw into the current pass, we are
            // guaranteed that any data from the iterator to the end will not
            // change. So, capture the iterator position from the end of the
            // list, and restore it after the change.
            int positionFromEnd = frame.renderPasses.size() - it;
            removeRenderPassesRecursive(renderPassQuad->renderPassId(), frame);
            it = frame.renderPasses.size() - positionFromEnd;
            DCHECK(it >= 0);
        }
    }
}

bool LayerTreeHostImpl::prepareToDraw(FrameData& frame)
{
    TRACE_EVENT0("cc", "LayerTreeHostImpl::prepareToDraw");
    DCHECK(canDraw());

    frame.renderSurfaceLayerList = &m_renderSurfaceLayerList;
    frame.renderPasses.clear();
    frame.renderPassesById.clear();
    frame.renderSurfaceLayerList->clear();
    frame.willDrawLayers.clear();

    if (!calculateRenderPasses(frame))
        return false;

    // If we return true, then we expect drawLayers() to be called before this function is called again.
    return true;
}

void LayerTreeHostImpl::enforceManagedMemoryPolicy(const ManagedMemoryPolicy& policy)
{
    bool evictedResources = m_client->reduceContentsTextureMemoryOnImplThread(
        m_visible ? policy.bytesLimitWhenVisible : policy.bytesLimitWhenNotVisible,
        m_visible ? policy.priorityCutoffWhenVisible : policy.priorityCutoffWhenNotVisible);
    if (evictedResources) {
        setContentsTexturesPurged();
        m_client->setNeedsCommitOnImplThread();
        m_client->onCanDrawStateChanged(canDraw());
    }
    m_client->sendManagedMemoryStats();
}

void LayerTreeHostImpl::setManagedMemoryPolicy(const ManagedMemoryPolicy& policy)
{
    if (m_managedMemoryPolicy == policy)
        return;
    m_managedMemoryPolicy = policy;
    enforceManagedMemoryPolicy(m_managedMemoryPolicy);
    // We always need to commit after changing the memory policy because the new
    // limit can result in more or less content having texture allocated for it.
    m_client->setNeedsCommitOnImplThread();
}

void LayerTreeHostImpl::onVSyncParametersChanged(double monotonicTimebase, double intervalInSeconds)
{
    base::TimeTicks timebase = base::TimeTicks::FromInternalValue(monotonicTimebase * base::Time::kMicrosecondsPerSecond);
    base::TimeDelta interval = base::TimeDelta::FromMicroseconds(intervalInSeconds * base::Time::kMicrosecondsPerSecond);
    m_client->onVSyncParametersChanged(timebase, interval);
}

void LayerTreeHostImpl::drawLayers(const FrameData& frame)
{
    TRACE_EVENT0("cc", "LayerTreeHostImpl::drawLayers");
    DCHECK(canDraw());
    DCHECK(!frame.renderPasses.empty());

    // FIXME: use the frame begin time from the overall compositor scheduler.
    // This value is currently inaccessible because it is up in Chromium's
    // RenderWidget.
    m_fpsCounter->markBeginningOfFrame(base::TimeTicks::Now());

    if (m_settings.showDebugRects())
        m_debugRectHistory->saveDebugRectsForCurrentFrame(m_rootLayerImpl.get(), *frame.renderSurfaceLayerList, frame.occludingScreenSpaceRects, settings());

    // Because the contents of the HUD depend on everything else in the frame, the contents
    // of its texture are updated as the last thing before the frame is drawn.
    if (m_hudLayerImpl)
        m_hudLayerImpl->updateHudTexture(m_resourceProvider.get());

    m_renderer->drawFrame(frame.renderPasses, frame.renderPassesById);

    // Once a RenderPass has been drawn, its damage should be cleared in
    // case the RenderPass will be reused next frame.
    for (unsigned int i = 0; i < frame.renderPasses.size(); i++)
        frame.renderPasses[i]->setDamageRect(FloatRect());

    // The next frame should start by assuming nothing has changed, and changes are noted as they occur.
    for (unsigned int i = 0; i < frame.renderSurfaceLayerList->size(); i++)
        (*frame.renderSurfaceLayerList)[i]->renderSurface()->damageTracker()->didDrawDamagedArea();
    m_rootLayerImpl->resetAllChangeTrackingForSubtree();
}

void LayerTreeHostImpl::didDrawAllLayers(const FrameData& frame)
{
    for (size_t i = 0; i < frame.willDrawLayers.size(); ++i)
        frame.willDrawLayers[i]->didDraw(m_resourceProvider.get());

    // Once all layers have been drawn, pending texture uploads should no
    // longer block future uploads.
    m_resourceProvider->markPendingUploadsAsNonBlocking();
}

void LayerTreeHostImpl::finishAllRendering()
{
    if (m_renderer)
        m_renderer->finish();
}

bool LayerTreeHostImpl::isContextLost()
{
    return m_renderer && m_renderer->isContextLost();
}

const RendererCapabilities& LayerTreeHostImpl::rendererCapabilities() const
{
    return m_renderer->capabilities();
}

bool LayerTreeHostImpl::swapBuffers()
{
    DCHECK(m_renderer);

    m_fpsCounter->markEndOfFrame();
    return m_renderer->swapBuffers();
}

const IntSize& LayerTreeHostImpl::deviceViewportSize() const
{
    return m_deviceViewportSize;
}

const LayerTreeSettings& LayerTreeHostImpl::settings() const
{
    return m_settings;
}

void LayerTreeHostImpl::didLoseContext()
{
    m_client->didLoseContextOnImplThread();
}

void LayerTreeHostImpl::onSwapBuffersComplete()
{
    m_client->onSwapBuffersCompleteOnImplThread();
}

void LayerTreeHostImpl::readback(void* pixels, const IntRect& rect)
{
    DCHECK(m_renderer);
    m_renderer->getFramebufferPixels(pixels, rect);
}

static LayerImpl* findRootScrollLayer(LayerImpl* layer)
{
    if (!layer)
        return 0;

    if (layer->scrollable())
        return layer;

    for (size_t i = 0; i < layer->children().size(); ++i) {
        LayerImpl* found = findRootScrollLayer(layer->children()[i]);
        if (found)
            return found;
    }

    return 0;
}

// Content layers can be either directly scrollable or contained in an outer
// scrolling layer which applies the scroll transform. Given a content layer,
// this function returns the associated scroll layer if any.
static LayerImpl* findScrollLayerForContentLayer(LayerImpl* layerImpl)
{
    if (!layerImpl)
        return 0;

    if (layerImpl->scrollable())
        return layerImpl;

    if (layerImpl->drawsContent() && layerImpl->parent() && layerImpl->parent()->scrollable())
        return layerImpl->parent();

    return 0;
}

void LayerTreeHostImpl::setRootLayer(scoped_ptr<LayerImpl> layer)
{
    m_rootLayerImpl = layer.Pass();
    m_rootScrollLayerImpl = findRootScrollLayer(m_rootLayerImpl.get());
    m_currentlyScrollingLayerImpl = 0;

    if (m_rootLayerImpl && m_scrollingLayerIdFromPreviousTree != -1)
        m_currentlyScrollingLayerImpl = LayerTreeHostCommon::findLayerInSubtree(m_rootLayerImpl.get(), m_scrollingLayerIdFromPreviousTree);

    m_scrollingLayerIdFromPreviousTree = -1;

    m_client->onCanDrawStateChanged(canDraw());
}

scoped_ptr<LayerImpl> LayerTreeHostImpl::detachLayerTree()
{
    // Clear all data structures that have direct references to the layer tree.
    m_scrollingLayerIdFromPreviousTree = m_currentlyScrollingLayerImpl ? m_currentlyScrollingLayerImpl->id() : -1;
    m_currentlyScrollingLayerImpl = 0;
    m_renderSurfaceLayerList.clear();

    return m_rootLayerImpl.Pass();
}

void LayerTreeHostImpl::setVisible(bool visible)
{
    DCHECK(Proxy::isImplThread());

    if (m_visible == visible)
        return;
    m_visible = visible;
    didVisibilityChange(this, m_visible);
    enforceManagedMemoryPolicy(m_managedMemoryPolicy);

    if (!m_renderer)
        return;

    m_renderer->setVisible(visible);

    setBackgroundTickingEnabled(!m_visible && m_needsAnimateLayers);
}

bool LayerTreeHostImpl::initializeRenderer(scoped_ptr<GraphicsContext> context)
{
    // Since we will create a new resource provider, we cannot continue to use
    // the old resources (i.e. renderSurfaces and texture IDs). Clear them
    // before we destroy the old resource provider.
    if (m_rootLayerImpl) {
        clearRenderSurfaces();
        sendDidLoseContextRecursive(m_rootLayerImpl.get());
    }
    // Note: order is important here.
    m_renderer.reset();
    m_resourceProvider.reset();
    m_context.reset();

    if (!context->bindToClient(this))
        return false;

    scoped_ptr<ResourceProvider> resourceProvider = ResourceProvider::create(context.get());
    if (!resourceProvider)
        return false;

    if (context->context3D())
        m_renderer = GLRenderer::create(this, resourceProvider.get());
    else if (context->softwareDevice())
        m_renderer = SoftwareRenderer::create(this, resourceProvider.get(), context->softwareDevice());
    if (!m_renderer)
        return false;

    m_resourceProvider = resourceProvider.Pass();
    m_context = context.Pass();

    if (!m_visible)
        m_renderer->setVisible(m_visible);

    m_client->onCanDrawStateChanged(canDraw());

    return true;
}

void LayerTreeHostImpl::setContentsTexturesPurged()
{
    m_contentsTexturesPurged = true;
    m_client->onCanDrawStateChanged(canDraw());
}

void LayerTreeHostImpl::resetContentsTexturesPurged()
{
    m_contentsTexturesPurged = false;
    m_client->onCanDrawStateChanged(canDraw());
}

void LayerTreeHostImpl::setViewportSize(const IntSize& layoutViewportSize, const IntSize& deviceViewportSize)
{
    if (layoutViewportSize == m_layoutViewportSize && deviceViewportSize == m_deviceViewportSize)
        return;

    m_layoutViewportSize = layoutViewportSize;
    m_deviceViewportSize = deviceViewportSize;

    m_pinchZoomViewport.setLayoutViewportSize(FloatSize(layoutViewportSize));

    updateMaxScrollPosition();

    if (m_renderer)
        m_renderer->viewportChanged();

    m_client->onCanDrawStateChanged(canDraw());
}

static void adjustScrollsForPageScaleChange(LayerImpl* layerImpl, float pageScaleChange)
{
    if (!layerImpl)
        return;

    if (layerImpl->scrollable()) {
        // We need to convert impl-side scroll deltas to pageScale space.
        FloatSize scrollDelta = layerImpl->scrollDelta();
        scrollDelta.scale(pageScaleChange);
        layerImpl->setScrollDelta(scrollDelta);
    }

    for (size_t i = 0; i < layerImpl->children().size(); ++i)
        adjustScrollsForPageScaleChange(layerImpl->children()[i], pageScaleChange);
}

void LayerTreeHostImpl::setDeviceScaleFactor(float deviceScaleFactor)
{
    if (deviceScaleFactor == m_deviceScaleFactor)
        return;
    m_deviceScaleFactor = deviceScaleFactor;

    updateMaxScrollPosition();
}

float LayerTreeHostImpl::pageScaleFactor() const
{
    return m_pinchZoomViewport.pageScaleFactor();
}

void LayerTreeHostImpl::setPageScaleFactorAndLimits(float pageScaleFactor, float minPageScaleFactor, float maxPageScaleFactor)
{
    if (!pageScaleFactor)
      return;

    float pageScaleChange = pageScaleFactor / m_pinchZoomViewport.pageScaleFactor();
    m_pinchZoomViewport.setPageScaleFactorAndLimits(pageScaleFactor, minPageScaleFactor, maxPageScaleFactor);

    if (!Settings::pageScalePinchZoomEnabled()) {
        if (pageScaleChange != 1)
            adjustScrollsForPageScaleChange(m_rootScrollLayerImpl, pageScaleChange);
    }

    // Clamp delta to limits and refresh display matrix.
    setPageScaleDelta(m_pinchZoomViewport.pageScaleDelta() / m_pinchZoomViewport.sentPageScaleDelta());
    m_pinchZoomViewport.setSentPageScaleDelta(1);
}

void LayerTreeHostImpl::setPageScaleDelta(float delta)
{
    m_pinchZoomViewport.setPageScaleDelta(delta);

    updateMaxScrollPosition();
}

void LayerTreeHostImpl::updateMaxScrollPosition()
{
    if (!m_rootScrollLayerImpl || !m_rootScrollLayerImpl->children().size())
        return;

    FloatSize viewBounds = m_deviceViewportSize;
    if (LayerImpl* clipLayer = m_rootScrollLayerImpl->parent()) {
        // Compensate for non-overlay scrollbars.
        if (clipLayer->masksToBounds()) {
            viewBounds = clipLayer->bounds();
            viewBounds.scale(m_deviceScaleFactor);
        }
    }

    IntSize contentBounds = contentSize();
    if (Settings::pageScalePinchZoomEnabled()) {
        // Pinch with pageScale scrolls entirely in layout space.  contentSize
        // returns the bounds including the page scale factor, so calculate the
        // pre page-scale layout size here.
        float pageScaleFactor = m_pinchZoomViewport.pageScaleFactor();
        contentBounds.setWidth(contentBounds.width() / pageScaleFactor);
        contentBounds.setHeight(contentBounds.height() / pageScaleFactor);
    } else {
        viewBounds.scale(1 / m_pinchZoomViewport.pageScaleDelta());
    }

    IntSize maxScroll = contentBounds - expandedIntSize(viewBounds);
    maxScroll.scale(1 / m_deviceScaleFactor);

    // The viewport may be larger than the contents in some cases, such as
    // having a vertical scrollbar but no horizontal overflow.
    maxScroll.clampNegativeToZero();

    m_rootScrollLayerImpl->setMaxScrollPosition(maxScroll);
}

void LayerTreeHostImpl::setNeedsRedraw()
{
    m_client->setNeedsRedrawOnImplThread();
}

bool LayerTreeHostImpl::ensureRenderSurfaceLayerList()
{
    if (!m_rootLayerImpl)
        return false;
    if (!m_renderer)
        return false;

    // We need both a non-empty render surface layer list and a root render
    // surface to be able to iterate over the visible layers.
    if (m_renderSurfaceLayerList.size() && m_rootLayerImpl->renderSurface())
        return true;

    // If we are called after setRootLayer() but before prepareToDraw(), we need
    // to recalculate the visible layers. This prevents being unable to scroll
    // during part of a commit.
    m_renderSurfaceLayerList.clear();
    calculateRenderSurfaceLayerList(m_renderSurfaceLayerList);

    return m_renderSurfaceLayerList.size();
}

InputHandlerClient::ScrollStatus LayerTreeHostImpl::scrollBegin(const IntPoint& viewportPoint, InputHandlerClient::ScrollInputType type)
{
    TRACE_EVENT0("cc", "LayerTreeHostImpl::scrollBegin");

    DCHECK(!m_currentlyScrollingLayerImpl);
    clearCurrentlyScrollingLayer();

    if (!ensureRenderSurfaceLayerList())
        return ScrollIgnored;

    IntPoint deviceViewportPoint = viewportPoint;
    deviceViewportPoint.scale(m_deviceScaleFactor, m_deviceScaleFactor);

    // First find out which layer was hit from the saved list of visible layers
    // in the most recent frame.
    LayerImpl* layerImpl = LayerTreeHostCommon::findLayerThatIsHitByPoint(deviceViewportPoint, m_renderSurfaceLayerList);

    // Walk up the hierarchy and look for a scrollable layer.
    LayerImpl* potentiallyScrollingLayerImpl = 0;
    for (; layerImpl; layerImpl = layerImpl->parent()) {
        // The content layer can also block attempts to scroll outside the main thread.
        if (layerImpl->tryScroll(deviceViewportPoint, type) == ScrollOnMainThread) {
            m_numMainThreadScrolls++;
            return ScrollOnMainThread;
        }

        LayerImpl* scrollLayerImpl = findScrollLayerForContentLayer(layerImpl);
        if (!scrollLayerImpl)
            continue;

        ScrollStatus status = scrollLayerImpl->tryScroll(deviceViewportPoint, type);

        // If any layer wants to divert the scroll event to the main thread, abort.
        if (status == ScrollOnMainThread) {
            m_numMainThreadScrolls++;
            return ScrollOnMainThread;
        }

        if (status == ScrollStarted && !potentiallyScrollingLayerImpl)
            potentiallyScrollingLayerImpl = scrollLayerImpl;
    }

    if (potentiallyScrollingLayerImpl) {
        m_currentlyScrollingLayerImpl = potentiallyScrollingLayerImpl;
        // Gesture events need to be transformed from viewport coordinates to local layer coordinates
        // so that the scrolling contents exactly follow the user's finger. In contrast, wheel
        // events are already in local layer coordinates so we can just apply them directly.
        m_scrollDeltaIsInViewportSpace = (type == Gesture);
        m_numImplThreadScrolls++;
        return ScrollStarted;
    }
    return ScrollIgnored;
}

static FloatSize scrollLayerWithViewportSpaceDelta(PinchZoomViewport* viewport, LayerImpl& layerImpl, float scaleFromViewportToScreenSpace, const FloatPoint& viewportPoint, const FloatSize& viewportDelta)
{
    // Layers with non-invertible screen space transforms should not have passed the scroll hit
    // test in the first place.
    DCHECK(layerImpl.screenSpaceTransform().isInvertible());
    WebTransformationMatrix inverseScreenSpaceTransform = layerImpl.screenSpaceTransform().inverse();

    FloatPoint screenSpacePoint = viewportPoint;
    screenSpacePoint.scale(scaleFromViewportToScreenSpace, scaleFromViewportToScreenSpace);

    FloatSize screenSpaceDelta = viewportDelta;
    screenSpaceDelta.scale(scaleFromViewportToScreenSpace, scaleFromViewportToScreenSpace);

    // First project the scroll start and end points to local layer space to find the scroll delta
    // in layer coordinates.
    bool startClipped, endClipped;
    FloatPoint screenSpaceEndPoint = screenSpacePoint + screenSpaceDelta;
    FloatPoint localStartPoint = MathUtil::projectPoint(inverseScreenSpaceTransform, screenSpacePoint, startClipped);
    FloatPoint localEndPoint = MathUtil::projectPoint(inverseScreenSpaceTransform, screenSpaceEndPoint, endClipped);

    // In general scroll point coordinates should not get clipped.
    DCHECK(!startClipped);
    DCHECK(!endClipped);
    if (startClipped || endClipped)
        return FloatSize();

    // localStartPoint and localEndPoint are in content space but we want to move them to layer space for scrolling.
    float widthScale = 1;
    float heightScale = 1;
    if (!layerImpl.contentBounds().isEmpty() && !layerImpl.bounds().isEmpty()) {
        widthScale = layerImpl.bounds().width() / static_cast<float>(layerImpl.contentBounds().width());
        heightScale = layerImpl.bounds().height() / static_cast<float>(layerImpl.contentBounds().height());
    }
    localStartPoint.scale(widthScale, heightScale);
    localEndPoint.scale(widthScale, heightScale);

    // Apply the scroll delta.
    FloatSize previousDelta(layerImpl.scrollDelta());
    FloatSize unscrolled = layerImpl.scrollBy(localEndPoint - localStartPoint);

    if (viewport)
        viewport->applyScroll(unscrolled);

    // Get the end point in the layer's content space so we can apply its screenSpaceTransform.
    FloatPoint actualLocalEndPoint = localStartPoint + layerImpl.scrollDelta() - previousDelta;
    FloatPoint actualLocalContentEndPoint = actualLocalEndPoint;
    actualLocalContentEndPoint.scale(1 / widthScale, 1 / heightScale);

    // Calculate the applied scroll delta in viewport space coordinates.
    FloatPoint actualScreenSpaceEndPoint = MathUtil::mapPoint(layerImpl.screenSpaceTransform(), actualLocalContentEndPoint, endClipped);
    DCHECK(!endClipped);
    if (endClipped)
        return FloatSize();
    FloatPoint actualViewportEndPoint = actualScreenSpaceEndPoint;
    actualViewportEndPoint.scale(1 / scaleFromViewportToScreenSpace, 1 / scaleFromViewportToScreenSpace);
    return actualViewportEndPoint - viewportPoint;
}

static FloatSize scrollLayerWithLocalDelta(LayerImpl& layerImpl, const FloatSize& localDelta)
{
    FloatSize previousDelta(layerImpl.scrollDelta());
    layerImpl.scrollBy(localDelta);
    return layerImpl.scrollDelta() - previousDelta;
}

void LayerTreeHostImpl::scrollBy(const IntPoint& viewportPoint, const IntSize& scrollDelta)
{
    TRACE_EVENT0("cc", "LayerTreeHostImpl::scrollBy");
    if (!m_currentlyScrollingLayerImpl)
        return;

    FloatSize pendingDelta(scrollDelta);

    for (LayerImpl* layerImpl = m_currentlyScrollingLayerImpl; layerImpl; layerImpl = layerImpl->parent()) {
        if (!layerImpl->scrollable())
            continue;

        PinchZoomViewport* viewport = layerImpl == m_rootScrollLayerImpl ? &m_pinchZoomViewport : 0;
        FloatSize appliedDelta;
        if (m_scrollDeltaIsInViewportSpace) {
            float scaleFromViewportToScreenSpace = m_deviceScaleFactor;
            appliedDelta = scrollLayerWithViewportSpaceDelta(viewport, *layerImpl, scaleFromViewportToScreenSpace, viewportPoint, pendingDelta);
        } else
            appliedDelta = scrollLayerWithLocalDelta(*layerImpl, pendingDelta);

        // If the layer wasn't able to move, try the next one in the hierarchy.
        float moveThresholdSquared = 0.1f * 0.1f;
        if (appliedDelta.diagonalLengthSquared() < moveThresholdSquared)
            continue;

        // If the applied delta is within 45 degrees of the input delta, bail out to make it easier
        // to scroll just one layer in one direction without affecting any of its parents.
        float angleThreshold = 45;
        if (MathUtil::smallestAngleBetweenVectors(appliedDelta, pendingDelta) < angleThreshold) {
            pendingDelta = FloatSize();
            break;
        }

        // Allow further movement only on an axis perpendicular to the direction in which the layer
        // moved.
        FloatSize perpendicularAxis(-appliedDelta.height(), appliedDelta.width());
        pendingDelta = MathUtil::projectVector(pendingDelta, perpendicularAxis);

        if (flooredIntSize(pendingDelta).isZero())
            break;
    }

    if (!scrollDelta.isZero() && flooredIntSize(pendingDelta).isEmpty()) {
        m_client->setNeedsCommitOnImplThread();
        m_client->setNeedsRedrawOnImplThread();
    }
}

void LayerTreeHostImpl::clearCurrentlyScrollingLayer()
{
    m_currentlyScrollingLayerImpl = 0;
    m_scrollingLayerIdFromPreviousTree = -1;
}

void LayerTreeHostImpl::scrollEnd()
{
    clearCurrentlyScrollingLayer();
}

void LayerTreeHostImpl::pinchGestureBegin()
{
    m_pinchGestureActive = true;
    m_previousPinchAnchor = IntPoint();

    if (m_rootScrollLayerImpl && m_rootScrollLayerImpl->scrollbarAnimationController())
        m_rootScrollLayerImpl->scrollbarAnimationController()->didPinchGestureBegin();
}

void LayerTreeHostImpl::pinchGestureUpdate(float magnifyDelta,
                                             const IntPoint& anchor)
{
    TRACE_EVENT0("cc", "LayerTreeHostImpl::pinchGestureUpdate");

    if (!m_rootScrollLayerImpl)
        return;

    if (m_previousPinchAnchor == IntPoint::zero())
        m_previousPinchAnchor = anchor;

    // Keep the center-of-pinch anchor specified by (x, y) in a stable
    // position over the course of the magnify.
    float pageScaleDelta = m_pinchZoomViewport.pageScaleDelta();
    FloatPoint previousScaleAnchor(m_previousPinchAnchor.x() / pageScaleDelta,
                                   m_previousPinchAnchor.y() / pageScaleDelta);
    setPageScaleDelta(pageScaleDelta * magnifyDelta);
    pageScaleDelta = m_pinchZoomViewport.pageScaleDelta();
    FloatPoint newScaleAnchor(anchor.x() / pageScaleDelta, anchor.y() / pageScaleDelta);
    FloatSize move = previousScaleAnchor - newScaleAnchor;

    m_previousPinchAnchor = anchor;

    if (Settings::pageScalePinchZoomEnabled()) {
        // Compute the application of the delta with respect to the current page zoom of the page.
        move.scale(1 / (m_pinchZoomViewport.pageScaleFactor() * m_deviceScaleFactor));
    }

    FloatSize scrollOverflow = Settings::pageScalePinchZoomEnabled() ? m_pinchZoomViewport.applyScroll(move) : move;
    m_rootScrollLayerImpl->scrollBy(roundedIntSize(scrollOverflow));

    if (m_rootScrollLayerImpl->scrollbarAnimationController())
        m_rootScrollLayerImpl->scrollbarAnimationController()->didPinchGestureUpdate();

    m_client->setNeedsCommitOnImplThread();
    m_client->setNeedsRedrawOnImplThread();
}

void LayerTreeHostImpl::pinchGestureEnd()
{
    m_pinchGestureActive = false;

    if (m_rootScrollLayerImpl && m_rootScrollLayerImpl->scrollbarAnimationController())
        m_rootScrollLayerImpl->scrollbarAnimationController()->didPinchGestureEnd();

    m_client->setNeedsCommitOnImplThread();
}

void LayerTreeHostImpl::computeDoubleTapZoomDeltas(ScrollAndScaleSet* scrollInfo)
{
    float pageScale = m_pageScaleAnimation->finalPageScale();
    IntSize scrollOffset = m_pageScaleAnimation->finalScrollOffset();
    scrollOffset.scale(m_pinchZoomViewport.pageScaleFactor() / pageScale);
    makeScrollAndScaleSet(scrollInfo, scrollOffset, pageScale);
}

void LayerTreeHostImpl::computePinchZoomDeltas(ScrollAndScaleSet* scrollInfo)
{
    if (!m_rootScrollLayerImpl)
        return;

    // Only send fake scroll/zoom deltas if we're pinch zooming out by a
    // significant amount. This also ensures only one fake delta set will be
    // sent.
    const float pinchZoomOutSensitivity = 0.95f;
    if (m_pinchZoomViewport.pageScaleDelta() > pinchZoomOutSensitivity)
        return;

    // Compute where the scroll offset/page scale would be if fully pinch-zoomed
    // out from the anchor point.
    IntSize scrollBegin = flooredIntSize(m_rootScrollLayerImpl->scrollPosition() + m_rootScrollLayerImpl->scrollDelta());
    scrollBegin.scale(m_pinchZoomViewport.pageScaleDelta());
    float scaleBegin = m_pinchZoomViewport.totalPageScaleFactor();
    float pageScaleDeltaToSend = m_pinchZoomViewport.minPageScaleFactor() / m_pinchZoomViewport.pageScaleFactor();
    FloatSize scaledContentsSize = contentSize();
    scaledContentsSize.scale(pageScaleDeltaToSend);

    FloatSize anchor = toSize(m_previousPinchAnchor);
    FloatSize scrollEnd = scrollBegin + anchor;
    scrollEnd.scale(m_pinchZoomViewport.minPageScaleFactor() / scaleBegin);
    scrollEnd -= anchor;
    scrollEnd = scrollEnd.shrunkTo(roundedIntSize(scaledContentsSize - m_deviceViewportSize)).expandedTo(FloatSize(0, 0));
    scrollEnd.scale(1 / pageScaleDeltaToSend);
    scrollEnd.scale(m_deviceScaleFactor);

    makeScrollAndScaleSet(scrollInfo, roundedIntSize(scrollEnd), m_pinchZoomViewport.minPageScaleFactor());
}

void LayerTreeHostImpl::makeScrollAndScaleSet(ScrollAndScaleSet* scrollInfo, const IntSize& scrollOffset, float pageScale)
{
    if (!m_rootScrollLayerImpl)
        return;

    LayerTreeHostCommon::ScrollUpdateInfo scroll;
    scroll.layerId = m_rootScrollLayerImpl->id();
    scroll.scrollDelta = scrollOffset - toSize(m_rootScrollLayerImpl->scrollPosition());
    scrollInfo->scrolls.push_back(scroll);
    m_rootScrollLayerImpl->setSentScrollDelta(scroll.scrollDelta);
    scrollInfo->pageScaleDelta = pageScale / m_pinchZoomViewport.pageScaleFactor();
    m_pinchZoomViewport.setSentPageScaleDelta(scrollInfo->pageScaleDelta);
}

static void collectScrollDeltas(ScrollAndScaleSet* scrollInfo, LayerImpl* layerImpl)
{
    if (!layerImpl)
        return;

    if (!layerImpl->scrollDelta().isZero()) {
        IntSize scrollDelta = flooredIntSize(layerImpl->scrollDelta());
        LayerTreeHostCommon::ScrollUpdateInfo scroll;
        scroll.layerId = layerImpl->id();
        scroll.scrollDelta = scrollDelta;
        scrollInfo->scrolls.push_back(scroll);
        layerImpl->setSentScrollDelta(scrollDelta);
    }

    for (size_t i = 0; i < layerImpl->children().size(); ++i)
        collectScrollDeltas(scrollInfo, layerImpl->children()[i]);
}

scoped_ptr<ScrollAndScaleSet> LayerTreeHostImpl::processScrollDeltas()
{
    scoped_ptr<ScrollAndScaleSet> scrollInfo(new ScrollAndScaleSet());

    if (m_pinchGestureActive || m_pageScaleAnimation) {
        scrollInfo->pageScaleDelta = 1;
        m_pinchZoomViewport.setSentPageScaleDelta(1);
        // FIXME(aelias): Make these painting optimizations compatible with
        // compositor-side scaling.
        if (!Settings::pageScalePinchZoomEnabled()) {
            if (m_pinchGestureActive)
                computePinchZoomDeltas(scrollInfo.get());
            else if (m_pageScaleAnimation.get())
                computeDoubleTapZoomDeltas(scrollInfo.get());
        }
        return scrollInfo.Pass();
    }

    collectScrollDeltas(scrollInfo.get(), m_rootLayerImpl.get());
    scrollInfo->pageScaleDelta = m_pinchZoomViewport.pageScaleDelta();
    m_pinchZoomViewport.setSentPageScaleDelta(scrollInfo->pageScaleDelta);

    return scrollInfo.Pass();
}

WebTransformationMatrix LayerTreeHostImpl::implTransform() const
{
    return m_pinchZoomViewport.implTransform();
}

void LayerTreeHostImpl::setFullRootLayerDamage()
{
    if (m_rootLayerImpl) {
        RenderSurfaceImpl* renderSurface = m_rootLayerImpl->renderSurface();
        if (renderSurface)
            renderSurface->damageTracker()->forceFullDamageNextUpdate();
    }
}

void LayerTreeHostImpl::animatePageScale(base::TimeTicks time)
{
    if (!m_pageScaleAnimation || !m_rootScrollLayerImpl)
        return;

    double monotonicTime = (time - base::TimeTicks()).InSecondsF();
    IntSize scrollTotal = flooredIntSize(m_rootScrollLayerImpl->scrollPosition() + m_rootScrollLayerImpl->scrollDelta());

    setPageScaleDelta(m_pageScaleAnimation->pageScaleAtTime(monotonicTime) / m_pinchZoomViewport.pageScaleFactor());
    IntSize nextScroll = m_pageScaleAnimation->scrollOffsetAtTime(monotonicTime);
    nextScroll.scale(1 / m_pinchZoomViewport.pageScaleDelta());
    m_rootScrollLayerImpl->scrollBy(nextScroll - scrollTotal);
    m_client->setNeedsRedrawOnImplThread();

    if (m_pageScaleAnimation->isAnimationCompleteAtTime(monotonicTime)) {
        m_pageScaleAnimation.reset();
        m_client->setNeedsCommitOnImplThread();
    }
}

void LayerTreeHostImpl::animateLayers(base::TimeTicks monotonicTime, base::Time wallClockTime)
{
    if (!Settings::acceleratedAnimationEnabled() || !m_needsAnimateLayers || !m_rootLayerImpl)
        return;

    TRACE_EVENT0("cc", "LayerTreeHostImpl::animateLayers");

    scoped_ptr<AnimationEventsVector> events(make_scoped_ptr(new AnimationEventsVector));

    bool didAnimate = false;
    animateLayersRecursive(m_rootLayerImpl.get(), monotonicTime, wallClockTime, events.get(), didAnimate, m_needsAnimateLayers);

    if (!events->empty())
        m_client->postAnimationEventsToMainThreadOnImplThread(events.Pass(), wallClockTime);

    if (didAnimate)
        m_client->setNeedsRedrawOnImplThread();

    setBackgroundTickingEnabled(!m_visible && m_needsAnimateLayers);
}

base::TimeDelta LayerTreeHostImpl::lowFrequencyAnimationInterval() const
{
    return base::TimeDelta::FromSeconds(1);
}

void LayerTreeHostImpl::sendDidLoseContextRecursive(LayerImpl* current)
{
    DCHECK(current);
    current->didLoseContext();
    if (current->maskLayer())
        sendDidLoseContextRecursive(current->maskLayer());
    if (current->replicaLayer())
        sendDidLoseContextRecursive(current->replicaLayer());
    for (size_t i = 0; i < current->children().size(); ++i)
        sendDidLoseContextRecursive(current->children()[i]);
}

static void clearRenderSurfacesOnLayerImplRecursive(LayerImpl* current)
{
    DCHECK(current);
    for (size_t i = 0; i < current->children().size(); ++i)
        clearRenderSurfacesOnLayerImplRecursive(current->children()[i]);
    current->clearRenderSurface();
}

void LayerTreeHostImpl::clearRenderSurfaces()
{
    clearRenderSurfacesOnLayerImplRecursive(m_rootLayerImpl.get());
    m_renderSurfaceLayerList.clear();
}

std::string LayerTreeHostImpl::layerTreeAsText() const
{
    std::string str;
    if (m_rootLayerImpl) {
        str = m_rootLayerImpl->layerTreeAsText();
        str +=  "RenderSurfaces:\n";
        dumpRenderSurfaces(&str, 1, m_rootLayerImpl.get());
    }
    return str;
}

void LayerTreeHostImpl::dumpRenderSurfaces(std::string* str, int indent, const LayerImpl* layer) const
{
    if (layer->renderSurface())
        layer->renderSurface()->dumpSurface(str, indent);

    for (size_t i = 0; i < layer->children().size(); ++i)
        dumpRenderSurfaces(str, indent, layer->children()[i]);
}

int LayerTreeHostImpl::sourceAnimationFrameNumber() const
{
    return fpsCounter()->currentFrameNumber();
}

void LayerTreeHostImpl::renderingStats(RenderingStats* stats) const
{
    stats->numFramesSentToScreen = fpsCounter()->currentFrameNumber();
    stats->droppedFrameCount = fpsCounter()->droppedFrameCount();
    stats->numImplThreadScrolls = m_numImplThreadScrolls;
    stats->numMainThreadScrolls = m_numMainThreadScrolls;
}

void LayerTreeHostImpl::animateScrollbars(base::TimeTicks time)
{
    animateScrollbarsRecursive(m_rootLayerImpl.get(), time);
}

void LayerTreeHostImpl::animateScrollbarsRecursive(LayerImpl* layer, base::TimeTicks time)
{
    if (!layer)
        return;

    ScrollbarAnimationController* scrollbarController = layer->scrollbarAnimationController();
    double monotonicTime = (time - base::TimeTicks()).InSecondsF();
    if (scrollbarController && scrollbarController->animate(monotonicTime))
        m_client->setNeedsRedrawOnImplThread();

    for (size_t i = 0; i < layer->children().size(); ++i)
        animateScrollbarsRecursive(layer->children()[i], time);
}

}  // namespace cc
