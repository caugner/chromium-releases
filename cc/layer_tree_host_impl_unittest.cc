// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/layer_tree_host_impl.h"

#include "base/command_line.h"
#include "base/hash_tables.h"
#include "cc/delegated_renderer_layer_impl.h"
#include "cc/gl_renderer.h"
#include "cc/heads_up_display_layer_impl.h"
#include "cc/io_surface_layer_impl.h"
#include "cc/layer_impl.h"
#include "cc/layer_tiling_data.h"
#include "cc/quad_sink.h"
#include "cc/render_pass_draw_quad.h"
#include "cc/scrollbar_geometry_fixed_thumb.h"
#include "cc/scrollbar_layer_impl.h"
#include "cc/settings.h"
#include "cc/single_thread_proxy.h"
#include "cc/solid_color_draw_quad.h"
#include "cc/test/animation_test_common.h"
#include "cc/test/fake_web_compositor_output_surface.h"
#include "cc/test/fake_web_graphics_context_3d.h"
#include "cc/test/fake_web_scrollbar_theme_geometry.h"
#include "cc/test/geometry_test_utils.h"
#include "cc/test/layer_test_common.h"
#include "cc/test/render_pass_test_common.h"
#include "cc/test/test_common.h"
#include "cc/texture_draw_quad.h"
#include "cc/texture_layer_impl.h"
#include "cc/tile_draw_quad.h"
#include "cc/tiled_layer_impl.h"
#include "cc/video_layer_impl.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include <public/WebVideoFrame.h>
#include <public/WebVideoFrameProvider.h>

using namespace cc;
using namespace LayerTestCommon;
using namespace WebKit;
using namespace WebKitTests;

using ::testing::Mock;
using ::testing::Return;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::_;

namespace {

// This test is parametrized to run all tests with the
// Settings::pageScalePinchZoomEnabled field enabled and disabled.
class LayerTreeHostImplTest : public testing::TestWithParam<bool>,
                                public LayerTreeHostImplClient {
public:
    LayerTreeHostImplTest()
        : m_onCanDrawStateChangedCalled(false)
        , m_didRequestCommit(false)
        , m_didRequestRedraw(false)
        , m_reduceMemoryResult(true)
    {
    }

    virtual void SetUp()
    {
        Settings::setPageScalePinchZoomEnabled(GetParam());
        LayerTreeSettings settings;
        settings.minimumOcclusionTrackingSize = IntSize();

        m_hostImpl = LayerTreeHostImpl::create(settings, this);
        m_hostImpl->initializeRenderer(createContext());
        m_hostImpl->setViewportSize(IntSize(10, 10), IntSize(10, 10));
    }

    virtual void TearDown()
    {
    }

    virtual void didLoseContextOnImplThread() OVERRIDE { }
    virtual void onSwapBuffersCompleteOnImplThread() OVERRIDE { }
    virtual void onVSyncParametersChanged(base::TimeTicks, base::TimeDelta) OVERRIDE { }
    virtual void onCanDrawStateChanged(bool canDraw) OVERRIDE { m_onCanDrawStateChangedCalled = true; }
    virtual void setNeedsRedrawOnImplThread() OVERRIDE { m_didRequestRedraw = true; }
    virtual void setNeedsCommitOnImplThread() OVERRIDE { m_didRequestCommit = true; }
    virtual void postAnimationEventsToMainThreadOnImplThread(scoped_ptr<AnimationEventsVector>, base::Time wallClockTime) OVERRIDE { }
    virtual bool reduceContentsTextureMemoryOnImplThread(size_t limitBytes, int priorityCutoff) OVERRIDE { return m_reduceMemoryResult; }
    virtual void sendManagedMemoryStats() OVERRIDE { }

    void setReduceMemoryResult(bool reduceMemoryResult) { m_reduceMemoryResult = reduceMemoryResult; }

    scoped_ptr<LayerTreeHostImpl> createLayerTreeHost(bool partialSwap, scoped_ptr<GraphicsContext> graphicsContext, scoped_ptr<LayerImpl> root)
    {
        Settings::setPartialSwapEnabled(partialSwap);

        LayerTreeSettings settings;
        settings.minimumOcclusionTrackingSize = IntSize();

        scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, this);

        myHostImpl->initializeRenderer(graphicsContext.Pass());
        myHostImpl->setViewportSize(IntSize(10, 10), IntSize(10, 10));

        root->setAnchorPoint(FloatPoint(0, 0));
        root->setPosition(FloatPoint(0, 0));
        root->setBounds(IntSize(10, 10));
        root->setContentBounds(IntSize(10, 10));
        root->setVisibleContentRect(IntRect(0, 0, 10, 10));
        root->setDrawsContent(true);
        myHostImpl->setRootLayer(root.Pass());
        return myHostImpl.Pass();
    }

    static void expectClearedScrollDeltasRecursive(LayerImpl* layer)
    {
        ASSERT_EQ(layer->scrollDelta(), IntSize());
        for (size_t i = 0; i < layer->children().size(); ++i)
            expectClearedScrollDeltasRecursive(layer->children()[i]);
    }

    static void expectContains(const ScrollAndScaleSet& scrollInfo, int id, const IntSize& scrollDelta)
    {
        int timesEncountered = 0;

        for (size_t i = 0; i < scrollInfo.scrolls.size(); ++i) {
            if (scrollInfo.scrolls[i].layerId != id)
                continue;
            EXPECT_EQ(scrollDelta.width(), scrollInfo.scrolls[i].scrollDelta.width());
            EXPECT_EQ(scrollDelta.height(), scrollInfo.scrolls[i].scrollDelta.height());
            timesEncountered++;
        }

        ASSERT_EQ(timesEncountered, 1);
    }

    void setupScrollAndContentsLayers(const IntSize& contentSize)
    {
        scoped_ptr<LayerImpl> root = LayerImpl::create(1);
        root->setScrollable(true);
        root->setScrollPosition(IntPoint(0, 0));
        root->setMaxScrollPosition(contentSize);
        root->setBounds(contentSize);
        root->setContentBounds(contentSize);
        root->setPosition(FloatPoint(0, 0));
        root->setAnchorPoint(FloatPoint(0, 0));

        scoped_ptr<LayerImpl> contents = LayerImpl::create(2);
        contents->setDrawsContent(true);
        contents->setBounds(contentSize);
        contents->setContentBounds(contentSize);
        contents->setPosition(FloatPoint(0, 0));
        contents->setAnchorPoint(FloatPoint(0, 0));
        root->addChild(contents.Pass());
        m_hostImpl->setRootLayer(root.Pass());
    }

    static scoped_ptr<LayerImpl> createScrollableLayer(int id, const IntSize& size)
    {
        scoped_ptr<LayerImpl> layer = LayerImpl::create(id);
        layer->setScrollable(true);
        layer->setDrawsContent(true);
        layer->setBounds(size);
        layer->setContentBounds(size);
        layer->setMaxScrollPosition(IntSize(size.width() * 2, size.height() * 2));
        return layer.Pass();
    }

    void initializeRendererAndDrawFrame()
    {
        m_hostImpl->initializeRenderer(createContext());
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
        m_hostImpl->drawLayers(frame);
        m_hostImpl->didDrawAllLayers(frame);
    }

protected:
    scoped_ptr<GraphicsContext> createContext()
    {
        return FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new FakeWebGraphicsContext3D)).PassAs<GraphicsContext>();
    }

    DebugScopedSetImplThread m_alwaysImplThread;
    DebugScopedSetMainThreadBlocked m_alwaysMainThreadBlocked;

    scoped_ptr<LayerTreeHostImpl> m_hostImpl;
    bool m_onCanDrawStateChangedCalled;
    bool m_didRequestCommit;
    bool m_didRequestRedraw;
    bool m_reduceMemoryResult;
    ScopedSettings m_scopedSettings;
};

class FakeWebGraphicsContext3DMakeCurrentFails : public FakeWebGraphicsContext3D {
public:
    virtual bool makeContextCurrent() { return false; }
};

TEST_P(LayerTreeHostImplTest, notifyIfCanDrawChanged)
{
    // Note: It is not possible to disable the renderer once it has been set,
    // so we do not need to test that disabling the renderer notifies us
    // that canDraw changed.
    EXPECT_FALSE(m_hostImpl->canDraw());
    m_onCanDrawStateChangedCalled = false;

    setupScrollAndContentsLayers(IntSize(100, 100));
    EXPECT_TRUE(m_hostImpl->canDraw());
    EXPECT_TRUE(m_onCanDrawStateChangedCalled);
    m_onCanDrawStateChangedCalled = false;

    // Toggle the root layer to make sure it toggles canDraw
    m_hostImpl->setRootLayer(scoped_ptr<LayerImpl>());
    EXPECT_FALSE(m_hostImpl->canDraw());
    EXPECT_TRUE(m_onCanDrawStateChangedCalled);
    m_onCanDrawStateChangedCalled = false;

    setupScrollAndContentsLayers(IntSize(100, 100));
    EXPECT_TRUE(m_hostImpl->canDraw());
    EXPECT_TRUE(m_onCanDrawStateChangedCalled);
    m_onCanDrawStateChangedCalled = false;

    // Toggle the device viewport size to make sure it toggles canDraw.
    m_hostImpl->setViewportSize(IntSize(100, 100), IntSize(0, 0));
    EXPECT_FALSE(m_hostImpl->canDraw());
    EXPECT_TRUE(m_onCanDrawStateChangedCalled);
    m_onCanDrawStateChangedCalled = false;

    m_hostImpl->setViewportSize(IntSize(100, 100), IntSize(100, 100));
    EXPECT_TRUE(m_hostImpl->canDraw());
    EXPECT_TRUE(m_onCanDrawStateChangedCalled);
    m_onCanDrawStateChangedCalled = false;

    // Toggle contents textures purged without causing any evictions,
    // and make sure that it does not change canDraw.
    setReduceMemoryResult(false);
    m_hostImpl->setManagedMemoryPolicy(ManagedMemoryPolicy(
        m_hostImpl->memoryAllocationLimitBytes() - 1));
    EXPECT_TRUE(m_hostImpl->canDraw());
    EXPECT_FALSE(m_onCanDrawStateChangedCalled);
    m_onCanDrawStateChangedCalled = false;

    // Toggle contents textures purged to make sure it toggles canDraw.
    setReduceMemoryResult(true);
    m_hostImpl->setManagedMemoryPolicy(ManagedMemoryPolicy(
        m_hostImpl->memoryAllocationLimitBytes() - 1));
    EXPECT_FALSE(m_hostImpl->canDraw());
    EXPECT_TRUE(m_onCanDrawStateChangedCalled);
    m_onCanDrawStateChangedCalled = false;

    m_hostImpl->resetContentsTexturesPurged();
    EXPECT_TRUE(m_hostImpl->canDraw());
    EXPECT_TRUE(m_onCanDrawStateChangedCalled);
    m_onCanDrawStateChangedCalled = false;
}

TEST_P(LayerTreeHostImplTest, scrollDeltaNoLayers)
{
    ASSERT_FALSE(m_hostImpl->rootLayer());

    scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
    ASSERT_EQ(scrollInfo->scrolls.size(), 0u);
}

TEST_P(LayerTreeHostImplTest, scrollDeltaTreeButNoChanges)
{
    {
        scoped_ptr<LayerImpl> root = LayerImpl::create(1);
        root->addChild(LayerImpl::create(2));
        root->addChild(LayerImpl::create(3));
        root->children()[1]->addChild(LayerImpl::create(4));
        root->children()[1]->addChild(LayerImpl::create(5));
        root->children()[1]->children()[0]->addChild(LayerImpl::create(6));
        m_hostImpl->setRootLayer(root.Pass());
    }
    LayerImpl* root = m_hostImpl->rootLayer();

    expectClearedScrollDeltasRecursive(root);

    scoped_ptr<ScrollAndScaleSet> scrollInfo;

    scrollInfo = m_hostImpl->processScrollDeltas();
    ASSERT_EQ(scrollInfo->scrolls.size(), 0u);
    expectClearedScrollDeltasRecursive(root);

    scrollInfo = m_hostImpl->processScrollDeltas();
    ASSERT_EQ(scrollInfo->scrolls.size(), 0u);
    expectClearedScrollDeltasRecursive(root);
}

TEST_P(LayerTreeHostImplTest, scrollDeltaRepeatedScrolls)
{
    IntPoint scrollPosition(20, 30);
    IntSize scrollDelta(11, -15);
    {
        scoped_ptr<LayerImpl> root = LayerImpl::create(1);
        root->setScrollPosition(scrollPosition);
        root->setScrollable(true);
        root->setMaxScrollPosition(IntSize(100, 100));
        root->scrollBy(scrollDelta);
        m_hostImpl->setRootLayer(root.Pass());
    }
    LayerImpl* root = m_hostImpl->rootLayer();

    scoped_ptr<ScrollAndScaleSet> scrollInfo;

    scrollInfo = m_hostImpl->processScrollDeltas();
    ASSERT_EQ(scrollInfo->scrolls.size(), 1u);
    EXPECT_EQ(root->sentScrollDelta(), scrollDelta);
    expectContains(*scrollInfo, root->id(), scrollDelta);

    IntSize scrollDelta2(-5, 27);
    root->scrollBy(scrollDelta2);
    scrollInfo = m_hostImpl->processScrollDeltas();
    ASSERT_EQ(scrollInfo->scrolls.size(), 1u);
    EXPECT_EQ(root->sentScrollDelta(), scrollDelta + scrollDelta2);
    expectContains(*scrollInfo, root->id(), scrollDelta + scrollDelta2);

    root->scrollBy(IntSize());
    scrollInfo = m_hostImpl->processScrollDeltas();
    EXPECT_EQ(root->sentScrollDelta(), scrollDelta + scrollDelta2);
}

TEST_P(LayerTreeHostImplTest, scrollRootCallsCommitAndRedraw)
{
    setupScrollAndContentsLayers(IntSize(100, 100));
    m_hostImpl->setViewportSize(IntSize(50, 50), IntSize(50, 50));
    initializeRendererAndDrawFrame();

    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), IntSize(0, 10));
    m_hostImpl->scrollEnd();
    EXPECT_TRUE(m_didRequestRedraw);
    EXPECT_TRUE(m_didRequestCommit);
}

TEST_P(LayerTreeHostImplTest, scrollWithoutRootLayer)
{
    // We should not crash when trying to scroll an empty layer tree.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Wheel), InputHandlerClient::ScrollIgnored);
}

TEST_P(LayerTreeHostImplTest, scrollWithoutRenderer)
{
    LayerTreeSettings settings;
    m_hostImpl = LayerTreeHostImpl::create(settings, this);

    // Initialization will fail here.
    m_hostImpl->initializeRenderer(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new FakeWebGraphicsContext3DMakeCurrentFails)).PassAs<GraphicsContext>());
    m_hostImpl->setViewportSize(IntSize(10, 10), IntSize(10, 10));

    setupScrollAndContentsLayers(IntSize(100, 100));

    // We should not crash when trying to scroll after the renderer initialization fails.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Wheel), InputHandlerClient::ScrollIgnored);
}

TEST_P(LayerTreeHostImplTest, replaceTreeWhileScrolling)
{
    const int scrollLayerId = 1;

    setupScrollAndContentsLayers(IntSize(100, 100));
    m_hostImpl->setViewportSize(IntSize(50, 50), IntSize(50, 50));
    initializeRendererAndDrawFrame();

    // We should not crash if the tree is replaced while we are scrolling.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->detachLayerTree();

    setupScrollAndContentsLayers(IntSize(100, 100));

    // We should still be scrolling, because the scrolled layer also exists in the new tree.
    IntSize scrollDelta(0, 10);
    m_hostImpl->scrollBy(IntPoint(), scrollDelta);
    m_hostImpl->scrollEnd();
    scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
    expectContains(*scrollInfo, scrollLayerId, scrollDelta);
}

TEST_P(LayerTreeHostImplTest, clearRootRenderSurfaceAndScroll)
{
    setupScrollAndContentsLayers(IntSize(100, 100));
    m_hostImpl->setViewportSize(IntSize(50, 50), IntSize(50, 50));
    initializeRendererAndDrawFrame();

    // We should be able to scroll even if the root layer loses its render surface after the most
    // recent render.
    m_hostImpl->rootLayer()->clearRenderSurface();
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
}

TEST_P(LayerTreeHostImplTest, wheelEventHandlers)
{
    setupScrollAndContentsLayers(IntSize(100, 100));
    m_hostImpl->setViewportSize(IntSize(50, 50), IntSize(50, 50));
    initializeRendererAndDrawFrame();
    LayerImpl* root = m_hostImpl->rootLayer();

    root->setHaveWheelEventHandlers(true);

    // With registered event handlers, wheel scrolls have to go to the main thread.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Wheel), InputHandlerClient::ScrollOnMainThread);

    // But gesture scrolls can still be handled.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Gesture), InputHandlerClient::ScrollStarted);
}

TEST_P(LayerTreeHostImplTest, shouldScrollOnMainThread)
{
    setupScrollAndContentsLayers(IntSize(100, 100));
    m_hostImpl->setViewportSize(IntSize(50, 50), IntSize(50, 50));
    initializeRendererAndDrawFrame();
    LayerImpl* root = m_hostImpl->rootLayer();

    root->setShouldScrollOnMainThread(true);

    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Wheel), InputHandlerClient::ScrollOnMainThread);
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Gesture), InputHandlerClient::ScrollOnMainThread);
}

TEST_P(LayerTreeHostImplTest, nonFastScrollableRegionBasic)
{
    setupScrollAndContentsLayers(IntSize(200, 200));
    m_hostImpl->setViewportSize(IntSize(100, 100), IntSize(100, 100));
    initializeRendererAndDrawFrame();
    LayerImpl* root = m_hostImpl->rootLayer();

    root->setNonFastScrollableRegion(IntRect(0, 0, 50, 50));

    // All scroll types inside the non-fast scrollable region should fail.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(25, 25), InputHandlerClient::Wheel), InputHandlerClient::ScrollOnMainThread);
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(25, 25), InputHandlerClient::Gesture), InputHandlerClient::ScrollOnMainThread);

    // All scroll types outside this region should succeed.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(75, 75), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), IntSize(0, 10));
    m_hostImpl->scrollEnd();
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(75, 75), InputHandlerClient::Gesture), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), IntSize(0, 10));
    m_hostImpl->scrollEnd();
}

TEST_P(LayerTreeHostImplTest, nonFastScrollableRegionWithOffset)
{
    setupScrollAndContentsLayers(IntSize(200, 200));
    m_hostImpl->setViewportSize(IntSize(100, 100), IntSize(100, 100));
    LayerImpl* root = m_hostImpl->rootLayer();

    root->setNonFastScrollableRegion(IntRect(0, 0, 50, 50));
    root->setPosition(FloatPoint(-25, 0));
    initializeRendererAndDrawFrame();

    // This point would fall into the non-fast scrollable region except that we've moved the layer down by 25 pixels.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(40, 10), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), IntSize(0, 1));
    m_hostImpl->scrollEnd();

    // This point is still inside the non-fast region.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(10, 10), InputHandlerClient::Wheel), InputHandlerClient::ScrollOnMainThread);
}

TEST_P(LayerTreeHostImplTest, maxScrollPositionChangedByDeviceScaleFactor)
{
    setupScrollAndContentsLayers(IntSize(100, 100));

    float deviceScaleFactor = 2;
    IntSize layoutViewport(25, 25);
    IntSize deviceViewport(layoutViewport);
    deviceViewport.scale(deviceScaleFactor);
    m_hostImpl->setViewportSize(layoutViewport, deviceViewport);
    m_hostImpl->setDeviceScaleFactor(deviceScaleFactor);
    EXPECT_EQ(m_hostImpl->rootLayer()->maxScrollPosition(), IntSize(25, 25));

    deviceScaleFactor = 1;
    m_hostImpl->setViewportSize(layoutViewport, layoutViewport);
    m_hostImpl->setDeviceScaleFactor(deviceScaleFactor);
    EXPECT_EQ(m_hostImpl->rootLayer()->maxScrollPosition(), IntSize(75, 75));
}

TEST_P(LayerTreeHostImplTest, implPinchZoom)
{
    // This test is specific to the page-scale based pinch zoom.
    if (!Settings::pageScalePinchZoomEnabled())
        return;

    setupScrollAndContentsLayers(IntSize(100, 100));
    m_hostImpl->setViewportSize(IntSize(50, 50), IntSize(50, 50));
    initializeRendererAndDrawFrame();

    LayerImpl* scrollLayer = m_hostImpl->rootScrollLayer();
    DCHECK(scrollLayer);

    const float minPageScale = 1, maxPageScale = 4;
    const WebTransformationMatrix identityScaleTransform;

    // The impl-based pinch zoome should not adjust the max scroll position.
    {
        m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
        scrollLayer->setImplTransform(identityScaleTransform);
        scrollLayer->setScrollDelta(IntSize());

        float pageScaleDelta = 2;
        m_hostImpl->pinchGestureBegin();
        m_hostImpl->pinchGestureUpdate(pageScaleDelta, IntPoint(50, 50));
        m_hostImpl->pinchGestureEnd();
        EXPECT_TRUE(m_didRequestRedraw);
        EXPECT_TRUE(m_didRequestCommit);

        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        EXPECT_EQ(scrollInfo->pageScaleDelta, pageScaleDelta);

        EXPECT_EQ(m_hostImpl->rootLayer()->maxScrollPosition(), IntSize(50, 50));
    }

    // Scrolling after a pinch gesture should always be in local space.  The scroll deltas do not
    // have the page scale factor applied.
    {
        m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
        scrollLayer->setImplTransform(identityScaleTransform);
        scrollLayer->setScrollDelta(IntSize());

        float pageScaleDelta = 2;
        m_hostImpl->pinchGestureBegin();
        m_hostImpl->pinchGestureUpdate(pageScaleDelta, IntPoint(0, 0));
        m_hostImpl->pinchGestureEnd();

        IntSize scrollDelta(0, 10);
        EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
        m_hostImpl->scrollBy(IntPoint(), scrollDelta);
        m_hostImpl->scrollEnd();

        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        expectContains(*scrollInfo.get(), m_hostImpl->rootLayer()->id(), scrollDelta);
    }
}

TEST_P(LayerTreeHostImplTest, pinchGesture)
{
    setupScrollAndContentsLayers(IntSize(100, 100));
    m_hostImpl->setViewportSize(IntSize(50, 50), IntSize(50, 50));
    initializeRendererAndDrawFrame();

    LayerImpl* scrollLayer = m_hostImpl->rootScrollLayer();
    DCHECK(scrollLayer);

    const float minPageScale = Settings::pageScalePinchZoomEnabled() ? 1 : 0.5;
    const float maxPageScale = 4;
    const WebTransformationMatrix identityScaleTransform;

    // Basic pinch zoom in gesture
    {
        m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
        scrollLayer->setImplTransform(identityScaleTransform);
        scrollLayer->setScrollDelta(IntSize());

        float pageScaleDelta = 2;
        m_hostImpl->pinchGestureBegin();
        m_hostImpl->pinchGestureUpdate(pageScaleDelta, IntPoint(50, 50));
        m_hostImpl->pinchGestureEnd();
        EXPECT_TRUE(m_didRequestRedraw);
        EXPECT_TRUE(m_didRequestCommit);

        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        EXPECT_EQ(scrollInfo->pageScaleDelta, pageScaleDelta);
    }

    // Zoom-in clamping
    {
        m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
        scrollLayer->setImplTransform(identityScaleTransform);
        scrollLayer->setScrollDelta(IntSize());
        float pageScaleDelta = 10;

        m_hostImpl->pinchGestureBegin();
        m_hostImpl->pinchGestureUpdate(pageScaleDelta, IntPoint(50, 50));
        m_hostImpl->pinchGestureEnd();

        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        EXPECT_EQ(scrollInfo->pageScaleDelta, maxPageScale);
    }

    // Zoom-out clamping
    {
        m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
        scrollLayer->setImplTransform(identityScaleTransform);
        scrollLayer->setScrollDelta(IntSize());
        scrollLayer->setScrollPosition(IntPoint(50, 50));

        float pageScaleDelta = 0.1f;
        m_hostImpl->pinchGestureBegin();
        m_hostImpl->pinchGestureUpdate(pageScaleDelta, IntPoint(0, 0));
        m_hostImpl->pinchGestureEnd();

        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        EXPECT_EQ(scrollInfo->pageScaleDelta, minPageScale);

        if (!Settings::pageScalePinchZoomEnabled()) {
            // Pushed to (0,0) via clamping against contents layer size.
            expectContains(*scrollInfo, scrollLayer->id(), IntSize(-50, -50));
        } else {
            EXPECT_TRUE(scrollInfo->scrolls.empty());
        }
    }

    // Two-finger panning
    {
        m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
        scrollLayer->setImplTransform(identityScaleTransform);
        scrollLayer->setScrollDelta(IntSize());
        scrollLayer->setScrollPosition(IntPoint(20, 20));

        float pageScaleDelta = 1;
        m_hostImpl->pinchGestureBegin();
        m_hostImpl->pinchGestureUpdate(pageScaleDelta, IntPoint(10, 10));
        m_hostImpl->pinchGestureUpdate(pageScaleDelta, IntPoint(20, 20));
        m_hostImpl->pinchGestureEnd();

        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        EXPECT_EQ(scrollInfo->pageScaleDelta, pageScaleDelta);
        expectContains(*scrollInfo, scrollLayer->id(), IntSize(-10, -10));
    }
}

TEST_P(LayerTreeHostImplTest, pageScaleAnimation)
{
    setupScrollAndContentsLayers(IntSize(100, 100));
    m_hostImpl->setViewportSize(IntSize(50, 50), IntSize(50, 50));
    initializeRendererAndDrawFrame();

    LayerImpl* scrollLayer = m_hostImpl->rootScrollLayer();
    DCHECK(scrollLayer);

    const float minPageScale = Settings::pageScalePinchZoomEnabled() ? 1 : 0.5;
    const float maxPageScale = 4;
    const base::TimeTicks startTime = base::TimeTicks() + base::TimeDelta::FromSeconds(1);
    const base::TimeDelta duration = base::TimeDelta::FromMilliseconds(100);
    const base::TimeTicks halfwayThroughAnimation = startTime + duration / 2;
    const base::TimeTicks endTime = startTime + duration;
    const WebTransformationMatrix identityScaleTransform;

    // Non-anchor zoom-in
    {
        m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
        scrollLayer->setImplTransform(identityScaleTransform);
        scrollLayer->setScrollPosition(IntPoint(50, 50));

        m_hostImpl->startPageScaleAnimation(IntSize(0, 0), false, 2, startTime, duration);
        m_hostImpl->animate(halfwayThroughAnimation, base::Time());
        EXPECT_TRUE(m_didRequestRedraw);
        m_hostImpl->animate(endTime, base::Time());
        EXPECT_TRUE(m_didRequestCommit);

        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        EXPECT_EQ(scrollInfo->pageScaleDelta, 2);
        expectContains(*scrollInfo, scrollLayer->id(), IntSize(-50, -50));
    }

    // Anchor zoom-out
    {
        m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
        scrollLayer->setImplTransform(identityScaleTransform);
        scrollLayer->setScrollPosition(IntPoint(50, 50));

        m_hostImpl->startPageScaleAnimation(IntSize(25, 25), true, minPageScale, startTime, duration);
        m_hostImpl->animate(endTime, base::Time());
        EXPECT_TRUE(m_didRequestRedraw);
        EXPECT_TRUE(m_didRequestCommit);

        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        EXPECT_EQ(scrollInfo->pageScaleDelta, minPageScale);
        // Pushed to (0,0) via clamping against contents layer size.
        expectContains(*scrollInfo, scrollLayer->id(), IntSize(-50, -50));
    }
}

TEST_P(LayerTreeHostImplTest, inhibitScrollAndPageScaleUpdatesWhilePinchZooming)
{
    setupScrollAndContentsLayers(IntSize(100, 100));
    m_hostImpl->setViewportSize(IntSize(50, 50), IntSize(50, 50));
    initializeRendererAndDrawFrame();

    LayerImpl* scrollLayer = m_hostImpl->rootScrollLayer();
    DCHECK(scrollLayer);

    const float minPageScale = Settings::pageScalePinchZoomEnabled() ? 1 : 0.5;
    const float maxPageScale = 4;

    // Pinch zoom in.
    {
        // Start a pinch in gesture at the bottom right corner of the viewport.
        const float zoomInDelta = 2;
        m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
        m_hostImpl->pinchGestureBegin();
        m_hostImpl->pinchGestureUpdate(zoomInDelta, IntPoint(50, 50));

        // Because we are pinch zooming in, we shouldn't get any scroll or page
        // scale deltas.
        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        EXPECT_EQ(scrollInfo->pageScaleDelta, 1);
        EXPECT_EQ(scrollInfo->scrolls.size(), 0u);

        // Once the gesture ends, we get the final scroll and page scale values.
        m_hostImpl->pinchGestureEnd();
        scrollInfo = m_hostImpl->processScrollDeltas();
        EXPECT_EQ(scrollInfo->pageScaleDelta, zoomInDelta);
        if (!Settings::pageScalePinchZoomEnabled()) {
            expectContains(*scrollInfo, scrollLayer->id(), IntSize(25, 25));
        } else {
            EXPECT_TRUE(scrollInfo->scrolls.empty());
        }
    }

    // Pinch zoom out.
    {
        // Start a pinch out gesture at the bottom right corner of the viewport.
        const float zoomOutDelta = 0.75;
        m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
        m_hostImpl->pinchGestureBegin();
        m_hostImpl->pinchGestureUpdate(zoomOutDelta, IntPoint(50, 50));

        // Since we are pinch zooming out, we should get an update to zoom all
        // the way out to the minimum page scale.
        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        if (!Settings::pageScalePinchZoomEnabled()) {
            EXPECT_EQ(scrollInfo->pageScaleDelta, minPageScale);
            expectContains(*scrollInfo, scrollLayer->id(), IntSize(0, 0));
        } else {
            EXPECT_EQ(scrollInfo->pageScaleDelta, 1);
            EXPECT_TRUE(scrollInfo->scrolls.empty());
        }

        // Once the gesture ends, we get the final scroll and page scale values.
        m_hostImpl->pinchGestureEnd();
        scrollInfo = m_hostImpl->processScrollDeltas();
        if (Settings::pageScalePinchZoomEnabled()) {
            EXPECT_EQ(scrollInfo->pageScaleDelta, minPageScale);
            expectContains(*scrollInfo, scrollLayer->id(), IntSize(25, 25));
        } else {
            EXPECT_EQ(scrollInfo->pageScaleDelta, zoomOutDelta);
            expectContains(*scrollInfo, scrollLayer->id(), IntSize(8, 8));
        }
    }
}

TEST_P(LayerTreeHostImplTest, inhibitScrollAndPageScaleUpdatesWhileAnimatingPageScale)
{
    setupScrollAndContentsLayers(IntSize(100, 100));
    m_hostImpl->setViewportSize(IntSize(50, 50), IntSize(50, 50));
    initializeRendererAndDrawFrame();

    LayerImpl* scrollLayer = m_hostImpl->rootScrollLayer();
    DCHECK(scrollLayer);

    const float minPageScale = Settings::pageScalePinchZoomEnabled() ? 1 : 0.5;
    const float maxPageScale = 4;
    const base::TimeTicks startTime = base::TimeTicks() + base::TimeDelta::FromSeconds(1);
    const base::TimeDelta duration = base::TimeDelta::FromMilliseconds(100);
    const base::TimeTicks halfwayThroughAnimation = startTime + duration / 2;
    const base::TimeTicks endTime = startTime + duration;
    // Start a page scale animation.
    const float pageScaleDelta = 2;
    m_hostImpl->setPageScaleFactorAndLimits(1, minPageScale, maxPageScale);
    m_hostImpl->startPageScaleAnimation(IntSize(50, 50), false, pageScaleDelta, startTime, duration);

    // We should immediately get the final zoom and scroll values for the
    // animation.
    m_hostImpl->animate(halfwayThroughAnimation, base::Time());
    scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();

    if (!Settings::pageScalePinchZoomEnabled()) {
        EXPECT_EQ(scrollInfo->pageScaleDelta, pageScaleDelta);
        expectContains(*scrollInfo, scrollLayer->id(), IntSize(25, 25));
    } else {
        EXPECT_EQ(scrollInfo->pageScaleDelta, 1);
        EXPECT_TRUE(scrollInfo->scrolls.empty());
    }

    // Scrolling during the animation is ignored.
    const IntSize scrollDelta(0, 10);
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(25, 25), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), scrollDelta);
    m_hostImpl->scrollEnd();

    // The final page scale and scroll deltas should match what we got
    // earlier.
    m_hostImpl->animate(endTime, base::Time());
    scrollInfo = m_hostImpl->processScrollDeltas();
    EXPECT_EQ(scrollInfo->pageScaleDelta, pageScaleDelta);
    expectContains(*scrollInfo, scrollLayer->id(), IntSize(25, 25));
}

class DidDrawCheckLayer : public TiledLayerImpl {
public:
    static scoped_ptr<LayerImpl> create(int id) { return scoped_ptr<LayerImpl>(new DidDrawCheckLayer(id)); }

    virtual void didDraw(ResourceProvider*) OVERRIDE
    {
        m_didDrawCalled = true;
    }

    virtual void willDraw(ResourceProvider*) OVERRIDE
    {
        m_willDrawCalled = true;
    }

    bool didDrawCalled() const { return m_didDrawCalled; }
    bool willDrawCalled() const { return m_willDrawCalled; }

    void clearDidDrawCheck()
    {
        m_didDrawCalled = false;
        m_willDrawCalled = false;
    }

protected:
    explicit DidDrawCheckLayer(int id)
        : TiledLayerImpl(id)
        , m_didDrawCalled(false)
        , m_willDrawCalled(false)
    {
        setAnchorPoint(FloatPoint(0, 0));
        setBounds(IntSize(10, 10));
        setContentBounds(IntSize(10, 10));
        setDrawsContent(true);
        setSkipsDraw(false);
        setVisibleContentRect(IntRect(0, 0, 10, 10));

        scoped_ptr<LayerTilingData> tiler = LayerTilingData::create(IntSize(100, 100), LayerTilingData::HasBorderTexels);
        tiler->setBounds(contentBounds());
        setTilingData(*tiler.get());
    }

private:
    bool m_didDrawCalled;
    bool m_willDrawCalled;
};

TEST_P(LayerTreeHostImplTest, didDrawNotCalledOnHiddenLayer)
{
    // The root layer is always drawn, so run this test on a child layer that
    // will be masked out by the root layer's bounds.
    m_hostImpl->setRootLayer(DidDrawCheckLayer::create(1));
    DidDrawCheckLayer* root = static_cast<DidDrawCheckLayer*>(m_hostImpl->rootLayer());
    root->setMasksToBounds(true);

    root->addChild(DidDrawCheckLayer::create(2));
    DidDrawCheckLayer* layer = static_cast<DidDrawCheckLayer*>(root->children()[0]);
    // Ensure visibleContentRect for layer is empty
    layer->setPosition(FloatPoint(100, 100));
    layer->setBounds(IntSize(10, 10));
    layer->setContentBounds(IntSize(10, 10));

    LayerTreeHostImpl::FrameData frame;

    EXPECT_FALSE(layer->willDrawCalled());
    EXPECT_FALSE(layer->didDrawCalled());

    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);

    EXPECT_FALSE(layer->willDrawCalled());
    EXPECT_FALSE(layer->didDrawCalled());

    EXPECT_TRUE(layer->visibleContentRect().isEmpty());

    // Ensure visibleContentRect for layer layer is not empty
    layer->setPosition(FloatPoint(0, 0));

    EXPECT_FALSE(layer->willDrawCalled());
    EXPECT_FALSE(layer->didDrawCalled());

    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);

    EXPECT_TRUE(layer->willDrawCalled());
    EXPECT_TRUE(layer->didDrawCalled());

    EXPECT_FALSE(layer->visibleContentRect().isEmpty());
}

TEST_P(LayerTreeHostImplTest, willDrawNotCalledOnOccludedLayer)
{
    IntSize bigSize(1000, 1000);
    m_hostImpl->setViewportSize(bigSize, bigSize);

    m_hostImpl->setRootLayer(DidDrawCheckLayer::create(1));
    DidDrawCheckLayer* root = static_cast<DidDrawCheckLayer*>(m_hostImpl->rootLayer());

    root->addChild(DidDrawCheckLayer::create(2));
    DidDrawCheckLayer* occludedLayer = static_cast<DidDrawCheckLayer*>(root->children()[0]);

    root->addChild(DidDrawCheckLayer::create(3));
    DidDrawCheckLayer* topLayer = static_cast<DidDrawCheckLayer*>(root->children()[1]);
    // This layer covers the occludedLayer above. Make this layer large so it can occlude.
    topLayer->setBounds(bigSize);
    topLayer->setContentBounds(bigSize);
    topLayer->setContentsOpaque(true);

    LayerTreeHostImpl::FrameData frame;

    EXPECT_FALSE(occludedLayer->willDrawCalled());
    EXPECT_FALSE(occludedLayer->didDrawCalled());
    EXPECT_FALSE(topLayer->willDrawCalled());
    EXPECT_FALSE(topLayer->didDrawCalled());

    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);

    EXPECT_FALSE(occludedLayer->willDrawCalled());
    EXPECT_FALSE(occludedLayer->didDrawCalled());
    EXPECT_TRUE(topLayer->willDrawCalled());
    EXPECT_TRUE(topLayer->didDrawCalled());
}

TEST_P(LayerTreeHostImplTest, didDrawCalledOnAllLayers)
{
    m_hostImpl->setRootLayer(DidDrawCheckLayer::create(1));
    DidDrawCheckLayer* root = static_cast<DidDrawCheckLayer*>(m_hostImpl->rootLayer());

    root->addChild(DidDrawCheckLayer::create(2));
    DidDrawCheckLayer* layer1 = static_cast<DidDrawCheckLayer*>(root->children()[0]);

    layer1->addChild(DidDrawCheckLayer::create(3));
    DidDrawCheckLayer* layer2 = static_cast<DidDrawCheckLayer*>(layer1->children()[0]);

    layer1->setOpacity(0.3f);
    layer1->setPreserves3D(false);

    EXPECT_FALSE(root->didDrawCalled());
    EXPECT_FALSE(layer1->didDrawCalled());
    EXPECT_FALSE(layer2->didDrawCalled());

    LayerTreeHostImpl::FrameData frame;
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);

    EXPECT_TRUE(root->didDrawCalled());
    EXPECT_TRUE(layer1->didDrawCalled());
    EXPECT_TRUE(layer2->didDrawCalled());

    EXPECT_NE(root->renderSurface(), layer1->renderSurface());
    EXPECT_TRUE(!!layer1->renderSurface());
}

class MissingTextureAnimatingLayer : public DidDrawCheckLayer {
public:
    static scoped_ptr<LayerImpl> create(int id, bool tileMissing, bool skipsDraw, bool animating, ResourceProvider* resourceProvider)
    {
        return scoped_ptr<LayerImpl>(new MissingTextureAnimatingLayer(id, tileMissing, skipsDraw, animating, resourceProvider));
    }

private:
    explicit MissingTextureAnimatingLayer(int id, bool tileMissing, bool skipsDraw, bool animating, ResourceProvider* resourceProvider)
        : DidDrawCheckLayer(id)
    {
        scoped_ptr<LayerTilingData> tilingData = LayerTilingData::create(IntSize(10, 10), LayerTilingData::NoBorderTexels);
        tilingData->setBounds(bounds());
        setTilingData(*tilingData.get());
        setSkipsDraw(skipsDraw);
        if (!tileMissing) {
            ResourceProvider::ResourceId resource = resourceProvider->createResource(Renderer::ContentPool, IntSize(), GL_RGBA, ResourceProvider::TextureUsageAny);
            pushTileProperties(0, 0, resource, IntRect(), false);
        }
        if (animating)
            addAnimatedTransformToLayer(*this, 10, 3, 0);
    }
};

TEST_P(LayerTreeHostImplTest, prepareToDrawFailsWhenAnimationUsesCheckerboard)
{
    // When the texture is not missing, we draw as usual.
    m_hostImpl->setRootLayer(DidDrawCheckLayer::create(1));
    DidDrawCheckLayer* root = static_cast<DidDrawCheckLayer*>(m_hostImpl->rootLayer());
    root->addChild(MissingTextureAnimatingLayer::create(2, false, false, true, m_hostImpl->resourceProvider()));

    LayerTreeHostImpl::FrameData frame;

    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);

    // When a texture is missing and we're not animating, we draw as usual with checkerboarding.
    m_hostImpl->setRootLayer(DidDrawCheckLayer::create(1));
    root = static_cast<DidDrawCheckLayer*>(m_hostImpl->rootLayer());
    root->addChild(MissingTextureAnimatingLayer::create(2, true, false, false, m_hostImpl->resourceProvider()));

    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);

    // When a texture is missing and we're animating, we don't want to draw anything.
    m_hostImpl->setRootLayer(DidDrawCheckLayer::create(1));
    root = static_cast<DidDrawCheckLayer*>(m_hostImpl->rootLayer());
    root->addChild(MissingTextureAnimatingLayer::create(2, true, false, true, m_hostImpl->resourceProvider()));

    EXPECT_FALSE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);

    // When the layer skips draw and we're animating, we still draw the frame.
    m_hostImpl->setRootLayer(DidDrawCheckLayer::create(1));
    root = static_cast<DidDrawCheckLayer*>(m_hostImpl->rootLayer());
    root->addChild(MissingTextureAnimatingLayer::create(2, false, true, true, m_hostImpl->resourceProvider()));

    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);
}

TEST_P(LayerTreeHostImplTest, scrollRootIgnored)
{
    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    root->setScrollable(false);
    m_hostImpl->setRootLayer(root.Pass());
    initializeRendererAndDrawFrame();

    // Scroll event is ignored because layer is not scrollable.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Wheel), InputHandlerClient::ScrollIgnored);
    EXPECT_FALSE(m_didRequestRedraw);
    EXPECT_FALSE(m_didRequestCommit);
}

TEST_P(LayerTreeHostImplTest, scrollNonCompositedRoot)
{
    // Test the configuration where a non-composited root layer is embedded in a
    // scrollable outer layer.
    IntSize surfaceSize(10, 10);

    scoped_ptr<LayerImpl> contentLayer = LayerImpl::create(1);
    contentLayer->setUseLCDText(true);
    contentLayer->setDrawsContent(true);
    contentLayer->setPosition(FloatPoint(0, 0));
    contentLayer->setAnchorPoint(FloatPoint(0, 0));
    contentLayer->setBounds(surfaceSize);
    contentLayer->setContentBounds(IntSize(surfaceSize.width() * 2, surfaceSize.height() * 2));

    scoped_ptr<LayerImpl> scrollLayer = LayerImpl::create(2);
    scrollLayer->setScrollable(true);
    scrollLayer->setMaxScrollPosition(surfaceSize);
    scrollLayer->setBounds(surfaceSize);
    scrollLayer->setContentBounds(surfaceSize);
    scrollLayer->setPosition(FloatPoint(0, 0));
    scrollLayer->setAnchorPoint(FloatPoint(0, 0));
    scrollLayer->addChild(contentLayer.Pass());

    m_hostImpl->setRootLayer(scrollLayer.Pass());
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();

    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), IntSize(0, 10));
    m_hostImpl->scrollEnd();
    EXPECT_TRUE(m_didRequestRedraw);
    EXPECT_TRUE(m_didRequestCommit);
}

TEST_P(LayerTreeHostImplTest, scrollChildCallsCommitAndRedraw)
{
    IntSize surfaceSize(10, 10);
    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    root->setBounds(surfaceSize);
    root->setContentBounds(surfaceSize);
    root->addChild(createScrollableLayer(2, surfaceSize));
    m_hostImpl->setRootLayer(root.Pass());
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();

    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), IntSize(0, 10));
    m_hostImpl->scrollEnd();
    EXPECT_TRUE(m_didRequestRedraw);
    EXPECT_TRUE(m_didRequestCommit);
}

TEST_P(LayerTreeHostImplTest, scrollMissesChild)
{
    IntSize surfaceSize(10, 10);
    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    root->addChild(createScrollableLayer(2, surfaceSize));
    m_hostImpl->setRootLayer(root.Pass());
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();

    // Scroll event is ignored because the input coordinate is outside the layer boundaries.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(15, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollIgnored);
    EXPECT_FALSE(m_didRequestRedraw);
    EXPECT_FALSE(m_didRequestCommit);
}

TEST_P(LayerTreeHostImplTest, scrollMissesBackfacingChild)
{
    IntSize surfaceSize(10, 10);
    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    scoped_ptr<LayerImpl> child = createScrollableLayer(2, surfaceSize);
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);

    WebTransformationMatrix matrix;
    matrix.rotate3d(180, 0, 0);
    child->setTransform(matrix);
    child->setDoubleSided(false);

    root->addChild(child.Pass());
    m_hostImpl->setRootLayer(root.Pass());
    initializeRendererAndDrawFrame();

    // Scroll event is ignored because the scrollable layer is not facing the viewer and there is
    // nothing scrollable behind it.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollIgnored);
    EXPECT_FALSE(m_didRequestRedraw);
    EXPECT_FALSE(m_didRequestCommit);
}

TEST_P(LayerTreeHostImplTest, scrollBlockedByContentLayer)
{
    IntSize surfaceSize(10, 10);
    scoped_ptr<LayerImpl> contentLayer = createScrollableLayer(1, surfaceSize);
    contentLayer->setShouldScrollOnMainThread(true);
    contentLayer->setScrollable(false);

    scoped_ptr<LayerImpl> scrollLayer = createScrollableLayer(2, surfaceSize);
    scrollLayer->addChild(contentLayer.Pass());

    m_hostImpl->setRootLayer(scrollLayer.Pass());
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();

    // Scrolling fails because the content layer is asking to be scrolled on the main thread.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollOnMainThread);
}

TEST_P(LayerTreeHostImplTest, scrollRootAndChangePageScaleOnMainThread)
{
    IntSize surfaceSize(10, 10);
    float pageScale = 2;
    scoped_ptr<LayerImpl> root = createScrollableLayer(1, surfaceSize);
    m_hostImpl->setRootLayer(root.Pass());
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();

    IntSize scrollDelta(0, 10);
    IntSize expectedScrollDelta(scrollDelta);
    IntSize expectedMaxScroll(m_hostImpl->rootLayer()->maxScrollPosition());
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), scrollDelta);
    m_hostImpl->scrollEnd();

    // Set new page scale from main thread.
    m_hostImpl->setPageScaleFactorAndLimits(pageScale, pageScale, pageScale);

    if (!Settings::pageScalePinchZoomEnabled()) {
        // The scale should apply to the scroll delta.
        expectedScrollDelta.scale(pageScale);
    }
    scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
    expectContains(*scrollInfo.get(), m_hostImpl->rootLayer()->id(), expectedScrollDelta);

    // The scroll range should also have been updated.
    EXPECT_EQ(m_hostImpl->rootLayer()->maxScrollPosition(), expectedMaxScroll);

    // The page scale delta remains constant because the impl thread did not scale.
    EXPECT_EQ(m_hostImpl->rootLayer()->implTransform(), WebTransformationMatrix());
}

TEST_P(LayerTreeHostImplTest, scrollRootAndChangePageScaleOnImplThread)
{
    IntSize surfaceSize(10, 10);
    float pageScale = 2;
    scoped_ptr<LayerImpl> root = createScrollableLayer(1, surfaceSize);
    m_hostImpl->setRootLayer(root.Pass());
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    m_hostImpl->setPageScaleFactorAndLimits(1, 1, pageScale);
    initializeRendererAndDrawFrame();

    IntSize scrollDelta(0, 10);
    IntSize expectedScrollDelta(scrollDelta);
    IntSize expectedMaxScroll(m_hostImpl->rootLayer()->maxScrollPosition());
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), scrollDelta);
    m_hostImpl->scrollEnd();

    // Set new page scale on impl thread by pinching.
    m_hostImpl->pinchGestureBegin();
    m_hostImpl->pinchGestureUpdate(pageScale, IntPoint());
    m_hostImpl->pinchGestureEnd();
    m_hostImpl->updateRootScrollLayerImplTransform();

    // The scroll delta is not scaled because the main thread did not scale.
    scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
    expectContains(*scrollInfo.get(), m_hostImpl->rootLayer()->id(), expectedScrollDelta);

    // The scroll range should also have been updated.
    EXPECT_EQ(m_hostImpl->rootLayer()->maxScrollPosition(), expectedMaxScroll);

    // The page scale delta should match the new scale on the impl side.
    WebTransformationMatrix expectedScale;
    expectedScale.scale(pageScale);
    EXPECT_EQ(m_hostImpl->rootLayer()->implTransform(), expectedScale);
}

TEST_P(LayerTreeHostImplTest, pageScaleDeltaAppliedToRootScrollLayerOnly)
{
    IntSize surfaceSize(10, 10);
    float defaultPageScale = 1;
    WebTransformationMatrix defaultPageScaleMatrix;

    float newPageScale = 2;
    WebTransformationMatrix newPageScaleMatrix;
    newPageScaleMatrix.scale(newPageScale);

    // Create a normal scrollable root layer and another scrollable child layer.
    setupScrollAndContentsLayers(surfaceSize);
    LayerImpl* root = m_hostImpl->rootLayer();
    LayerImpl* child = root->children()[0];

    scoped_ptr<LayerImpl> scrollableChild = createScrollableLayer(3, surfaceSize);
    child->addChild(scrollableChild.Pass());
    LayerImpl* grandChild = child->children()[0];

    // Set new page scale on impl thread by pinching.
    m_hostImpl->pinchGestureBegin();
    m_hostImpl->pinchGestureUpdate(newPageScale, IntPoint());
    m_hostImpl->pinchGestureEnd();
    m_hostImpl->updateRootScrollLayerImplTransform();

    // The page scale delta should only be applied to the scrollable root layer.
    EXPECT_EQ(root->implTransform(), newPageScaleMatrix);
    EXPECT_EQ(child->implTransform(), defaultPageScaleMatrix);
    EXPECT_EQ(grandChild->implTransform(), defaultPageScaleMatrix);

    // Make sure all the layers are drawn with the page scale delta applied, i.e., the page scale
    // delta on the root layer is applied hierarchically.
    LayerTreeHostImpl::FrameData frame;
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);

    EXPECT_EQ(root->drawTransform().m11(), newPageScale);
    EXPECT_EQ(root->drawTransform().m22(), newPageScale);
    EXPECT_EQ(child->drawTransform().m11(), newPageScale);
    EXPECT_EQ(child->drawTransform().m22(), newPageScale);
    EXPECT_EQ(grandChild->drawTransform().m11(), newPageScale);
    EXPECT_EQ(grandChild->drawTransform().m22(), newPageScale);
}

TEST_P(LayerTreeHostImplTest, scrollChildAndChangePageScaleOnMainThread)
{
    IntSize surfaceSize(10, 10);
    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    root->setBounds(surfaceSize);
    root->setContentBounds(surfaceSize);
    // Also mark the root scrollable so it becomes the root scroll layer.
    root->setScrollable(true);
    int scrollLayerId = 2;
    root->addChild(createScrollableLayer(scrollLayerId, surfaceSize));
    m_hostImpl->setRootLayer(root.Pass());
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();

    LayerImpl* child = m_hostImpl->rootLayer()->children()[0];

    IntSize scrollDelta(0, 10);
    IntSize expectedScrollDelta(scrollDelta);
    IntSize expectedMaxScroll(child->maxScrollPosition());
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), scrollDelta);
    m_hostImpl->scrollEnd();

    float pageScale = 2;
    m_hostImpl->setPageScaleFactorAndLimits(pageScale, 1, pageScale);

    m_hostImpl->updateRootScrollLayerImplTransform();

    if (!Settings::pageScalePinchZoomEnabled()) {
        // The scale should apply to the scroll delta.
        expectedScrollDelta.scale(pageScale);
    }
    scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
    expectContains(*scrollInfo.get(), scrollLayerId, expectedScrollDelta);

    // The scroll range should not have changed.
    EXPECT_EQ(child->maxScrollPosition(), expectedMaxScroll);

    // The page scale delta remains constant because the impl thread did not scale.
    WebTransformationMatrix identityTransform;
    EXPECT_EQ(child->implTransform(), WebTransformationMatrix());
}

TEST_P(LayerTreeHostImplTest, scrollChildBeyondLimit)
{
    // Scroll a child layer beyond its maximum scroll range and make sure the
    // parent layer is scrolled on the axis on which the child was unable to
    // scroll.
    IntSize surfaceSize(10, 10);
    scoped_ptr<LayerImpl> root = createScrollableLayer(1, surfaceSize);

    scoped_ptr<LayerImpl> grandChild = createScrollableLayer(3, surfaceSize);
    grandChild->setScrollPosition(IntPoint(0, 5));

    scoped_ptr<LayerImpl> child = createScrollableLayer(2, surfaceSize);
    child->setScrollPosition(IntPoint(3, 0));
    child->addChild(grandChild.Pass());

    root->addChild(child.Pass());
    m_hostImpl->setRootLayer(root.Pass());
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();
    {
        IntSize scrollDelta(-8, -7);
        EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
        m_hostImpl->scrollBy(IntPoint(), scrollDelta);
        m_hostImpl->scrollEnd();

        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();

        // The grand child should have scrolled up to its limit.
        LayerImpl* child = m_hostImpl->rootLayer()->children()[0];
        LayerImpl* grandChild = child->children()[0];
        expectContains(*scrollInfo.get(), grandChild->id(), IntSize(0, -5));

        // The child should have only scrolled on the other axis.
        expectContains(*scrollInfo.get(), child->id(), IntSize(-3, 0));
    }
}

TEST_P(LayerTreeHostImplTest, scrollEventBubbling)
{
    // When we try to scroll a non-scrollable child layer, the scroll delta
    // should be applied to one of its ancestors if possible.
    IntSize surfaceSize(10, 10);
    scoped_ptr<LayerImpl> root = createScrollableLayer(1, surfaceSize);
    scoped_ptr<LayerImpl> child = createScrollableLayer(2, surfaceSize);

    child->setScrollable(false);
    root->addChild(child.Pass());

    m_hostImpl->setRootLayer(root.Pass());
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();
    {
        IntSize scrollDelta(0, 4);
        EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
        m_hostImpl->scrollBy(IntPoint(), scrollDelta);
        m_hostImpl->scrollEnd();

        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();

        // Only the root should have scrolled.
        ASSERT_EQ(scrollInfo->scrolls.size(), 1u);
        expectContains(*scrollInfo.get(), m_hostImpl->rootLayer()->id(), scrollDelta);
    }
}

TEST_P(LayerTreeHostImplTest, scrollBeforeRedraw)
{
    IntSize surfaceSize(10, 10);
    m_hostImpl->setRootLayer(createScrollableLayer(1, surfaceSize));
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);

    // Draw one frame and then immediately rebuild the layer tree to mimic a tree synchronization.
    initializeRendererAndDrawFrame();
    m_hostImpl->detachLayerTree();
    m_hostImpl->setRootLayer(createScrollableLayer(2, surfaceSize));

    // Scrolling should still work even though we did not draw yet.
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(5, 5), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
}

TEST_P(LayerTreeHostImplTest, scrollAxisAlignedRotatedLayer)
{
    setupScrollAndContentsLayers(IntSize(100, 100));

    // Rotate the root layer 90 degrees counter-clockwise about its center.
    WebTransformationMatrix rotateTransform;
    rotateTransform.rotate(-90);
    m_hostImpl->rootLayer()->setTransform(rotateTransform);

    IntSize surfaceSize(50, 50);
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();

    // Scroll to the right in screen coordinates with a gesture.
    IntSize gestureScrollDelta(10, 0);
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Gesture), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), gestureScrollDelta);
    m_hostImpl->scrollEnd();

    // The layer should have scrolled down in its local coordinates.
    scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
    expectContains(*scrollInfo.get(), m_hostImpl->rootLayer()->id(), IntSize(0, gestureScrollDelta.width()));

    // Reset and scroll down with the wheel.
    m_hostImpl->rootLayer()->setScrollDelta(FloatSize());
    IntSize wheelScrollDelta(0, 10);
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), wheelScrollDelta);
    m_hostImpl->scrollEnd();

    // The layer should have scrolled down in its local coordinates.
    scrollInfo = m_hostImpl->processScrollDeltas();
    expectContains(*scrollInfo.get(), m_hostImpl->rootLayer()->id(), wheelScrollDelta);
}

TEST_P(LayerTreeHostImplTest, scrollNonAxisAlignedRotatedLayer)
{
    setupScrollAndContentsLayers(IntSize(100, 100));
    int childLayerId = 3;
    float childLayerAngle = -20;

    // Create a child layer that is rotated to a non-axis-aligned angle.
    scoped_ptr<LayerImpl> child = createScrollableLayer(childLayerId, m_hostImpl->rootLayer()->contentBounds());
    WebTransformationMatrix rotateTransform;
    rotateTransform.translate(-50, -50);
    rotateTransform.rotate(childLayerAngle);
    rotateTransform.translate(50, 50);
    child->setTransform(rotateTransform);

    // Only allow vertical scrolling.
    child->setMaxScrollPosition(IntSize(0, child->contentBounds().height()));
    m_hostImpl->rootLayer()->addChild(child.Pass());

    IntSize surfaceSize(50, 50);
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();

    {
        // Scroll down in screen coordinates with a gesture.
        IntSize gestureScrollDelta(0, 10);
        EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Gesture), InputHandlerClient::ScrollStarted);
        m_hostImpl->scrollBy(IntPoint(), gestureScrollDelta);
        m_hostImpl->scrollEnd();

        // The child layer should have scrolled down in its local coordinates an amount proportional to
        // the angle between it and the input scroll delta.
        IntSize expectedScrollDelta(0, gestureScrollDelta.height() * cosf(deg2rad(childLayerAngle)));
        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        expectContains(*scrollInfo.get(), childLayerId, expectedScrollDelta);

        // The root layer should not have scrolled, because the input delta was close to the layer's
        // axis of movement.
        EXPECT_EQ(scrollInfo->scrolls.size(), 1u);
    }

    {
        // Now reset and scroll the same amount horizontally.
        m_hostImpl->rootLayer()->children()[1]->setScrollDelta(FloatSize());
        IntSize gestureScrollDelta(10, 0);
        EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Gesture), InputHandlerClient::ScrollStarted);
        m_hostImpl->scrollBy(IntPoint(), gestureScrollDelta);
        m_hostImpl->scrollEnd();

        // The child layer should have scrolled down in its local coordinates an amount proportional to
        // the angle between it and the input scroll delta.
        IntSize expectedScrollDelta(0, -gestureScrollDelta.width() * sinf(deg2rad(childLayerAngle)));
        scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
        expectContains(*scrollInfo.get(), childLayerId, expectedScrollDelta);

        // The root layer should have scrolled more, since the input scroll delta was mostly
        // orthogonal to the child layer's vertical scroll axis.
        IntSize expectedRootScrollDelta(gestureScrollDelta.width() * pow(cosf(deg2rad(childLayerAngle)), 2), 0);
        expectContains(*scrollInfo.get(), m_hostImpl->rootLayer()->id(), expectedRootScrollDelta);
    }
}

TEST_P(LayerTreeHostImplTest, scrollScaledLayer)
{
    setupScrollAndContentsLayers(IntSize(100, 100));

    // Scale the layer to twice its normal size.
    int scale = 2;
    WebTransformationMatrix scaleTransform;
    scaleTransform.scale(scale);
    m_hostImpl->rootLayer()->setTransform(scaleTransform);

    IntSize surfaceSize(50, 50);
    m_hostImpl->setViewportSize(surfaceSize, surfaceSize);
    initializeRendererAndDrawFrame();

    // Scroll down in screen coordinates with a gesture.
    IntSize scrollDelta(0, 10);
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Gesture), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), scrollDelta);
    m_hostImpl->scrollEnd();

    // The layer should have scrolled down in its local coordinates, but half he amount.
    scoped_ptr<ScrollAndScaleSet> scrollInfo = m_hostImpl->processScrollDeltas();
    expectContains(*scrollInfo.get(), m_hostImpl->rootLayer()->id(), IntSize(0, scrollDelta.height() / scale));

    // Reset and scroll down with the wheel.
    m_hostImpl->rootLayer()->setScrollDelta(FloatSize());
    IntSize wheelScrollDelta(0, 10);
    EXPECT_EQ(m_hostImpl->scrollBegin(IntPoint(0, 0), InputHandlerClient::Wheel), InputHandlerClient::ScrollStarted);
    m_hostImpl->scrollBy(IntPoint(), wheelScrollDelta);
    m_hostImpl->scrollEnd();

    // The scale should not have been applied to the scroll delta.
    scrollInfo = m_hostImpl->processScrollDeltas();
    expectContains(*scrollInfo.get(), m_hostImpl->rootLayer()->id(), wheelScrollDelta);
}

class BlendStateTrackerContext: public FakeWebGraphicsContext3D {
public:
    BlendStateTrackerContext() : m_blend(false) { }

    virtual void enable(WGC3Denum cap)
    {
        if (cap == GL_BLEND)
            m_blend = true;
    }

    virtual void disable(WGC3Denum cap)
    {
        if (cap == GL_BLEND)
            m_blend = false;
    }

    bool blend() const { return m_blend; }

private:
    bool m_blend;
};

class BlendStateCheckLayer : public LayerImpl {
public:
    static scoped_ptr<LayerImpl> create(int id, ResourceProvider* resourceProvider) { return scoped_ptr<LayerImpl>(new BlendStateCheckLayer(id, resourceProvider)); }

    virtual void appendQuads(QuadSink& quadSink, AppendQuadsData& appendQuadsData) OVERRIDE
    {
        m_quadsAppended = true;

        IntRect opaqueRect;
        if (contentsOpaque())
            opaqueRect = m_quadRect;
        else
            opaqueRect = m_opaqueContentRect;

        SharedQuadState* sharedQuadState = quadSink.useSharedQuadState(createSharedQuadState());
        scoped_ptr<TileDrawQuad> testBlendingDrawQuad = TileDrawQuad::create(sharedQuadState, m_quadRect, opaqueRect, m_resourceId, IntPoint(), IntSize(1, 1), 0, false, false, false, false, false);
        testBlendingDrawQuad->setQuadVisibleRect(m_quadVisibleRect);
        EXPECT_EQ(m_blend, testBlendingDrawQuad->needsBlending());
        EXPECT_EQ(m_hasRenderSurface, !!renderSurface());
        quadSink.append(testBlendingDrawQuad.PassAs<DrawQuad>(), appendQuadsData);
    }

    void setExpectation(bool blend, bool hasRenderSurface)
    {
        m_blend = blend;
        m_hasRenderSurface = hasRenderSurface;
        m_quadsAppended = false;
    }

    bool quadsAppended() const { return m_quadsAppended; }

    void setQuadRect(const IntRect& rect) { m_quadRect = rect; }
    void setQuadVisibleRect(const IntRect& rect) { m_quadVisibleRect = rect; }
    void setOpaqueContentRect(const IntRect& rect) { m_opaqueContentRect = rect; }

private:
    explicit BlendStateCheckLayer(int id, ResourceProvider* resourceProvider)
        : LayerImpl(id)
        , m_blend(false)
        , m_hasRenderSurface(false)
        , m_quadsAppended(false)
        , m_quadRect(5, 5, 5, 5)
        , m_quadVisibleRect(5, 5, 5, 5)
        , m_resourceId(resourceProvider->createResource(Renderer::ContentPool, IntSize(1, 1), GL_RGBA, ResourceProvider::TextureUsageAny))
    {
        setAnchorPoint(FloatPoint(0, 0));
        setBounds(IntSize(10, 10));
        setContentBounds(IntSize(10, 10));
        setDrawsContent(true);
    }

    bool m_blend;
    bool m_hasRenderSurface;
    bool m_quadsAppended;
    IntRect m_quadRect;
    IntRect m_opaqueContentRect;
    IntRect m_quadVisibleRect;
    ResourceProvider::ResourceId m_resourceId;
};

TEST_P(LayerTreeHostImplTest, blendingOffWhenDrawingOpaqueLayers)
{
    {
        scoped_ptr<LayerImpl> root = LayerImpl::create(1);
        root->setAnchorPoint(FloatPoint(0, 0));
        root->setBounds(IntSize(10, 10));
        root->setContentBounds(root->bounds());
        root->setDrawsContent(false);
        m_hostImpl->setRootLayer(root.Pass());
    }
    LayerImpl* root = m_hostImpl->rootLayer();

    root->addChild(BlendStateCheckLayer::create(2, m_hostImpl->resourceProvider()));
    BlendStateCheckLayer* layer1 = static_cast<BlendStateCheckLayer*>(root->children()[0]);
    layer1->setPosition(FloatPoint(2, 2));

    LayerTreeHostImpl::FrameData frame;

    // Opaque layer, drawn without blending.
    layer1->setContentsOpaque(true);
    layer1->setExpectation(false, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Layer with translucent content and painting, so drawn with blending.
    layer1->setContentsOpaque(false);
    layer1->setExpectation(true, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Layer with translucent opacity, drawn with blending.
    layer1->setContentsOpaque(true);
    layer1->setOpacity(0.5);
    layer1->setExpectation(true, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Layer with translucent opacity and painting, drawn with blending.
    layer1->setContentsOpaque(true);
    layer1->setOpacity(0.5);
    layer1->setExpectation(true, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    layer1->addChild(BlendStateCheckLayer::create(3, m_hostImpl->resourceProvider()));
    BlendStateCheckLayer* layer2 = static_cast<BlendStateCheckLayer*>(layer1->children()[0]);
    layer2->setPosition(FloatPoint(4, 4));

    // 2 opaque layers, drawn without blending.
    layer1->setContentsOpaque(true);
    layer1->setOpacity(1);
    layer1->setExpectation(false, false);
    layer2->setContentsOpaque(true);
    layer2->setOpacity(1);
    layer2->setExpectation(false, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    EXPECT_TRUE(layer2->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Parent layer with translucent content, drawn with blending.
    // Child layer with opaque content, drawn without blending.
    layer1->setContentsOpaque(false);
    layer1->setExpectation(true, false);
    layer2->setExpectation(false, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    EXPECT_TRUE(layer2->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Parent layer with translucent content but opaque painting, drawn without blending.
    // Child layer with opaque content, drawn without blending.
    layer1->setContentsOpaque(true);
    layer1->setExpectation(false, false);
    layer2->setExpectation(false, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    EXPECT_TRUE(layer2->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Parent layer with translucent opacity and opaque content. Since it has a
    // drawing child, it's drawn to a render surface which carries the opacity,
    // so it's itself drawn without blending.
    // Child layer with opaque content, drawn without blending (parent surface
    // carries the inherited opacity).
    layer1->setContentsOpaque(true);
    layer1->setOpacity(0.5);
    layer1->setExpectation(false, true);
    layer2->setExpectation(false, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    EXPECT_TRUE(layer2->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Draw again, but with child non-opaque, to make sure
    // layer1 not culled.
    layer1->setContentsOpaque(true);
    layer1->setOpacity(1);
    layer1->setExpectation(false, false);
    layer2->setContentsOpaque(true);
    layer2->setOpacity(0.5);
    layer2->setExpectation(true, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    EXPECT_TRUE(layer2->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // A second way of making the child non-opaque.
    layer1->setContentsOpaque(true);
    layer1->setOpacity(1);
    layer1->setExpectation(false, false);
    layer2->setContentsOpaque(false);
    layer2->setOpacity(1);
    layer2->setExpectation(true, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    EXPECT_TRUE(layer2->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // And when the layer says its not opaque but is painted opaque, it is not blended.
    layer1->setContentsOpaque(true);
    layer1->setOpacity(1);
    layer1->setExpectation(false, false);
    layer2->setContentsOpaque(true);
    layer2->setOpacity(1);
    layer2->setExpectation(false, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    EXPECT_TRUE(layer2->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Layer with partially opaque contents, drawn with blending.
    layer1->setContentsOpaque(false);
    layer1->setQuadRect(IntRect(5, 5, 5, 5));
    layer1->setQuadVisibleRect(IntRect(5, 5, 5, 5));
    layer1->setOpaqueContentRect(IntRect(5, 5, 2, 5));
    layer1->setExpectation(true, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Layer with partially opaque contents partially culled, drawn with blending.
    layer1->setContentsOpaque(false);
    layer1->setQuadRect(IntRect(5, 5, 5, 5));
    layer1->setQuadVisibleRect(IntRect(5, 5, 5, 2));
    layer1->setOpaqueContentRect(IntRect(5, 5, 2, 5));
    layer1->setExpectation(true, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Layer with partially opaque contents culled, drawn with blending.
    layer1->setContentsOpaque(false);
    layer1->setQuadRect(IntRect(5, 5, 5, 5));
    layer1->setQuadVisibleRect(IntRect(7, 5, 3, 5));
    layer1->setOpaqueContentRect(IntRect(5, 5, 2, 5));
    layer1->setExpectation(true, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

    // Layer with partially opaque contents and translucent contents culled, drawn without blending.
    layer1->setContentsOpaque(false);
    layer1->setQuadRect(IntRect(5, 5, 5, 5));
    layer1->setQuadVisibleRect(IntRect(5, 5, 2, 5));
    layer1->setOpaqueContentRect(IntRect(5, 5, 2, 5));
    layer1->setExpectation(false, false);
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(layer1->quadsAppended());
    m_hostImpl->didDrawAllLayers(frame);

}

TEST_P(LayerTreeHostImplTest, viewportCovered)
{
    m_hostImpl->initializeRenderer(createContext());
    m_hostImpl->setBackgroundColor(SK_ColorGRAY);

    IntSize viewportSize(1000, 1000);
    m_hostImpl->setViewportSize(viewportSize, viewportSize);

    m_hostImpl->setRootLayer(BlendStateCheckLayer::create(1, m_hostImpl->resourceProvider()));
    BlendStateCheckLayer* root = static_cast<BlendStateCheckLayer*>(m_hostImpl->rootLayer());
    root->setExpectation(false, true);
    root->setContentsOpaque(true);

    // No gutter rects
    {
        IntRect layerRect(0, 0, 1000, 1000);
        root->setPosition(layerRect.location());
        root->setBounds(layerRect.size());
        root->setContentBounds(layerRect.size());
        root->setQuadRect(IntRect(IntPoint(), layerRect.size()));
        root->setQuadVisibleRect(IntRect(IntPoint(), layerRect.size()));

        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
        ASSERT_EQ(1u, frame.renderPasses.size());

        size_t numGutterQuads = 0;
        for (size_t i = 0; i < frame.renderPasses[0]->quadList().size(); ++i)
            numGutterQuads += (frame.renderPasses[0]->quadList()[i]->material() == DrawQuad::SolidColor) ? 1 : 0;
        EXPECT_EQ(0u, numGutterQuads);
        EXPECT_EQ(1u, frame.renderPasses[0]->quadList().size());

        verifyQuadsExactlyCoverRect(frame.renderPasses[0]->quadList(), IntRect(-layerRect.location(), viewportSize));
        m_hostImpl->didDrawAllLayers(frame);
    }

    // Empty visible content area (fullscreen gutter rect)
    {
        IntRect layerRect(0, 0, 0, 0);
        root->setPosition(layerRect.location());
        root->setBounds(layerRect.size());
        root->setContentBounds(layerRect.size());
        root->setQuadRect(IntRect(IntPoint(), layerRect.size()));
        root->setQuadVisibleRect(IntRect(IntPoint(), layerRect.size()));

        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
        ASSERT_EQ(1u, frame.renderPasses.size());
        m_hostImpl->didDrawAllLayers(frame);

        size_t numGutterQuads = 0;
        for (size_t i = 0; i < frame.renderPasses[0]->quadList().size(); ++i)
            numGutterQuads += (frame.renderPasses[0]->quadList()[i]->material() == DrawQuad::SolidColor) ? 1 : 0;
        EXPECT_EQ(1u, numGutterQuads);
        EXPECT_EQ(1u, frame.renderPasses[0]->quadList().size());

        verifyQuadsExactlyCoverRect(frame.renderPasses[0]->quadList(), IntRect(-layerRect.location(), viewportSize));
        m_hostImpl->didDrawAllLayers(frame);
    }

    // Content area in middle of clip rect (four surrounding gutter rects)
    {
        IntRect layerRect(500, 500, 200, 200);
        root->setPosition(layerRect.location());
        root->setBounds(layerRect.size());
        root->setContentBounds(layerRect.size());
        root->setQuadRect(IntRect(IntPoint(), layerRect.size()));
        root->setQuadVisibleRect(IntRect(IntPoint(), layerRect.size()));

        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
        ASSERT_EQ(1u, frame.renderPasses.size());

        size_t numGutterQuads = 0;
        for (size_t i = 0; i < frame.renderPasses[0]->quadList().size(); ++i)
            numGutterQuads += (frame.renderPasses[0]->quadList()[i]->material() == DrawQuad::SolidColor) ? 1 : 0;
        EXPECT_EQ(4u, numGutterQuads);
        EXPECT_EQ(5u, frame.renderPasses[0]->quadList().size());

        verifyQuadsExactlyCoverRect(frame.renderPasses[0]->quadList(), IntRect(-layerRect.location(), viewportSize));
        m_hostImpl->didDrawAllLayers(frame);
    }

}


class ReshapeTrackerContext: public FakeWebGraphicsContext3D {
public:
    ReshapeTrackerContext() : m_reshapeCalled(false) { }

    virtual void reshape(int width, int height)
    {
        m_reshapeCalled = true;
    }

    bool reshapeCalled() const { return m_reshapeCalled; }

private:
    bool m_reshapeCalled;
};

class FakeDrawableLayerImpl: public LayerImpl {
public:
    static scoped_ptr<LayerImpl> create(int id) { return scoped_ptr<LayerImpl>(new FakeDrawableLayerImpl(id)); }
protected:
    explicit FakeDrawableLayerImpl(int id) : LayerImpl(id) { }
};

// Only reshape when we know we are going to draw. Otherwise, the reshape
// can leave the window at the wrong size if we never draw and the proper
// viewport size is never set.
TEST_P(LayerTreeHostImplTest, reshapeNotCalledUntilDraw)
{
    scoped_ptr<GraphicsContext> outputSurface = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new ReshapeTrackerContext)).PassAs<GraphicsContext>();
    ReshapeTrackerContext* reshapeTracker = static_cast<ReshapeTrackerContext*>(outputSurface->context3D());
    m_hostImpl->initializeRenderer(outputSurface.Pass());

    scoped_ptr<LayerImpl> root = FakeDrawableLayerImpl::create(1);
    root->setAnchorPoint(FloatPoint(0, 0));
    root->setBounds(IntSize(10, 10));
    root->setDrawsContent(true);
    m_hostImpl->setRootLayer(root.Pass());
    EXPECT_FALSE(reshapeTracker->reshapeCalled());

    LayerTreeHostImpl::FrameData frame;
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    EXPECT_TRUE(reshapeTracker->reshapeCalled());
    m_hostImpl->didDrawAllLayers(frame);
}

class PartialSwapTrackerContext : public FakeWebGraphicsContext3D {
public:
    virtual void postSubBufferCHROMIUM(int x, int y, int width, int height)
    {
        m_partialSwapRect = IntRect(x, y, width, height);
    }

    virtual WebString getString(WGC3Denum name)
    {
        if (name == GL_EXTENSIONS)
            return WebString("GL_CHROMIUM_post_sub_buffer GL_CHROMIUM_set_visibility");

        return WebString();
    }

    IntRect partialSwapRect() const { return m_partialSwapRect; }

private:
    IntRect m_partialSwapRect;
};

// Make sure damage tracking propagates all the way to the graphics context,
// where it should request to swap only the subBuffer that is damaged.
TEST_P(LayerTreeHostImplTest, partialSwapReceivesDamageRect)
{
    scoped_ptr<GraphicsContext> outputSurface = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new PartialSwapTrackerContext)).PassAs<GraphicsContext>();
    PartialSwapTrackerContext* partialSwapTracker = static_cast<PartialSwapTrackerContext*>(outputSurface->context3D());

    // This test creates its own LayerTreeHostImpl, so
    // that we can force partial swap enabled.
    LayerTreeSettings settings;
    Settings::setPartialSwapEnabled(true);
    scoped_ptr<LayerTreeHostImpl> layerTreeHostImpl = LayerTreeHostImpl::create(settings, this);
    layerTreeHostImpl->initializeRenderer(outputSurface.Pass());
    layerTreeHostImpl->setViewportSize(IntSize(500, 500), IntSize(500, 500));

    scoped_ptr<LayerImpl> root = FakeDrawableLayerImpl::create(1);
    scoped_ptr<LayerImpl> child = FakeDrawableLayerImpl::create(2);
    child->setPosition(FloatPoint(12, 13));
    child->setAnchorPoint(FloatPoint(0, 0));
    child->setBounds(IntSize(14, 15));
    child->setContentBounds(IntSize(14, 15));
    child->setDrawsContent(true);
    root->setAnchorPoint(FloatPoint(0, 0));
    root->setBounds(IntSize(500, 500));
    root->setContentBounds(IntSize(500, 500));
    root->setDrawsContent(true);
    root->addChild(child.Pass());
    layerTreeHostImpl->setRootLayer(root.Pass());

    LayerTreeHostImpl::FrameData frame;

    // First frame, the entire screen should get swapped.
    EXPECT_TRUE(layerTreeHostImpl->prepareToDraw(frame));
    layerTreeHostImpl->drawLayers(frame);
    layerTreeHostImpl->didDrawAllLayers(frame);
    layerTreeHostImpl->swapBuffers();
    IntRect actualSwapRect = partialSwapTracker->partialSwapRect();
    IntRect expectedSwapRect = IntRect(IntPoint::zero(), IntSize(500, 500));
    EXPECT_EQ(expectedSwapRect.x(), actualSwapRect.x());
    EXPECT_EQ(expectedSwapRect.y(), actualSwapRect.y());
    EXPECT_EQ(expectedSwapRect.width(), actualSwapRect.width());
    EXPECT_EQ(expectedSwapRect.height(), actualSwapRect.height());

    // Second frame, only the damaged area should get swapped. Damage should be the union
    // of old and new child rects.
    // expected damage rect: IntRect(IntPoint::zero(), IntSize(26, 28));
    // expected swap rect: vertically flipped, with origin at bottom left corner.
    layerTreeHostImpl->rootLayer()->children()[0]->setPosition(FloatPoint(0, 0));
    EXPECT_TRUE(layerTreeHostImpl->prepareToDraw(frame));
    layerTreeHostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);
    layerTreeHostImpl->swapBuffers();
    actualSwapRect = partialSwapTracker->partialSwapRect();
    expectedSwapRect = IntRect(IntPoint(0, 500-28), IntSize(26, 28));
    EXPECT_EQ(expectedSwapRect.x(), actualSwapRect.x());
    EXPECT_EQ(expectedSwapRect.y(), actualSwapRect.y());
    EXPECT_EQ(expectedSwapRect.width(), actualSwapRect.width());
    EXPECT_EQ(expectedSwapRect.height(), actualSwapRect.height());

    // Make sure that partial swap is constrained to the viewport dimensions
    // expected damage rect: IntRect(IntPoint::zero(), IntSize(500, 500));
    // expected swap rect: flipped damage rect, but also clamped to viewport
    layerTreeHostImpl->setViewportSize(IntSize(10, 10), IntSize(10, 10));
    layerTreeHostImpl->rootLayer()->setOpacity(0.7f); // this will damage everything
    EXPECT_TRUE(layerTreeHostImpl->prepareToDraw(frame));
    layerTreeHostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);
    layerTreeHostImpl->swapBuffers();
    actualSwapRect = partialSwapTracker->partialSwapRect();
    expectedSwapRect = IntRect(IntPoint::zero(), IntSize(10, 10));
    EXPECT_EQ(expectedSwapRect.x(), actualSwapRect.x());
    EXPECT_EQ(expectedSwapRect.y(), actualSwapRect.y());
    EXPECT_EQ(expectedSwapRect.width(), actualSwapRect.width());
    EXPECT_EQ(expectedSwapRect.height(), actualSwapRect.height());
}

TEST_P(LayerTreeHostImplTest, rootLayerDoesntCreateExtraSurface)
{
    scoped_ptr<LayerImpl> root = FakeDrawableLayerImpl::create(1);
    scoped_ptr<LayerImpl> child = FakeDrawableLayerImpl::create(2);
    child->setAnchorPoint(FloatPoint(0, 0));
    child->setBounds(IntSize(10, 10));
    child->setContentBounds(IntSize(10, 10));
    child->setDrawsContent(true);
    root->setAnchorPoint(FloatPoint(0, 0));
    root->setBounds(IntSize(10, 10));
    root->setContentBounds(IntSize(10, 10));
    root->setDrawsContent(true);
    root->setOpacity(0.7f);
    root->addChild(child.Pass());

    m_hostImpl->setRootLayer(root.Pass());

    LayerTreeHostImpl::FrameData frame;

    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    EXPECT_EQ(1u, frame.renderSurfaceLayerList->size());
    EXPECT_EQ(1u, frame.renderPasses.size());
    m_hostImpl->didDrawAllLayers(frame);
}

} // namespace

class FakeLayerWithQuads : public LayerImpl {
public:
    static scoped_ptr<LayerImpl> create(int id) { return scoped_ptr<LayerImpl>(new FakeLayerWithQuads(id)); }

    virtual void appendQuads(QuadSink& quadSink, AppendQuadsData& appendQuadsData) OVERRIDE
    {
        SharedQuadState* sharedQuadState = quadSink.useSharedQuadState(createSharedQuadState());

        SkColor gray = SkColorSetRGB(100, 100, 100);
        IntRect quadRect(IntPoint(0, 0), contentBounds());
        scoped_ptr<SolidColorDrawQuad> myQuad = SolidColorDrawQuad::create(sharedQuadState, quadRect, gray);
        quadSink.append(myQuad.PassAs<DrawQuad>(), appendQuadsData);
    }

private:
    FakeLayerWithQuads(int id)
        : LayerImpl(id)
    {
    }
};

namespace {

class MockContext : public FakeWebGraphicsContext3D {
public:
    MOCK_METHOD1(useProgram, void(WebGLId program));
    MOCK_METHOD5(uniform4f, void(WGC3Dint location, WGC3Dfloat x, WGC3Dfloat y, WGC3Dfloat z, WGC3Dfloat w));
    MOCK_METHOD4(uniformMatrix4fv, void(WGC3Dint location, WGC3Dsizei count, WGC3Dboolean transpose, const WGC3Dfloat* value));
    MOCK_METHOD4(drawElements, void(WGC3Denum mode, WGC3Dsizei count, WGC3Denum type, WGC3Dintptr offset));
    MOCK_METHOD1(getString, WebString(WGC3Denum name));
    MOCK_METHOD0(getRequestableExtensionsCHROMIUM, WebString());
    MOCK_METHOD1(enable, void(WGC3Denum cap));
    MOCK_METHOD1(disable, void(WGC3Denum cap));
    MOCK_METHOD4(scissor, void(WGC3Dint x, WGC3Dint y, WGC3Dsizei width, WGC3Dsizei height));
};

class MockContextHarness {
private:
    MockContext* m_context;
public:
    MockContextHarness(MockContext* context)
        : m_context(context)
    {
        // Catch "uninteresting" calls
        EXPECT_CALL(*m_context, useProgram(_))
            .Times(0);

        EXPECT_CALL(*m_context, drawElements(_, _, _, _))
            .Times(0);

        // These are not asserted
        EXPECT_CALL(*m_context, uniformMatrix4fv(_, _, _, _))
            .WillRepeatedly(Return());

        EXPECT_CALL(*m_context, uniform4f(_, _, _, _, _))
            .WillRepeatedly(Return());

        // Any other strings are empty
        EXPECT_CALL(*m_context, getString(_))
            .WillRepeatedly(Return(WebString()));

        // Support for partial swap, if needed
        EXPECT_CALL(*m_context, getString(GL_EXTENSIONS))
            .WillRepeatedly(Return(WebString("GL_CHROMIUM_post_sub_buffer")));

        EXPECT_CALL(*m_context, getRequestableExtensionsCHROMIUM())
            .WillRepeatedly(Return(WebString("GL_CHROMIUM_post_sub_buffer")));

        // Any un-sanctioned calls to enable() are OK
        EXPECT_CALL(*m_context, enable(_))
            .WillRepeatedly(Return());

        // Any un-sanctioned calls to disable() are OK
        EXPECT_CALL(*m_context, disable(_))
            .WillRepeatedly(Return());
    }

    void mustDrawSolidQuad()
    {
        EXPECT_CALL(*m_context, drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0))
            .WillOnce(Return())
            .RetiresOnSaturation();

        // 1 is hardcoded return value of fake createProgram()
        EXPECT_CALL(*m_context, useProgram(1))
            .WillOnce(Return())
            .RetiresOnSaturation();

    }

    void mustSetScissor(int x, int y, int width, int height)
    {
        EXPECT_CALL(*m_context, enable(GL_SCISSOR_TEST))
            .WillRepeatedly(Return());

        EXPECT_CALL(*m_context, scissor(x, y, width, height))
            .Times(AtLeast(1))
            .WillRepeatedly(Return());
    }

    void mustSetNoScissor()
    {
        EXPECT_CALL(*m_context, disable(GL_SCISSOR_TEST))
            .WillRepeatedly(Return());

        EXPECT_CALL(*m_context, enable(GL_SCISSOR_TEST))
            .Times(0);

        EXPECT_CALL(*m_context, scissor(_, _, _, _))
            .Times(0);
    }
};

TEST_P(LayerTreeHostImplTest, noPartialSwap)
{
    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebGraphicsContext3D>(new MockContext)).PassAs<GraphicsContext>();
    MockContext* mockContext = static_cast<MockContext*>(context->context3D());
    MockContextHarness harness(mockContext);

    harness.mustDrawSolidQuad();
    harness.mustSetScissor(0, 0, 10, 10);

    // Run test case
    scoped_ptr<LayerTreeHostImpl> myHostImpl = createLayerTreeHost(false, context.Pass(), FakeLayerWithQuads::create(1));

    LayerTreeHostImpl::FrameData frame;
    EXPECT_TRUE(myHostImpl->prepareToDraw(frame));
    myHostImpl->drawLayers(frame);
    myHostImpl->didDrawAllLayers(frame);
    Mock::VerifyAndClearExpectations(&mockContext);
}

TEST_P(LayerTreeHostImplTest, partialSwap)
{
    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new MockContext)).PassAs<GraphicsContext>();
    MockContext* mockContext = static_cast<MockContext*>(context->context3D());
    MockContextHarness harness(mockContext);

    scoped_ptr<LayerTreeHostImpl> myHostImpl = createLayerTreeHost(true, context.Pass(), FakeLayerWithQuads::create(1));

    // The first frame is not a partially-swapped one.
    harness.mustSetScissor(0, 0, 10, 10);
    harness.mustDrawSolidQuad();
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));
        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }
    Mock::VerifyAndClearExpectations(&mockContext);

    // Damage a portion of the frame.
    myHostImpl->rootLayer()->setUpdateRect(IntRect(0, 0, 2, 3));

    // The second frame will be partially-swapped (the y coordinates are flipped).
    harness.mustSetScissor(0, 7, 2, 3);
    harness.mustDrawSolidQuad();
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));
        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }
    Mock::VerifyAndClearExpectations(&mockContext);
}

class PartialSwapContext : public FakeWebGraphicsContext3D {
public:
    WebString getString(WGC3Denum name)
    {
        if (name == GL_EXTENSIONS)
            return WebString("GL_CHROMIUM_post_sub_buffer");
        return WebString();
    }

    WebString getRequestableExtensionsCHROMIUM()
    {
        return WebString("GL_CHROMIUM_post_sub_buffer");
    }

    // Unlimited texture size.
    virtual void getIntegerv(WGC3Denum pname, WGC3Dint* value)
    {
        if (pname == GL_MAX_TEXTURE_SIZE)
            *value = 8192;
    }
};

static scoped_ptr<LayerTreeHostImpl> setupLayersForOpacity(bool partialSwap, LayerTreeHostImplClient* client)
{
    Settings::setPartialSwapEnabled(partialSwap);

    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new PartialSwapContext)).PassAs<GraphicsContext>();

    LayerTreeSettings settings;
    scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, client);
    myHostImpl->initializeRenderer(context.Pass());
    myHostImpl->setViewportSize(IntSize(100, 100), IntSize(100, 100));

    /*
      Layers are created as follows:

         +--------------------+
         |                  1 |
         |  +-----------+     |
         |  |         2 |     |
         |  | +-------------------+
         |  | |   3               |
         |  | +-------------------+
         |  |           |     |
         |  +-----------+     |
         |                    |
         |                    |
         +--------------------+

         Layers 1, 2 have render surfaces
     */
    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    scoped_ptr<LayerImpl> child = LayerImpl::create(2);
    scoped_ptr<LayerImpl> grandChild = FakeLayerWithQuads::create(3);

    IntRect rootRect(0, 0, 100, 100);
    IntRect childRect(10, 10, 50, 50);
    IntRect grandChildRect(5, 5, 150, 150);

    root->createRenderSurface();
    root->setAnchorPoint(FloatPoint(0, 0));
    root->setPosition(FloatPoint(rootRect.x(), rootRect.y()));
    root->setBounds(IntSize(rootRect.width(), rootRect.height()));
    root->setContentBounds(root->bounds());
    root->setVisibleContentRect(rootRect);
    root->setDrawsContent(false);
    root->renderSurface()->setContentRect(IntRect(IntPoint(), IntSize(rootRect.width(), rootRect.height())));

    child->setAnchorPoint(FloatPoint(0, 0));
    child->setPosition(FloatPoint(childRect.x(), childRect.y()));
    child->setOpacity(0.5f);
    child->setBounds(IntSize(childRect.width(), childRect.height()));
    child->setContentBounds(child->bounds());
    child->setVisibleContentRect(childRect);
    child->setDrawsContent(false);

    grandChild->setAnchorPoint(FloatPoint(0, 0));
    grandChild->setPosition(IntPoint(grandChildRect.x(), grandChildRect.y()));
    grandChild->setBounds(IntSize(grandChildRect.width(), grandChildRect.height()));
    grandChild->setContentBounds(grandChild->bounds());
    grandChild->setVisibleContentRect(grandChildRect);
    grandChild->setDrawsContent(true);

    child->addChild(grandChild.Pass());
    root->addChild(child.Pass());

    myHostImpl->setRootLayer(root.Pass());
    return myHostImpl.Pass();
}

TEST_P(LayerTreeHostImplTest, contributingLayerEmptyScissorPartialSwap)
{
    scoped_ptr<LayerTreeHostImpl> myHostImpl = setupLayersForOpacity(true, this);

    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Just for consistency, the most interesting stuff already happened
        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);

        // Verify all quads have been computed
        ASSERT_EQ(2U, frame.renderPasses.size());
        ASSERT_EQ(1U, frame.renderPasses[0]->quadList().size());
        ASSERT_EQ(1U, frame.renderPasses[1]->quadList().size());
        EXPECT_EQ(DrawQuad::SolidColor, frame.renderPasses[0]->quadList()[0]->material());
        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[1]->quadList()[0]->material());
    }
}

TEST_P(LayerTreeHostImplTest, contributingLayerEmptyScissorNoPartialSwap)
{
    scoped_ptr<LayerTreeHostImpl> myHostImpl = setupLayersForOpacity(false, this);

    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Just for consistency, the most interesting stuff already happened
        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);

        // Verify all quads have been computed
        ASSERT_EQ(2U, frame.renderPasses.size());
        ASSERT_EQ(1U, frame.renderPasses[0]->quadList().size());
        ASSERT_EQ(1U, frame.renderPasses[1]->quadList().size());
        EXPECT_EQ(DrawQuad::SolidColor, frame.renderPasses[0]->quadList()[0]->material());
        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[1]->quadList()[0]->material());
    }
}

// Make sure that context lost notifications are propagated through the tree.
class ContextLostNotificationCheckLayer : public LayerImpl {
public:
    static scoped_ptr<LayerImpl> create(int id) { return scoped_ptr<LayerImpl>(new ContextLostNotificationCheckLayer(id)); }

    virtual void didLoseContext() OVERRIDE
    {
        m_didLoseContextCalled = true;
    }

    bool didLoseContextCalled() const { return m_didLoseContextCalled; }

private:
    explicit ContextLostNotificationCheckLayer(int id)
        : LayerImpl(id)
        , m_didLoseContextCalled(false)
    {
    }

    bool m_didLoseContextCalled;
};

TEST_P(LayerTreeHostImplTest, contextLostAndRestoredNotificationSentToAllLayers)
{
    m_hostImpl->setRootLayer(ContextLostNotificationCheckLayer::create(1));
    ContextLostNotificationCheckLayer* root = static_cast<ContextLostNotificationCheckLayer*>(m_hostImpl->rootLayer());

    root->addChild(ContextLostNotificationCheckLayer::create(1));
    ContextLostNotificationCheckLayer* layer1 = static_cast<ContextLostNotificationCheckLayer*>(root->children()[0]);

    layer1->addChild(ContextLostNotificationCheckLayer::create(2));
    ContextLostNotificationCheckLayer* layer2 = static_cast<ContextLostNotificationCheckLayer*>(layer1->children()[0]);

    EXPECT_FALSE(root->didLoseContextCalled());
    EXPECT_FALSE(layer1->didLoseContextCalled());
    EXPECT_FALSE(layer2->didLoseContextCalled());

    m_hostImpl->initializeRenderer(createContext());

    EXPECT_TRUE(root->didLoseContextCalled());
    EXPECT_TRUE(layer1->didLoseContextCalled());
    EXPECT_TRUE(layer2->didLoseContextCalled());
}

TEST_P(LayerTreeHostImplTest, finishAllRenderingAfterContextLost)
{
    LayerTreeSettings settings;
    m_hostImpl = LayerTreeHostImpl::create(settings, this);

    // The context initialization will fail, but we should still be able to call finishAllRendering() without any ill effects.
    m_hostImpl->initializeRenderer(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new FakeWebGraphicsContext3DMakeCurrentFails)).PassAs<GraphicsContext>());
    m_hostImpl->finishAllRendering();
}

class FakeWebGraphicsContext3DMakeCurrentFailsEventually : public FakeWebGraphicsContext3D {
public:
    explicit FakeWebGraphicsContext3DMakeCurrentFailsEventually(unsigned succeedCount) : m_succeedCount(succeedCount) { }
    virtual bool makeContextCurrent() {
        if (!m_succeedCount)
            return false;
        --m_succeedCount;
        return true;
    }

private:
    unsigned m_succeedCount;
};

TEST_P(LayerTreeHostImplTest, contextLostDuringInitialize)
{
    LayerTreeSettings settings;
    m_hostImpl = LayerTreeHostImpl::create(settings, this);

    // Initialize into a known successful state.
    EXPECT_TRUE(m_hostImpl->initializeRenderer(createContext()));
    EXPECT_TRUE(m_hostImpl->context());
    EXPECT_TRUE(m_hostImpl->renderer());
    EXPECT_TRUE(m_hostImpl->resourceProvider());

    // We will make the context get lost after a numer of makeContextCurrent
    // calls. The exact number of calls to make it succeed is dependent on the
    // implementation and doesn't really matter (i.e. can be changed to make the
    // tests pass after some refactoring).
    const unsigned kMakeCurrentSuccessesNeededForSuccessfulInitialization = 3;

    for (unsigned i = 0; i < kMakeCurrentSuccessesNeededForSuccessfulInitialization; ++i) {
        // The context will get lost during initialization, we shouldn't crash. We
        // should also be in a consistent state.
        EXPECT_FALSE(m_hostImpl->initializeRenderer(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new FakeWebGraphicsContext3DMakeCurrentFailsEventually(i))).PassAs<GraphicsContext>()));
        EXPECT_EQ(0, m_hostImpl->context());
        EXPECT_EQ(0, m_hostImpl->renderer());
        EXPECT_EQ(0, m_hostImpl->resourceProvider());
        EXPECT_TRUE(m_hostImpl->initializeRenderer(createContext()));
    }

    EXPECT_TRUE(m_hostImpl->initializeRenderer(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new FakeWebGraphicsContext3DMakeCurrentFailsEventually(kMakeCurrentSuccessesNeededForSuccessfulInitialization))).PassAs<GraphicsContext>()));
    EXPECT_TRUE(m_hostImpl->context());
    EXPECT_TRUE(m_hostImpl->renderer());
    EXPECT_TRUE(m_hostImpl->resourceProvider());
}

// Fake WebGraphicsContext3D that will cause a failure if trying to use a
// resource that wasn't created by it (resources created by
// FakeWebGraphicsContext3D have an id of 1).
class StrictWebGraphicsContext3D : public FakeWebGraphicsContext3D {
public:
    StrictWebGraphicsContext3D()
        : FakeWebGraphicsContext3D()
    {
        m_nextTextureId = 7; // Start allocating texture ids larger than any other resource IDs so we can tell if someone's mixing up their resource types.
    }

    virtual WebGLId createBuffer() { return 2; }
    virtual WebGLId createFramebuffer() { return 3; }
    virtual WebGLId createProgram() { return 4; }
    virtual WebGLId createRenderbuffer() { return 5; }
    virtual WebGLId createShader(WGC3Denum) { return 6; }

    virtual void deleteBuffer(WebGLId id)
    {
        if (id != 2)
            ADD_FAILURE() << "Trying to delete buffer id " << id;
    }

    virtual void deleteFramebuffer(WebGLId id)
    {
        if (id != 3)
            ADD_FAILURE() << "Trying to delete framebuffer id " << id;
    }

    virtual void deleteProgram(WebGLId id)
    {
        if (id != 4)
            ADD_FAILURE() << "Trying to delete program id " << id;
    }

    virtual void deleteRenderbuffer(WebGLId id)
    {
        if (id != 5)
            ADD_FAILURE() << "Trying to delete renderbuffer id " << id;
    }

    virtual void deleteShader(WebGLId id)
    {
        if (id != 6)
            ADD_FAILURE() << "Trying to delete shader id " << id;
    }

    virtual WebGLId createTexture()
    {
        unsigned textureId = FakeWebGraphicsContext3D::createTexture();
        m_allocatedTextureIds.insert(textureId);
        return textureId;
    }
    virtual void deleteTexture(WebGLId id)
    {
        if (!ContainsKey(m_allocatedTextureIds, id))
            ADD_FAILURE() << "Trying to delete texture id " << id;
        m_allocatedTextureIds.erase(id);
    }

    virtual void bindBuffer(WGC3Denum, WebGLId id)
    {
        if (id != 2 && id)
            ADD_FAILURE() << "Trying to bind buffer id " << id;
    }

    virtual void bindFramebuffer(WGC3Denum, WebGLId id)
    {
        if (id != 3 && id)
            ADD_FAILURE() << "Trying to bind framebuffer id " << id;
    }

    virtual void useProgram(WebGLId id)
    {
        if (id != 4)
            ADD_FAILURE() << "Trying to use program id " << id;
    }

    virtual void bindRenderbuffer(WGC3Denum, WebGLId id)
    {
        if (id != 5 && id)
            ADD_FAILURE() << "Trying to bind renderbuffer id " << id;
    }

    virtual void attachShader(WebGLId program, WebGLId shader)
    {
        if ((program != 4) || (shader != 6))
            ADD_FAILURE() << "Trying to attach shader id " << shader << " to program id " << program;
    }

    virtual void bindTexture(WGC3Denum, WebGLId id)
    {
        if (id && !ContainsKey(m_allocatedTextureIds, id))
            ADD_FAILURE() << "Trying to bind texture id " << id;
    }

private:
    base::hash_set<unsigned> m_allocatedTextureIds;
};

// Fake video frame that represents a 4x4 YUV video frame.
class FakeVideoFrame: public WebVideoFrame {
public:
    FakeVideoFrame() : m_textureId(0) { memset(m_data, 0x80, sizeof(m_data)); }
    virtual ~FakeVideoFrame() { }
    virtual Format format() const { return m_textureId ? FormatNativeTexture : FormatYV12; }
    virtual unsigned width() const { return 4; }
    virtual unsigned height() const { return 4; }
    virtual unsigned planes() const { return m_textureId ? 0 : 3; }
    virtual int stride(unsigned plane) const { return 4; }
    virtual const void* data(unsigned plane) const { return m_data; }
    virtual unsigned textureId() const { return m_textureId; }
    virtual unsigned textureTarget() const { return m_textureId ? GL_TEXTURE_2D : 0; }

    void setTextureId(unsigned id) { m_textureId = id; }

private:
    char m_data[16];
    unsigned m_textureId;
};

// Fake video frame provider that always provides the same FakeVideoFrame.
class FakeVideoFrameProvider: public WebVideoFrameProvider {
public:
    FakeVideoFrameProvider() : m_frame(0), m_client(0) { }
    virtual ~FakeVideoFrameProvider()
    {
        if (m_client)
            m_client->stopUsingProvider();
    }

    virtual void setVideoFrameProviderClient(Client* client) { m_client = client; }
    virtual WebVideoFrame* getCurrentFrame() { return m_frame; }
    virtual void putCurrentFrame(WebVideoFrame*) { }

    void setFrame(WebVideoFrame* frame) { m_frame = frame; }

private:
    WebVideoFrame* m_frame;
    Client* m_client;
};

class StrictWebGraphicsContext3DWithIOSurface : public StrictWebGraphicsContext3D {
public:
    virtual WebString getString(WGC3Denum name) OVERRIDE
    {
        if (name == GL_EXTENSIONS)
            return WebString("GL_CHROMIUM_iosurface GL_ARB_texture_rectangle");

        return WebString();
    }
};

class FakeWebGraphicsContext3DWithIOSurface : public FakeWebGraphicsContext3D {
public:
    virtual WebString getString(WGC3Denum name) OVERRIDE
    {
        if (name == GL_EXTENSIONS)
            return WebString("GL_CHROMIUM_iosurface GL_ARB_texture_rectangle");

        return WebString();
    }
};

class FakeWebScrollbarThemeGeometryNonEmpty : public FakeWebScrollbarThemeGeometry {
    virtual WebRect trackRect(WebScrollbar*) OVERRIDE { return WebRect(0, 0, 10, 10); }
    virtual WebRect thumbRect(WebScrollbar*) OVERRIDE { return WebRect(0, 5, 5, 2); }
    virtual void splitTrack(WebScrollbar*, const WebRect& track, WebRect& startTrack, WebRect& thumb, WebRect& endTrack) OVERRIDE
    {
        thumb = WebRect(0, 5, 5, 2);
        startTrack = WebRect(0, 5, 0, 5);
        endTrack = WebRect(0, 0, 0, 5);
    }
};

class FakeScrollbarLayerImpl : public ScrollbarLayerImpl {
public:
    static scoped_ptr<FakeScrollbarLayerImpl> create(int id)
    {
        return make_scoped_ptr(new FakeScrollbarLayerImpl(id));
    }

    void createResources(ResourceProvider* provider)
    {
        DCHECK(provider);
        int pool = 0;
        IntSize size(10, 10);
        GLenum format = GL_RGBA;
        ResourceProvider::TextureUsageHint hint = ResourceProvider::TextureUsageAny;
        setScrollbarGeometry(ScrollbarGeometryFixedThumb::create(FakeWebScrollbarThemeGeometryNonEmpty::create()));

        setBackTrackResourceId(provider->createResource(pool, size, format, hint));
        setForeTrackResourceId(provider->createResource(pool, size, format, hint));
        setThumbResourceId(provider->createResource(pool, size, format, hint));
    }

protected:
    explicit FakeScrollbarLayerImpl(int id)
        : ScrollbarLayerImpl(id)
    {
    }
};

static inline scoped_ptr<RenderPass> createRenderPassWithResource(ResourceProvider* provider)
{
    ResourceProvider::ResourceId resourceId = provider->createResource(0, IntSize(1, 1), GL_RGBA, ResourceProvider::TextureUsageAny);

    scoped_ptr<TestRenderPass> pass = TestRenderPass::create(RenderPass::Id(1, 1), IntRect(0, 0, 1, 1), WebTransformationMatrix());
    scoped_ptr<SharedQuadState> sharedState = SharedQuadState::create(WebTransformationMatrix(), IntRect(0, 0, 1, 1), IntRect(0, 0, 1, 1), 1, false);
    scoped_ptr<TextureDrawQuad> quad = TextureDrawQuad::create(sharedState.get(), IntRect(0, 0, 1, 1), resourceId, false, FloatRect(0, 0, 1, 1), false);

    pass->appendSharedQuadState(sharedState.Pass());
    pass->appendQuad(quad.PassAs<DrawQuad>());

    return pass.PassAs<RenderPass>();
}

TEST_P(LayerTreeHostImplTest, dontUseOldResourcesAfterLostContext)
{
    int layerId = 1;

    scoped_ptr<LayerImpl> rootLayer(LayerImpl::create(layerId++));
    rootLayer->setBounds(IntSize(10, 10));
    rootLayer->setAnchorPoint(FloatPoint(0, 0));

    scoped_ptr<TiledLayerImpl> tileLayer = TiledLayerImpl::create(layerId++);
    tileLayer->setBounds(IntSize(10, 10));
    tileLayer->setAnchorPoint(FloatPoint(0, 0));
    tileLayer->setContentBounds(IntSize(10, 10));
    tileLayer->setDrawsContent(true);
    tileLayer->setSkipsDraw(false);
    scoped_ptr<LayerTilingData> tilingData(LayerTilingData::create(IntSize(10, 10), LayerTilingData::NoBorderTexels));
    tilingData->setBounds(IntSize(10, 10));
    tileLayer->setTilingData(*tilingData);
    tileLayer->pushTileProperties(0, 0, 1, IntRect(0, 0, 10, 10), false);
    rootLayer->addChild(tileLayer.PassAs<LayerImpl>());

    scoped_ptr<TextureLayerImpl> textureLayer = TextureLayerImpl::create(layerId++);
    textureLayer->setBounds(IntSize(10, 10));
    textureLayer->setAnchorPoint(FloatPoint(0, 0));
    textureLayer->setContentBounds(IntSize(10, 10));
    textureLayer->setDrawsContent(true);
    textureLayer->setTextureId(1);
    rootLayer->addChild(textureLayer.PassAs<LayerImpl>());

    scoped_ptr<TiledLayerImpl> maskLayer = TiledLayerImpl::create(layerId++);
    maskLayer->setBounds(IntSize(10, 10));
    maskLayer->setAnchorPoint(FloatPoint(0, 0));
    maskLayer->setContentBounds(IntSize(10, 10));
    maskLayer->setDrawsContent(true);
    maskLayer->setSkipsDraw(false);
    maskLayer->setTilingData(*tilingData);
    maskLayer->pushTileProperties(0, 0, 1, IntRect(0, 0, 10, 10), false);

    scoped_ptr<TextureLayerImpl> textureLayerWithMask = TextureLayerImpl::create(layerId++);
    textureLayerWithMask->setBounds(IntSize(10, 10));
    textureLayerWithMask->setAnchorPoint(FloatPoint(0, 0));
    textureLayerWithMask->setContentBounds(IntSize(10, 10));
    textureLayerWithMask->setDrawsContent(true);
    textureLayerWithMask->setTextureId(1);
    textureLayerWithMask->setMaskLayer(maskLayer.PassAs<LayerImpl>());
    rootLayer->addChild(textureLayerWithMask.PassAs<LayerImpl>());

    FakeVideoFrame videoFrame;
    FakeVideoFrameProvider provider;
    provider.setFrame(&videoFrame);
    scoped_ptr<VideoLayerImpl> videoLayer = VideoLayerImpl::create(layerId++, &provider);
    videoLayer->setBounds(IntSize(10, 10));
    videoLayer->setAnchorPoint(FloatPoint(0, 0));
    videoLayer->setContentBounds(IntSize(10, 10));
    videoLayer->setDrawsContent(true);
    videoLayer->setLayerTreeHostImpl(m_hostImpl.get());
    rootLayer->addChild(videoLayer.PassAs<LayerImpl>());

    FakeVideoFrame hwVideoFrame;
    FakeVideoFrameProvider hwProvider;
    hwProvider.setFrame(&hwVideoFrame);
    scoped_ptr<VideoLayerImpl> hwVideoLayer = VideoLayerImpl::create(layerId++, &hwProvider);
    hwVideoLayer->setBounds(IntSize(10, 10));
    hwVideoLayer->setAnchorPoint(FloatPoint(0, 0));
    hwVideoLayer->setContentBounds(IntSize(10, 10));
    hwVideoLayer->setDrawsContent(true);
    hwVideoLayer->setLayerTreeHostImpl(m_hostImpl.get());
    rootLayer->addChild(hwVideoLayer.PassAs<LayerImpl>());

    scoped_ptr<IOSurfaceLayerImpl> ioSurfaceLayer = IOSurfaceLayerImpl::create(layerId++);
    ioSurfaceLayer->setBounds(IntSize(10, 10));
    ioSurfaceLayer->setAnchorPoint(FloatPoint(0, 0));
    ioSurfaceLayer->setContentBounds(IntSize(10, 10));
    ioSurfaceLayer->setDrawsContent(true);
    ioSurfaceLayer->setIOSurfaceProperties(1, IntSize(10, 10));
    ioSurfaceLayer->setLayerTreeHostImpl(m_hostImpl.get());
    rootLayer->addChild(ioSurfaceLayer.PassAs<LayerImpl>());

    scoped_ptr<HeadsUpDisplayLayerImpl> hudLayer = HeadsUpDisplayLayerImpl::create(layerId++);
    hudLayer->setBounds(IntSize(10, 10));
    hudLayer->setAnchorPoint(FloatPoint(0, 0));
    hudLayer->setContentBounds(IntSize(10, 10));
    hudLayer->setDrawsContent(true);
    hudLayer->setLayerTreeHostImpl(m_hostImpl.get());
    rootLayer->addChild(hudLayer.PassAs<LayerImpl>());

    scoped_ptr<FakeScrollbarLayerImpl> scrollbarLayer(FakeScrollbarLayerImpl::create(layerId++));
    scrollbarLayer->setBounds(IntSize(10, 10));
    scrollbarLayer->setContentBounds(IntSize(10, 10));
    scrollbarLayer->setDrawsContent(true);
    scrollbarLayer->setLayerTreeHostImpl(m_hostImpl.get());
    scrollbarLayer->createResources(m_hostImpl->resourceProvider());
    rootLayer->addChild(scrollbarLayer.PassAs<LayerImpl>());

    scoped_ptr<DelegatedRendererLayerImpl> delegatedRendererLayer(DelegatedRendererLayerImpl::create(layerId++));
    delegatedRendererLayer->setBounds(IntSize(10, 10));
    delegatedRendererLayer->setContentBounds(IntSize(10, 10));
    delegatedRendererLayer->setDrawsContent(true);
    delegatedRendererLayer->setLayerTreeHostImpl(m_hostImpl.get());
    ScopedPtrVector<RenderPass> passList;
    passList.append(createRenderPassWithResource(m_hostImpl->resourceProvider()));
    delegatedRendererLayer->setRenderPasses(passList);
    EXPECT_TRUE(passList.isEmpty());
    rootLayer->addChild(delegatedRendererLayer.PassAs<LayerImpl>());

    // Use a context that supports IOSurfaces
    m_hostImpl->initializeRenderer(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new FakeWebGraphicsContext3DWithIOSurface)).PassAs<GraphicsContext>());

    hwVideoFrame.setTextureId(m_hostImpl->resourceProvider()->graphicsContext3D()->createTexture());

    m_hostImpl->setRootLayer(rootLayer.Pass());

    LayerTreeHostImpl::FrameData frame;
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);
    m_hostImpl->swapBuffers();

    unsigned numResources = m_hostImpl->resourceProvider()->numResources();

    // Lose the context, replacing it with a StrictWebGraphicsContext3DWithIOSurface,
    // that will warn if any resource from the previous context gets used.
    m_hostImpl->initializeRenderer(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new StrictWebGraphicsContext3DWithIOSurface)).PassAs<GraphicsContext>());

    // Create dummy resources so that looking up an old resource will get an
    // invalid texture id mapping.
    for (unsigned i = 0; i < numResources; ++i)
        m_hostImpl->resourceProvider()->createResourceFromExternalTexture(1);

    // The WebVideoFrameProvider is expected to recreate its textures after a
    // lost context (or not serve a frame).
    hwProvider.setFrame(0);

    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);
    m_hostImpl->swapBuffers();

    hwVideoFrame.setTextureId(m_hostImpl->resourceProvider()->graphicsContext3D()->createTexture());
    hwProvider.setFrame(&hwVideoFrame);

    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);
    m_hostImpl->swapBuffers();
}

// Fake WebGraphicsContext3D that tracks the number of textures in use.
class TrackingWebGraphicsContext3D : public FakeWebGraphicsContext3D {
public:
    TrackingWebGraphicsContext3D()
        : FakeWebGraphicsContext3D()
        , m_numTextures(0)
    { }

    virtual WebGLId createTexture() OVERRIDE
    {
        WebGLId id = FakeWebGraphicsContext3D::createTexture();

        m_textures[id] = true;
        ++m_numTextures;
        return id;
    }

    virtual void deleteTexture(WebGLId id) OVERRIDE
    {
        if (m_textures.find(id) == m_textures.end())
            return;

        m_textures[id] = false;
        --m_numTextures;
    }

    virtual WebString getString(WGC3Denum name) OVERRIDE
    {
        if (name == GL_EXTENSIONS)
            return WebString("GL_CHROMIUM_iosurface GL_ARB_texture_rectangle");

        return WebString();
    }

    unsigned numTextures() const { return m_numTextures; }

private:
    base::hash_map<WebGLId, bool> m_textures;
    unsigned m_numTextures;
};

TEST_P(LayerTreeHostImplTest, layersFreeTextures)
{
    scoped_ptr<LayerImpl> rootLayer(LayerImpl::create(1));
    rootLayer->setBounds(IntSize(10, 10));
    rootLayer->setAnchorPoint(FloatPoint(0, 0));

    scoped_ptr<TiledLayerImpl> tileLayer = TiledLayerImpl::create(2);
    tileLayer->setBounds(IntSize(10, 10));
    tileLayer->setAnchorPoint(FloatPoint(0, 0));
    tileLayer->setContentBounds(IntSize(10, 10));
    tileLayer->setDrawsContent(true);
    tileLayer->setSkipsDraw(false);
    scoped_ptr<LayerTilingData> tilingData(LayerTilingData::create(IntSize(10, 10), LayerTilingData::NoBorderTexels));
    tilingData->setBounds(IntSize(10, 10));
    tileLayer->setTilingData(*tilingData);
    tileLayer->pushTileProperties(0, 0, 1, IntRect(0, 0, 10, 10), false);
    rootLayer->addChild(tileLayer.PassAs<LayerImpl>());

    scoped_ptr<TextureLayerImpl> textureLayer = TextureLayerImpl::create(3);
    textureLayer->setBounds(IntSize(10, 10));
    textureLayer->setAnchorPoint(FloatPoint(0, 0));
    textureLayer->setContentBounds(IntSize(10, 10));
    textureLayer->setDrawsContent(true);
    textureLayer->setTextureId(1);
    rootLayer->addChild(textureLayer.PassAs<LayerImpl>());

    FakeVideoFrameProvider provider;
    scoped_ptr<VideoLayerImpl> videoLayer = VideoLayerImpl::create(4, &provider);
    videoLayer->setBounds(IntSize(10, 10));
    videoLayer->setAnchorPoint(FloatPoint(0, 0));
    videoLayer->setContentBounds(IntSize(10, 10));
    videoLayer->setDrawsContent(true);
    videoLayer->setLayerTreeHostImpl(m_hostImpl.get());
    rootLayer->addChild(videoLayer.PassAs<LayerImpl>());

    scoped_ptr<IOSurfaceLayerImpl> ioSurfaceLayer = IOSurfaceLayerImpl::create(5);
    ioSurfaceLayer->setBounds(IntSize(10, 10));
    ioSurfaceLayer->setAnchorPoint(FloatPoint(0, 0));
    ioSurfaceLayer->setContentBounds(IntSize(10, 10));
    ioSurfaceLayer->setDrawsContent(true);
    ioSurfaceLayer->setIOSurfaceProperties(1, IntSize(10, 10));
    ioSurfaceLayer->setLayerTreeHostImpl(m_hostImpl.get());
    rootLayer->addChild(ioSurfaceLayer.PassAs<LayerImpl>());

    // Lose the context, replacing it with a TrackingWebGraphicsContext3D (which the LayerTreeHostImpl takes ownership of).
    scoped_ptr<GraphicsContext> outputSurface(FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new TrackingWebGraphicsContext3D)));
    TrackingWebGraphicsContext3D* trackingWebGraphicsContext = static_cast<TrackingWebGraphicsContext3D*>(outputSurface->context3D());
    m_hostImpl->initializeRenderer(outputSurface.Pass());

    m_hostImpl->setRootLayer(rootLayer.Pass());

    LayerTreeHostImpl::FrameData frame;
    EXPECT_TRUE(m_hostImpl->prepareToDraw(frame));
    m_hostImpl->drawLayers(frame);
    m_hostImpl->didDrawAllLayers(frame);
    m_hostImpl->swapBuffers();

    EXPECT_GT(trackingWebGraphicsContext->numTextures(), 0u);

    // Kill the layer tree.
    m_hostImpl->setRootLayer(LayerImpl::create(100));
    // There should be no textures left in use after.
    EXPECT_EQ(0u, trackingWebGraphicsContext->numTextures());
}

class MockDrawQuadsToFillScreenContext : public FakeWebGraphicsContext3D {
public:
    MOCK_METHOD1(useProgram, void(WebGLId program));
    MOCK_METHOD4(drawElements, void(WGC3Denum mode, WGC3Dsizei count, WGC3Denum type, WGC3Dintptr offset));
};

TEST_P(LayerTreeHostImplTest, hasTransparentBackground)
{
    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new MockDrawQuadsToFillScreenContext)).PassAs<GraphicsContext>();
    MockDrawQuadsToFillScreenContext* mockContext = static_cast<MockDrawQuadsToFillScreenContext*>(context->context3D());

    // Run test case
    scoped_ptr<LayerTreeHostImpl> myHostImpl = createLayerTreeHost(false, context.Pass(), LayerImpl::create(1));
    myHostImpl->setBackgroundColor(SK_ColorWHITE);

    // Verify one quad is drawn when transparent background set is not set.
    myHostImpl->setHasTransparentBackground(false);
    EXPECT_CALL(*mockContext, useProgram(_))
        .Times(1);
    EXPECT_CALL(*mockContext, drawElements(_, _, _, _))
        .Times(1);
    LayerTreeHostImpl::FrameData frame;
    EXPECT_TRUE(myHostImpl->prepareToDraw(frame));
    myHostImpl->drawLayers(frame);
    myHostImpl->didDrawAllLayers(frame);
    Mock::VerifyAndClearExpectations(&mockContext);

    // Verify no quads are drawn when transparent background is set.
    myHostImpl->setHasTransparentBackground(true);
    EXPECT_TRUE(myHostImpl->prepareToDraw(frame));
    myHostImpl->drawLayers(frame);
    myHostImpl->didDrawAllLayers(frame);
    Mock::VerifyAndClearExpectations(&mockContext);
}

static void addDrawingLayerTo(LayerImpl* parent, int id, const IntRect& layerRect, LayerImpl** result)
{
    scoped_ptr<LayerImpl> layer = FakeLayerWithQuads::create(id);
    LayerImpl* layerPtr = layer.get();
    layerPtr->setAnchorPoint(FloatPoint(0, 0));
    layerPtr->setPosition(FloatPoint(layerRect.location()));
    layerPtr->setBounds(layerRect.size());
    layerPtr->setContentBounds(layerRect.size());
    layerPtr->setDrawsContent(true); // only children draw content
    layerPtr->setContentsOpaque(true);
    parent->addChild(layer.Pass());
    if (result)
        *result = layerPtr;
}

static void setupLayersForTextureCaching(LayerTreeHostImpl* layerTreeHostImpl, LayerImpl*& rootPtr, LayerImpl*& intermediateLayerPtr, LayerImpl*& surfaceLayerPtr, LayerImpl*& childPtr, const IntSize& rootSize)
{
    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new PartialSwapContext)).PassAs<GraphicsContext>();

    layerTreeHostImpl->initializeRenderer(context.Pass());
    layerTreeHostImpl->setViewportSize(rootSize, rootSize);

    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    rootPtr = root.get();

    root->setAnchorPoint(FloatPoint(0, 0));
    root->setPosition(FloatPoint(0, 0));
    root->setBounds(rootSize);
    root->setContentBounds(rootSize);
    root->setDrawsContent(true);
    layerTreeHostImpl->setRootLayer(root.Pass());

    addDrawingLayerTo(rootPtr, 2, IntRect(10, 10, rootSize.width(), rootSize.height()), &intermediateLayerPtr);
    intermediateLayerPtr->setDrawsContent(false); // only children draw content

    // Surface layer is the layer that changes its opacity
    // It will contain other layers that draw content.
    addDrawingLayerTo(intermediateLayerPtr, 3, IntRect(10, 10, rootSize.width(), rootSize.height()), &surfaceLayerPtr);
    surfaceLayerPtr->setDrawsContent(false); // only children draw content
    surfaceLayerPtr->setOpacity(0.5f); // This will cause it to have a surface

    // Child of the surface layer will produce some quads
    addDrawingLayerTo(surfaceLayerPtr, 4, IntRect(5, 5, rootSize.width() - 25, rootSize.height() - 25), &childPtr);
}

class GLRendererWithReleaseTextures : public GLRenderer {
public:
    using GLRenderer::releaseRenderPassTextures;
};

TEST_P(LayerTreeHostImplTest, textureCachingWithClipping)
{
    Settings::setPartialSwapEnabled(true);

    LayerTreeSettings settings;
    settings.minimumOcclusionTrackingSize = IntSize();
    scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, this);

    LayerImpl* rootPtr;
    LayerImpl* surfaceLayerPtr;

    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new PartialSwapContext)).PassAs<GraphicsContext>();

    IntSize rootSize(100, 100);

    myHostImpl->initializeRenderer(context.Pass());
    myHostImpl->setViewportSize(IntSize(rootSize.width(), rootSize.height()), IntSize(rootSize.width(), rootSize.height()));

    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    rootPtr = root.get();

    root->setAnchorPoint(FloatPoint(0, 0));
    root->setPosition(FloatPoint(0, 0));
    root->setBounds(rootSize);
    root->setContentBounds(rootSize);
    root->setDrawsContent(true);
    root->setMasksToBounds(true);
    myHostImpl->setRootLayer(root.Pass());

    addDrawingLayerTo(rootPtr, 3, IntRect(0, 0, rootSize.width(), rootSize.height()), &surfaceLayerPtr);
    surfaceLayerPtr->setDrawsContent(false);

    // Surface layer is the layer that changes its opacity
    // It will contain other layers that draw content.
    surfaceLayerPtr->setOpacity(0.5f); // This will cause it to have a surface

    addDrawingLayerTo(surfaceLayerPtr, 4, IntRect(0, 0, 100, 3), 0);
    addDrawingLayerTo(surfaceLayerPtr, 5, IntRect(0, 97, 100, 3), 0);

    // Rotation will put part of the child ouside the bounds of the root layer.
    // Nevertheless, the child layers should be drawn.
    WebTransformationMatrix transform = surfaceLayerPtr->transform();
    transform.translate(50, 50);
    transform.rotate(35);
    transform.translate(-50, -50);
    surfaceLayerPtr->setTransform(transform);

    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive two render passes, each with one quad
        ASSERT_EQ(2U, frame.renderPasses.size());
        EXPECT_EQ(2U, frame.renderPasses[0]->quadList().size());
        ASSERT_EQ(1U, frame.renderPasses[1]->quadList().size());

        // Verify that the child layers are being clipped.
        gfx::Rect quadVisibleRect = frame.renderPasses[0]->quadList()[0]->quadVisibleRect();
        EXPECT_LT(quadVisibleRect.width(), 100);

        quadVisibleRect = frame.renderPasses[0]->quadList()[1]->quadVisibleRect();
        EXPECT_LT(quadVisibleRect.width(), 100);

        // Verify that the render surface texture is *not* clipped.
        EXPECT_RECT_EQ(IntRect(0, 0, 100, 100), frame.renderPasses[0]->outputRect());

        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[1]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[1]->quadList()[0]);
        EXPECT_FALSE(quad->contentsChangedSinceLastFrame().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    transform = surfaceLayerPtr->transform();
    transform.translate(50, 50);
    transform.rotate(-35);
    transform.translate(-50, -50);
    surfaceLayerPtr->setTransform(transform);

    // The surface is now aligned again, and the clipped parts are exposed.
    // Since the layers were clipped, even though the render surface size
    // was not changed, the texture should not be saved.
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive two render passes, each with one quad
        ASSERT_EQ(2U, frame.renderPasses.size());
        EXPECT_EQ(2U, frame.renderPasses[0]->quadList().size());
        ASSERT_EQ(1U, frame.renderPasses[1]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }
}

TEST_P(LayerTreeHostImplTest, textureCachingWithOcclusion)
{
    Settings::setPartialSwapEnabled(false);

    LayerTreeSettings settings;
    settings.minimumOcclusionTrackingSize = IntSize();
    scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, this);

    // Layers are structure as follows:
    //
    //  R +-- S1 +- L10 (owning)
    //    |      +- L11
    //    |      +- L12
    //    |
    //    +-- S2 +- L20 (owning)
    //           +- L21
    //
    // Occlusion:
    // L12 occludes L11 (internal)
    // L20 occludes L10 (external)
    // L21 occludes L20 (internal)

    LayerImpl* rootPtr;
    LayerImpl* layerS1Ptr;
    LayerImpl* layerS2Ptr;

    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new PartialSwapContext)).PassAs<GraphicsContext>();

    IntSize rootSize(1000, 1000);

    myHostImpl->initializeRenderer(context.Pass());
    myHostImpl->setViewportSize(IntSize(rootSize.width(), rootSize.height()), IntSize(rootSize.width(), rootSize.height()));

    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    rootPtr = root.get();

    root->setAnchorPoint(FloatPoint(0, 0));
    root->setPosition(FloatPoint(0, 0));
    root->setBounds(rootSize);
    root->setContentBounds(rootSize);
    root->setDrawsContent(true);
    root->setMasksToBounds(true);
    myHostImpl->setRootLayer(root.Pass());

    addDrawingLayerTo(rootPtr, 2, IntRect(300, 300, 300, 300), &layerS1Ptr);
    layerS1Ptr->setForceRenderSurface(true);

    addDrawingLayerTo(layerS1Ptr, 3, IntRect(10, 10, 10, 10), 0); // L11
    addDrawingLayerTo(layerS1Ptr, 4, IntRect(0, 0, 30, 30), 0); // L12

    addDrawingLayerTo(rootPtr, 5, IntRect(550, 250, 300, 400), &layerS2Ptr);
    layerS2Ptr->setForceRenderSurface(true);

    addDrawingLayerTo(layerS2Ptr, 6, IntRect(20, 20, 5, 5), 0); // L21

    // Initial draw - must receive all quads
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 3 render passes.
        // For Root, there are 2 quads; for S1, there are 2 quads (1 is occluded); for S2, there is 2 quads.
        ASSERT_EQ(3U, frame.renderPasses.size());

        EXPECT_EQ(2U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(2U, frame.renderPasses[1]->quadList().size());
        EXPECT_EQ(2U, frame.renderPasses[2]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // "Unocclude" surface S1 and repeat draw.
    // Must remove S2's render pass since it's cached;
    // Must keep S1 quads because texture contained external occlusion.
    WebTransformationMatrix transform = layerS2Ptr->transform();
    transform.translate(150, 150);
    layerS2Ptr->setTransform(transform);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 2 render passes.
        // For Root, there are 2 quads
        // For S1, the number of quads depends on what got unoccluded, so not asserted beyond being positive.
        // For S2, there is no render pass
        ASSERT_EQ(2U, frame.renderPasses.size());

        EXPECT_GT(frame.renderPasses[0]->quadList().size(), 0U);
        EXPECT_EQ(2U, frame.renderPasses[1]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // "Re-occlude" surface S1 and repeat draw.
    // Must remove S1's render pass since it is now available in full.
    // S2 has no change so must also be removed.
    transform = layerS2Ptr->transform();
    transform.translate(-15, -15);
    layerS2Ptr->setTransform(transform);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 1 render pass - for the root.
        ASSERT_EQ(1U, frame.renderPasses.size());

        EXPECT_EQ(2U, frame.renderPasses[0]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

}

TEST_P(LayerTreeHostImplTest, textureCachingWithOcclusionEarlyOut)
{
    Settings::setPartialSwapEnabled(false);

    LayerTreeSettings settings;
    settings.minimumOcclusionTrackingSize = IntSize();
    scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, this);

    // Layers are structure as follows:
    //
    //  R +-- S1 +- L10 (owning, non drawing)
    //    |      +- L11 (corner, unoccluded)
    //    |      +- L12 (corner, unoccluded)
    //    |      +- L13 (corner, unoccluded)
    //    |      +- L14 (corner, entirely occluded)
    //    |
    //    +-- S2 +- L20 (owning, drawing)
    //

    LayerImpl* rootPtr;
    LayerImpl* layerS1Ptr;
    LayerImpl* layerS2Ptr;

    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new PartialSwapContext)).PassAs<GraphicsContext>();

    IntSize rootSize(1000, 1000);

    myHostImpl->initializeRenderer(context.Pass());
    myHostImpl->setViewportSize(IntSize(rootSize.width(), rootSize.height()), IntSize(rootSize.width(), rootSize.height()));

    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    rootPtr = root.get();

    root->setAnchorPoint(FloatPoint(0, 0));
    root->setPosition(FloatPoint(0, 0));
    root->setBounds(rootSize);
    root->setContentBounds(rootSize);
    root->setDrawsContent(true);
    root->setMasksToBounds(true);
    myHostImpl->setRootLayer(root.Pass());

    addDrawingLayerTo(rootPtr, 2, IntRect(0, 0, 800, 800), &layerS1Ptr);
    layerS1Ptr->setForceRenderSurface(true);
    layerS1Ptr->setDrawsContent(false);

    addDrawingLayerTo(layerS1Ptr, 3, IntRect(0, 0, 300, 300), 0); // L11
    addDrawingLayerTo(layerS1Ptr, 4, IntRect(0, 500, 300, 300), 0); // L12
    addDrawingLayerTo(layerS1Ptr, 5, IntRect(500, 0, 300, 300), 0); // L13
    addDrawingLayerTo(layerS1Ptr, 6, IntRect(500, 500, 300, 300), 0); // L14
    addDrawingLayerTo(layerS1Ptr, 9, IntRect(500, 500, 300, 300), 0); // L14

    addDrawingLayerTo(rootPtr, 7, IntRect(450, 450, 450, 450), &layerS2Ptr);
    layerS2Ptr->setForceRenderSurface(true);

    // Initial draw - must receive all quads
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 3 render passes.
        // For Root, there are 2 quads; for S1, there are 3 quads; for S2, there is 1 quad.
        ASSERT_EQ(3U, frame.renderPasses.size());

        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());

        // L14 is culled, so only 3 quads.
        EXPECT_EQ(3U, frame.renderPasses[1]->quadList().size());
        EXPECT_EQ(2U, frame.renderPasses[2]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // "Unocclude" surface S1 and repeat draw.
    // Must remove S2's render pass since it's cached;
    // Must keep S1 quads because texture contained external occlusion.
    WebTransformationMatrix transform = layerS2Ptr->transform();
    transform.translate(100, 100);
    layerS2Ptr->setTransform(transform);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 2 render passes.
        // For Root, there are 2 quads
        // For S1, the number of quads depends on what got unoccluded, so not asserted beyond being positive.
        // For S2, there is no render pass
        ASSERT_EQ(2U, frame.renderPasses.size());

        EXPECT_GT(frame.renderPasses[0]->quadList().size(), 0U);
        EXPECT_EQ(2U, frame.renderPasses[1]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // "Re-occlude" surface S1 and repeat draw.
    // Must remove S1's render pass since it is now available in full.
    // S2 has no change so must also be removed.
    transform = layerS2Ptr->transform();
    transform.translate(-15, -15);
    layerS2Ptr->setTransform(transform);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 1 render pass - for the root.
        ASSERT_EQ(1U, frame.renderPasses.size());

        EXPECT_EQ(2U, frame.renderPasses[0]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }
}

TEST_P(LayerTreeHostImplTest, textureCachingWithOcclusionExternalOverInternal)
{
    Settings::setPartialSwapEnabled(false);

    LayerTreeSettings settings;
    settings.minimumOcclusionTrackingSize = IntSize();
    scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, this);

    // Layers are structured as follows:
    //
    //  R +-- S1 +- L10 (owning, drawing)
    //    |      +- L11 (corner, occluded by L12)
    //    |      +- L12 (opposite corner)
    //    |
    //    +-- S2 +- L20 (owning, drawing)
    //

    LayerImpl* rootPtr;
    LayerImpl* layerS1Ptr;
    LayerImpl* layerS2Ptr;

    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new PartialSwapContext)).PassAs<GraphicsContext>();

    IntSize rootSize(1000, 1000);

    myHostImpl->initializeRenderer(context.Pass());
    myHostImpl->setViewportSize(IntSize(rootSize.width(), rootSize.height()), IntSize(rootSize.width(), rootSize.height()));

    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    rootPtr = root.get();

    root->setAnchorPoint(FloatPoint(0, 0));
    root->setPosition(FloatPoint(0, 0));
    root->setBounds(rootSize);
    root->setContentBounds(rootSize);
    root->setDrawsContent(true);
    root->setMasksToBounds(true);
    myHostImpl->setRootLayer(root.Pass());

    addDrawingLayerTo(rootPtr, 2, IntRect(0, 0, 400, 400), &layerS1Ptr);
    layerS1Ptr->setForceRenderSurface(true);

    addDrawingLayerTo(layerS1Ptr, 3, IntRect(0, 0, 300, 300), 0); // L11
    addDrawingLayerTo(layerS1Ptr, 4, IntRect(100, 0, 300, 300), 0); // L12

    addDrawingLayerTo(rootPtr, 7, IntRect(200, 0, 300, 300), &layerS2Ptr);
    layerS2Ptr->setForceRenderSurface(true);

    // Initial draw - must receive all quads
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 3 render passes.
        // For Root, there are 2 quads; for S1, there are 3 quads; for S2, there is 1 quad.
        ASSERT_EQ(3U, frame.renderPasses.size());

        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(3U, frame.renderPasses[1]->quadList().size());
        EXPECT_EQ(2U, frame.renderPasses[2]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // "Unocclude" surface S1 and repeat draw.
    // Must remove S2's render pass since it's cached;
    // Must keep S1 quads because texture contained external occlusion.
    WebTransformationMatrix transform = layerS2Ptr->transform();
    transform.translate(300, 0);
    layerS2Ptr->setTransform(transform);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 2 render passes.
        // For Root, there are 2 quads
        // For S1, the number of quads depends on what got unoccluded, so not asserted beyond being positive.
        // For S2, there is no render pass
        ASSERT_EQ(2U, frame.renderPasses.size());

        EXPECT_GT(frame.renderPasses[0]->quadList().size(), 0U);
        EXPECT_EQ(2U, frame.renderPasses[1]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }
}

TEST_P(LayerTreeHostImplTest, textureCachingWithOcclusionExternalNotAligned)
{
    Settings::setPartialSwapEnabled(false);

    LayerTreeSettings settings;
    scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, this);

    // Layers are structured as follows:
    //
    //  R +-- S1 +- L10 (rotated, drawing)
    //           +- L11 (occupies half surface)

    LayerImpl* rootPtr;
    LayerImpl* layerS1Ptr;

    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new PartialSwapContext)).PassAs<GraphicsContext>();

    IntSize rootSize(1000, 1000);

    myHostImpl->initializeRenderer(context.Pass());
    myHostImpl->setViewportSize(IntSize(rootSize.width(), rootSize.height()), IntSize(rootSize.width(), rootSize.height()));

    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    rootPtr = root.get();

    root->setAnchorPoint(FloatPoint(0, 0));
    root->setPosition(FloatPoint(0, 0));
    root->setBounds(rootSize);
    root->setContentBounds(rootSize);
    root->setDrawsContent(true);
    root->setMasksToBounds(true);
    myHostImpl->setRootLayer(root.Pass());

    addDrawingLayerTo(rootPtr, 2, IntRect(0, 0, 400, 400), &layerS1Ptr);
    layerS1Ptr->setForceRenderSurface(true);
    WebTransformationMatrix transform = layerS1Ptr->transform();
    transform.translate(200, 200);
    transform.rotate(45);
    transform.translate(-200, -200);
    layerS1Ptr->setTransform(transform);

    addDrawingLayerTo(layerS1Ptr, 3, IntRect(200, 0, 200, 400), 0); // L11

    // Initial draw - must receive all quads
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 2 render passes.
        ASSERT_EQ(2U, frame.renderPasses.size());

        EXPECT_EQ(2U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(1U, frame.renderPasses[1]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Change opacity and draw. Verify we used cached texture.
    layerS1Ptr->setOpacity(0.2f);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // One render pass must be gone due to cached texture.
        ASSERT_EQ(1U, frame.renderPasses.size());

        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }
}

TEST_P(LayerTreeHostImplTest, textureCachingWithOcclusionPartialSwap)
{
    Settings::setPartialSwapEnabled(true);

    LayerTreeSettings settings;
    settings.minimumOcclusionTrackingSize = IntSize();
    scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, this);

    // Layers are structure as follows:
    //
    //  R +-- S1 +- L10 (owning)
    //    |      +- L11
    //    |      +- L12
    //    |
    //    +-- S2 +- L20 (owning)
    //           +- L21
    //
    // Occlusion:
    // L12 occludes L11 (internal)
    // L20 occludes L10 (external)
    // L21 occludes L20 (internal)

    LayerImpl* rootPtr;
    LayerImpl* layerS1Ptr;
    LayerImpl* layerS2Ptr;

    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new PartialSwapContext)).PassAs<GraphicsContext>();

    IntSize rootSize(1000, 1000);

    myHostImpl->initializeRenderer(context.Pass());
    myHostImpl->setViewportSize(IntSize(rootSize.width(), rootSize.height()), IntSize(rootSize.width(), rootSize.height()));

    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    rootPtr = root.get();

    root->setAnchorPoint(FloatPoint(0, 0));
    root->setPosition(FloatPoint(0, 0));
    root->setBounds(rootSize);
    root->setContentBounds(rootSize);
    root->setDrawsContent(true);
    root->setMasksToBounds(true);
    myHostImpl->setRootLayer(root.Pass());

    addDrawingLayerTo(rootPtr, 2, IntRect(300, 300, 300, 300), &layerS1Ptr);
    layerS1Ptr->setForceRenderSurface(true);

    addDrawingLayerTo(layerS1Ptr, 3, IntRect(10, 10, 10, 10), 0); // L11
    addDrawingLayerTo(layerS1Ptr, 4, IntRect(0, 0, 30, 30), 0); // L12

    addDrawingLayerTo(rootPtr, 5, IntRect(550, 250, 300, 400), &layerS2Ptr);
    layerS2Ptr->setForceRenderSurface(true);

    addDrawingLayerTo(layerS2Ptr, 6, IntRect(20, 20, 5, 5), 0); // L21

    // Initial draw - must receive all quads
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 3 render passes.
        // For Root, there are 2 quads; for S1, there are 2 quads (one is occluded); for S2, there is 2 quads.
        ASSERT_EQ(3U, frame.renderPasses.size());

        EXPECT_EQ(2U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(2U, frame.renderPasses[1]->quadList().size());
        EXPECT_EQ(2U, frame.renderPasses[2]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // "Unocclude" surface S1 and repeat draw.
    // Must remove S2's render pass since it's cached;
    // Must keep S1 quads because texture contained external occlusion.
    WebTransformationMatrix transform = layerS2Ptr->transform();
    transform.translate(150, 150);
    layerS2Ptr->setTransform(transform);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive 2 render passes.
        // For Root, there are 2 quads.
        // For S1, there are 2 quads.
        // For S2, there is no render pass
        ASSERT_EQ(2U, frame.renderPasses.size());

        EXPECT_EQ(2U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(2U, frame.renderPasses[1]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // "Re-occlude" surface S1 and repeat draw.
    // Must remove S1's render pass since it is now available in full.
    // S2 has no change so must also be removed.
    transform = layerS2Ptr->transform();
    transform.translate(-15, -15);
    layerS2Ptr->setTransform(transform);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Root render pass only.
        ASSERT_EQ(1U, frame.renderPasses.size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }
}

TEST_P(LayerTreeHostImplTest, textureCachingWithScissor)
{
    Settings::setPartialSwapEnabled(false);

    LayerTreeSettings settings;
    settings.minimumOcclusionTrackingSize = IntSize();
    scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, this);

    /*
      Layers are created as follows:

         +--------------------+
         |                  1 |
         |  +-----------+     |
         |  |         2 |     |
         |  | +-------------------+
         |  | |   3               |
         |  | +-------------------+
         |  |           |     |
         |  +-----------+     |
         |                    |
         |                    |
         +--------------------+

         Layers 1, 2 have render surfaces
     */
    scoped_ptr<LayerImpl> root = LayerImpl::create(1);
    scoped_ptr<TiledLayerImpl> child = TiledLayerImpl::create(2);
    scoped_ptr<LayerImpl> grandChild = LayerImpl::create(3);

    IntRect rootRect(0, 0, 100, 100);
    IntRect childRect(10, 10, 50, 50);
    IntRect grandChildRect(5, 5, 150, 150);

    scoped_ptr<GraphicsContext> context = FakeWebCompositorOutputSurface::create(scoped_ptr<WebKit::WebGraphicsContext3D>(new PartialSwapContext)).PassAs<GraphicsContext>();
    myHostImpl->initializeRenderer(context.Pass());

    root->setAnchorPoint(FloatPoint(0, 0));
    root->setPosition(FloatPoint(rootRect.x(), rootRect.y()));
    root->setBounds(IntSize(rootRect.width(), rootRect.height()));
    root->setContentBounds(root->bounds());
    root->setDrawsContent(true);
    root->setMasksToBounds(true);

    child->setAnchorPoint(FloatPoint(0, 0));
    child->setPosition(FloatPoint(childRect.x(), childRect.y()));
    child->setOpacity(0.5);
    child->setBounds(IntSize(childRect.width(), childRect.height()));
    child->setContentBounds(child->bounds());
    child->setDrawsContent(true);
    child->setSkipsDraw(false);

    // child layer has 10x10 tiles.
    scoped_ptr<LayerTilingData> tiler = LayerTilingData::create(IntSize(10, 10), LayerTilingData::HasBorderTexels);
    tiler->setBounds(child->contentBounds());
    child->setTilingData(*tiler.get());

    grandChild->setAnchorPoint(FloatPoint(0, 0));
    grandChild->setPosition(IntPoint(grandChildRect.x(), grandChildRect.y()));
    grandChild->setBounds(IntSize(grandChildRect.width(), grandChildRect.height()));
    grandChild->setContentBounds(grandChild->bounds());
    grandChild->setDrawsContent(true);

    TiledLayerImpl* childPtr = child.get();
    RenderPass::Id childPassId(childPtr->id(), 0);

    child->addChild(grandChild.Pass());
    root->addChild(child.PassAs<LayerImpl>());
    myHostImpl->setRootLayer(root.Pass());
    myHostImpl->setViewportSize(rootRect.size(), rootRect.size());

    EXPECT_FALSE(myHostImpl->renderer()->haveCachedResourcesForRenderPassId(childPassId));

    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));
        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // We should have cached textures for surface 2.
    EXPECT_TRUE(myHostImpl->renderer()->haveCachedResourcesForRenderPassId(childPassId));

    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));
        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // We should still have cached textures for surface 2 after drawing with no damage.
    EXPECT_TRUE(myHostImpl->renderer()->haveCachedResourcesForRenderPassId(childPassId));

    // Damage a single tile of surface 2.
    childPtr->setUpdateRect(IntRect(10, 10, 10, 10));

    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));
        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // We should have a cached texture for surface 2 again even though it was damaged.
    EXPECT_TRUE(myHostImpl->renderer()->haveCachedResourcesForRenderPassId(childPassId));
}

TEST_P(LayerTreeHostImplTest, surfaceTextureCaching)
{
    Settings::setPartialSwapEnabled(true);

    LayerTreeSettings settings;
    settings.minimumOcclusionTrackingSize = IntSize();
    scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, this);

    LayerImpl* rootPtr;
    LayerImpl* intermediateLayerPtr;
    LayerImpl* surfaceLayerPtr;
    LayerImpl* childPtr;

    setupLayersForTextureCaching(myHostImpl.get(), rootPtr, intermediateLayerPtr, surfaceLayerPtr, childPtr, IntSize(100, 100));

    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive two render passes, each with one quad
        ASSERT_EQ(2U, frame.renderPasses.size());
        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(1U, frame.renderPasses[1]->quadList().size());

        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[1]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[1]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_FALSE(targetPass->damageRect().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Draw without any change
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive one render pass, as the other one should be culled
        ASSERT_EQ(1U, frame.renderPasses.size());

        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[0]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[0]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_TRUE(targetPass->damageRect().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Change opacity and draw
    surfaceLayerPtr->setOpacity(0.6f);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive one render pass, as the other one should be culled
        ASSERT_EQ(1U, frame.renderPasses.size());

        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[0]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[0]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_TRUE(targetPass->damageRect().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Change less benign property and draw - should have contents changed flag
    surfaceLayerPtr->setStackingOrderChanged(true);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive two render passes, each with one quad
        ASSERT_EQ(2U, frame.renderPasses.size());

        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(DrawQuad::SolidColor, frame.renderPasses[0]->quadList()[0]->material());

        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[1]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[1]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_FALSE(targetPass->damageRect().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Change opacity again, and evict the cached surface texture.
    surfaceLayerPtr->setOpacity(0.5f);
    static_cast<GLRendererWithReleaseTextures*>(myHostImpl->renderer())->releaseRenderPassTextures();

    // Change opacity and draw
    surfaceLayerPtr->setOpacity(0.6f);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive two render passes
        ASSERT_EQ(2U, frame.renderPasses.size());

        // Even though not enough properties changed, the entire thing must be
        // redrawn as we don't have cached textures
        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(1U, frame.renderPasses[1]->quadList().size());

        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[1]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[1]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_TRUE(targetPass->damageRect().IsEmpty());

        // Was our surface evicted?
        EXPECT_FALSE(myHostImpl->renderer()->haveCachedResourcesForRenderPassId(targetPass->id()));

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Draw without any change, to make sure the state is clear
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive one render pass, as the other one should be culled
        ASSERT_EQ(1U, frame.renderPasses.size());

        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[0]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[0]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_TRUE(targetPass->damageRect().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Change location of the intermediate layer
    WebTransformationMatrix transform = intermediateLayerPtr->transform();
    transform.setM41(1.0001);
    intermediateLayerPtr->setTransform(transform);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive one render pass, as the other one should be culled.
        ASSERT_EQ(1U, frame.renderPasses.size());
        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());

        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[0]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[0]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_TRUE(targetPass->damageRect().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }
}

TEST_P(LayerTreeHostImplTest, surfaceTextureCachingNoPartialSwap)
{
    Settings::setPartialSwapEnabled(false);

    LayerTreeSettings settings;
    settings.minimumOcclusionTrackingSize = IntSize();
    scoped_ptr<LayerTreeHostImpl> myHostImpl = LayerTreeHostImpl::create(settings, this);

    LayerImpl* rootPtr;
    LayerImpl* intermediateLayerPtr;
    LayerImpl* surfaceLayerPtr;
    LayerImpl* childPtr;

    setupLayersForTextureCaching(myHostImpl.get(), rootPtr, intermediateLayerPtr, surfaceLayerPtr, childPtr, IntSize(100, 100));

    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive two render passes, each with one quad
        ASSERT_EQ(2U, frame.renderPasses.size());
        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(1U, frame.renderPasses[1]->quadList().size());

        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[1]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[1]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_FALSE(targetPass->damageRect().IsEmpty());

        EXPECT_FALSE(frame.renderPasses[0]->damageRect().IsEmpty());
        EXPECT_FALSE(frame.renderPasses[1]->damageRect().IsEmpty());

        EXPECT_FALSE(frame.renderPasses[0]->hasOcclusionFromOutsideTargetSurface());
        EXPECT_FALSE(frame.renderPasses[1]->hasOcclusionFromOutsideTargetSurface());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Draw without any change
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Even though there was no change, we set the damage to entire viewport.
        // One of the passes should be culled as a result, since contents didn't change
        // and we have cached texture.
        ASSERT_EQ(1U, frame.renderPasses.size());
        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());

        EXPECT_TRUE(frame.renderPasses[0]->damageRect().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Change opacity and draw
    surfaceLayerPtr->setOpacity(0.6f);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive one render pass, as the other one should be culled
        ASSERT_EQ(1U, frame.renderPasses.size());

        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[0]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[0]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_TRUE(targetPass->damageRect().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Change less benign property and draw - should have contents changed flag
    surfaceLayerPtr->setStackingOrderChanged(true);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive two render passes, each with one quad
        ASSERT_EQ(2U, frame.renderPasses.size());

        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(DrawQuad::SolidColor, frame.renderPasses[0]->quadList()[0]->material());

        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[1]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[1]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_FALSE(targetPass->damageRect().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Change opacity again, and evict the cached surface texture.
    surfaceLayerPtr->setOpacity(0.5f);
    static_cast<GLRendererWithReleaseTextures*>(myHostImpl->renderer())->releaseRenderPassTextures();

    // Change opacity and draw
    surfaceLayerPtr->setOpacity(0.6f);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive two render passes
        ASSERT_EQ(2U, frame.renderPasses.size());

        // Even though not enough properties changed, the entire thing must be
        // redrawn as we don't have cached textures
        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());
        EXPECT_EQ(1U, frame.renderPasses[1]->quadList().size());

        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[1]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[1]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_TRUE(targetPass->damageRect().IsEmpty());

        // Was our surface evicted?
        EXPECT_FALSE(myHostImpl->renderer()->haveCachedResourcesForRenderPassId(targetPass->id()));

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Draw without any change, to make sure the state is clear
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Even though there was no change, we set the damage to entire viewport.
        // One of the passes should be culled as a result, since contents didn't change
        // and we have cached texture.
        ASSERT_EQ(1U, frame.renderPasses.size());
        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }

    // Change location of the intermediate layer
    WebTransformationMatrix transform = intermediateLayerPtr->transform();
    transform.setM41(1.0001);
    intermediateLayerPtr->setTransform(transform);
    {
        LayerTreeHostImpl::FrameData frame;
        EXPECT_TRUE(myHostImpl->prepareToDraw(frame));

        // Must receive one render pass, as the other one should be culled.
        ASSERT_EQ(1U, frame.renderPasses.size());
        EXPECT_EQ(1U, frame.renderPasses[0]->quadList().size());

        EXPECT_EQ(DrawQuad::RenderPass, frame.renderPasses[0]->quadList()[0]->material());
        RenderPassDrawQuad* quad = static_cast<RenderPassDrawQuad*>(frame.renderPasses[0]->quadList()[0]);
        RenderPass* targetPass = frame.renderPassesById.get(quad->renderPassId());
        EXPECT_TRUE(targetPass->damageRect().IsEmpty());

        myHostImpl->drawLayers(frame);
        myHostImpl->didDrawAllLayers(frame);
    }
}

TEST_P(LayerTreeHostImplTest, releaseContentsTextureShouldTriggerCommit)
{
    setReduceMemoryResult(false);

    // Even if changing the memory limit didn't result in anything being
    // evicted, we need to re-commit because the new value may result in us
    // drawing something different than before.
    setReduceMemoryResult(false);
    m_hostImpl->setManagedMemoryPolicy(ManagedMemoryPolicy(
        m_hostImpl->memoryAllocationLimitBytes() - 1));
    EXPECT_TRUE(m_didRequestCommit);
    m_didRequestCommit = false;

    // Especially if changing the memory limit caused evictions, we need
    // to re-commit.
    setReduceMemoryResult(true);
    m_hostImpl->setManagedMemoryPolicy(ManagedMemoryPolicy(
        m_hostImpl->memoryAllocationLimitBytes() - 1));
    EXPECT_TRUE(m_didRequestCommit);
    m_didRequestCommit = false;

    // But if we set it to the same value that it was before, we shouldn't
    // re-commit.
    m_hostImpl->setManagedMemoryPolicy(ManagedMemoryPolicy(
        m_hostImpl->memoryAllocationLimitBytes()));
    EXPECT_FALSE(m_didRequestCommit);
}

struct RenderPassRemovalTestData : public LayerTreeHostImpl::FrameData {
    ScopedPtrHashMap<RenderPass::Id, TestRenderPass> renderPassCache;
    scoped_ptr<SharedQuadState> sharedQuadState;
};

class TestRenderer : public GLRenderer, public RendererClient {
public:
    static scoped_ptr<TestRenderer> create(ResourceProvider* resourceProvider)
    {
        scoped_ptr<TestRenderer> renderer(new TestRenderer(resourceProvider));
        if (!renderer->initialize())
            return scoped_ptr<TestRenderer>();

        return renderer.Pass();
    }

    void clearCachedTextures() { m_textures.clear(); }
    void setHaveCachedResourcesForRenderPassId(RenderPass::Id id) { m_textures.insert(id); }

    virtual bool haveCachedResourcesForRenderPassId(RenderPass::Id id) const OVERRIDE { return m_textures.count(id); }

    // RendererClient implementation.
    virtual const IntSize& deviceViewportSize() const OVERRIDE { return m_viewportSize; }
    virtual const LayerTreeSettings& settings() const OVERRIDE { return m_settings; }
    virtual void didLoseContext() OVERRIDE { }
    virtual void onSwapBuffersComplete() OVERRIDE { }
    virtual void setFullRootLayerDamage() OVERRIDE { }
    virtual void setManagedMemoryPolicy(const ManagedMemoryPolicy& policy) OVERRIDE { }
    virtual void enforceManagedMemoryPolicy(const ManagedMemoryPolicy& policy) OVERRIDE { }

protected:
    TestRenderer(ResourceProvider* resourceProvider) : GLRenderer(this, resourceProvider) { }

private:
    LayerTreeSettings m_settings;
    IntSize m_viewportSize;
    base::hash_set<RenderPass::Id> m_textures;
};

static void configureRenderPassTestData(const char* testScript, RenderPassRemovalTestData& testData, TestRenderer* renderer)
{
    renderer->clearCachedTextures();

    // One shared state for all quads - we don't need the correct details
    testData.sharedQuadState = SharedQuadState::create(WebTransformationMatrix(), IntRect(), IntRect(), 1.0, true);

    const char* currentChar = testScript;

    // Pre-create root pass
    RenderPass::Id rootRenderPassId = RenderPass::Id(testScript[0], testScript[1]);
    testData.renderPassCache.add(rootRenderPassId, TestRenderPass::create(rootRenderPassId, IntRect(), WebTransformationMatrix()));
    while (*currentChar) {
        int layerId = *currentChar;
        currentChar++;
        ASSERT_TRUE(currentChar);
        int index = *currentChar;
        currentChar++;

        RenderPass::Id renderPassId = RenderPass::Id(layerId, index);

        bool isReplica = false;
        if (!testData.renderPassCache.contains(renderPassId))
            isReplica = true;

        scoped_ptr<TestRenderPass> renderPass = testData.renderPassCache.take(renderPassId);

        // Cycle through quad data and create all quads
        while (*currentChar && *currentChar != '\n') {
            if (*currentChar == 's') {
                // Solid color draw quad
                scoped_ptr<SolidColorDrawQuad> quad = SolidColorDrawQuad::create(testData.sharedQuadState.get(), IntRect(0, 0, 10, 10), SK_ColorWHITE);
                
                renderPass->appendQuad(quad.PassAs<DrawQuad>());
                currentChar++;
            } else if ((*currentChar >= 'A') && (*currentChar <= 'Z')) {
                // RenderPass draw quad
                int layerId = *currentChar;
                currentChar++;
                ASSERT_TRUE(currentChar);
                int index = *currentChar;
                currentChar++;
                RenderPass::Id newRenderPassId = RenderPass::Id(layerId, index);
                ASSERT_NE(rootRenderPassId, newRenderPassId);
                bool hasTexture = false;
                bool contentsChanged = true;
                
                if (*currentChar == '[') {
                    currentChar++;
                    while (*currentChar && *currentChar != ']') {
                        switch (*currentChar) {
                        case 'c':
                            contentsChanged = false;
                            break;
                        case 't':
                            hasTexture = true;
                            break;
                        }
                        currentChar++;
                    }
                    if (*currentChar == ']')
                        currentChar++;
                }

                if (testData.renderPassCache.find(newRenderPassId) == testData.renderPassCache.end()) {
                    if (hasTexture)
                        renderer->setHaveCachedResourcesForRenderPassId(newRenderPassId);

                    testData.renderPassCache.add(newRenderPassId, TestRenderPass::create(newRenderPassId, IntRect(), WebTransformationMatrix()));
                }

                IntRect quadRect = IntRect(0, 0, 1, 1);
                IntRect contentsChangedRect = contentsChanged ? quadRect : IntRect();
                scoped_ptr<RenderPassDrawQuad> quad = RenderPassDrawQuad::create(testData.sharedQuadState.get(), quadRect, newRenderPassId, isReplica, 1, contentsChangedRect, 1, 1, 0, 0);
                renderPass->appendQuad(quad.PassAs<DrawQuad>());
            }
        }
        testData.renderPasses.insert(testData.renderPasses.begin(), renderPass.get());
        testData.renderPassesById.add(renderPassId, renderPass.PassAs<RenderPass>());
        if (*currentChar)
            currentChar++;
    }
}

void dumpRenderPassTestData(const RenderPassRemovalTestData& testData, char* buffer)
{
    char* pos = buffer;
    for (RenderPassList::const_reverse_iterator it = testData.renderPasses.rbegin(); it != testData.renderPasses.rend(); ++it) {
        const RenderPass* currentPass = *it;
        *pos = currentPass->id().layerId;
        pos++;
        *pos = currentPass->id().index;
        pos++;

        QuadList::const_iterator quadListIterator = currentPass->quadList().begin();
        while (quadListIterator != currentPass->quadList().end()) {
            DrawQuad* currentQuad = *quadListIterator;
            switch (currentQuad->material()) {
            case DrawQuad::SolidColor:
                *pos = 's';
                pos++;
                break;
            case DrawQuad::RenderPass:
                *pos = RenderPassDrawQuad::materialCast(currentQuad)->renderPassId().layerId;
                pos++;
                *pos = RenderPassDrawQuad::materialCast(currentQuad)->renderPassId().index;
                pos++;
                break;
            default:
                *pos = 'x';
                pos++;
                break;
            }
            
            quadListIterator++;
        }
        *pos = '\n';
        pos++;
    }
    *pos = '\0';
}

// Each RenderPassList is represented by a string which describes the configuration.
// The syntax of the string is as follows:
//
//                                                      RsssssX[c]ssYsssZ[t]ssW[ct]
// Identifies the render pass---------------------------^ ^^^ ^ ^   ^     ^     ^
// These are solid color quads-----------------------------+  | |   |     |     |
// Identifies RenderPassDrawQuad's RenderPass-----------------+ |   |     |     |
// This quad's contents didn't change---------------------------+   |     |     |
// This quad's contents changed and it has no texture---------------+     |     |
// This quad has texture but its contents changed-------------------------+     |
// This quad's contents didn't change and it has texture - will be removed------+
//
// Expected results have exactly the same syntax, except they do not use square brackets,
// since we only check the structure, not attributes.
//
// Test case configuration consists of initialization script and expected results,
// all in the same format.
struct TestCase {
    const char* name;
    const char* initScript;
    const char* expectedResult;
};

TestCase removeRenderPassesCases[] =
    {
        {
            "Single root pass",
            "R0ssss\n",
            "R0ssss\n"
        }, {
            "Single pass - no quads",
            "R0\n",
            "R0\n"
        }, {
            "Two passes, no removal",
            "R0ssssA0sss\n"
            "A0ssss\n",
            "R0ssssA0sss\n"
            "A0ssss\n"
        }, {
            "Two passes, remove last",
            "R0ssssA0[ct]sss\n"
            "A0ssss\n",
            "R0ssssA0sss\n"
        }, {
            "Have texture but contents changed - leave pass",
            "R0ssssA0[t]sss\n"
            "A0ssss\n",
            "R0ssssA0sss\n"
            "A0ssss\n"
        }, {
            "Contents didn't change but no texture - leave pass",
            "R0ssssA0[c]sss\n"
            "A0ssss\n",
            "R0ssssA0sss\n"
            "A0ssss\n"
        }, {
            "Replica: two quads reference the same pass; remove",
            "R0ssssA0[ct]A0[ct]sss\n"
            "A0ssss\n",
            "R0ssssA0A0sss\n"
        }, {
            "Replica: two quads reference the same pass; leave",
            "R0ssssA0[c]A0[c]sss\n"
            "A0ssss\n",
            "R0ssssA0A0sss\n"
            "A0ssss\n",
        }, {
            "Many passes, remove all",
            "R0ssssA0[ct]sss\n"
            "A0sssB0[ct]C0[ct]s\n"
            "B0sssD0[ct]ssE0[ct]F0[ct]\n"
            "E0ssssss\n"
            "C0G0[ct]\n"
            "D0sssssss\n"
            "F0sssssss\n"
            "G0sss\n",

            "R0ssssA0sss\n"
        }, {
            "Deep recursion, remove all",

            "R0sssssA0[ct]ssss\n"
            "A0ssssB0sss\n"
            "B0C0\n"
            "C0D0\n"
            "D0E0\n"
            "E0F0\n"
            "F0G0\n"
            "G0H0\n"
            "H0sssI0sss\n"
            "I0J0\n"
            "J0ssss\n",
            
            "R0sssssA0ssss\n"
        }, {
            "Wide recursion, remove all",
            "R0A0[ct]B0[ct]C0[ct]D0[ct]E0[ct]F0[ct]G0[ct]H0[ct]I0[ct]J0[ct]\n"
            "A0s\n"
            "B0s\n"
            "C0ssss\n"
            "D0ssss\n"
            "E0s\n"
            "F0\n"
            "G0s\n"
            "H0s\n"
            "I0s\n"
            "J0ssss\n",
            
            "R0A0B0C0D0E0F0G0H0I0J0\n"
        }, {
            "Remove passes regardless of cache state",
            "R0ssssA0[ct]sss\n"
            "A0sssB0C0s\n"
            "B0sssD0[c]ssE0[t]F0\n"
            "E0ssssss\n"
            "C0G0\n"
            "D0sssssss\n"
            "F0sssssss\n"
            "G0sss\n",

            "R0ssssA0sss\n"
        }, {
            "Leave some passes, remove others",

            "R0ssssA0[c]sss\n"
            "A0sssB0[t]C0[ct]s\n"
            "B0sssD0[c]ss\n"
            "C0G0\n"
            "D0sssssss\n"
            "G0sss\n",

            "R0ssssA0sss\n"
            "A0sssB0C0s\n"
            "B0sssD0ss\n"
            "D0sssssss\n"
        }, {
            0, 0, 0
        }
    };

static void verifyRenderPassTestData(TestCase& testCase, RenderPassRemovalTestData& testData)
{
    char actualResult[1024];
    dumpRenderPassTestData(testData, actualResult);
    EXPECT_STREQ(testCase.expectedResult, actualResult) << "In test case: " << testCase.name;
}

TEST_P(LayerTreeHostImplTest, testRemoveRenderPasses)
{
    scoped_ptr<GraphicsContext> context(createContext());
    ASSERT_TRUE(context->context3D());
    scoped_ptr<ResourceProvider> resourceProvider(ResourceProvider::create(context.get()));

    scoped_ptr<TestRenderer> renderer(TestRenderer::create(resourceProvider.get()));

    int testCaseIndex = 0;
    while (removeRenderPassesCases[testCaseIndex].name) {
        RenderPassRemovalTestData testData;
        configureRenderPassTestData(removeRenderPassesCases[testCaseIndex].initScript, testData, renderer.get());
        LayerTreeHostImpl::removeRenderPasses(LayerTreeHostImpl::CullRenderPassesWithCachedTextures(*renderer), testData);
        verifyRenderPassTestData(removeRenderPassesCases[testCaseIndex], testData);
        testCaseIndex++;
    }
}

INSTANTIATE_TEST_CASE_P(LayerTreeHostImplTests,
                        LayerTreeHostImplTest,
                        ::testing::Values(false, true));

} // namespace
