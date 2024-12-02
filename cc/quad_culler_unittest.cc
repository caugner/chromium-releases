// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/quad_culler.h"

#include "cc/append_quads_data.h"
#include "cc/layer_tiling_data.h"
#include "cc/math_util.h"
#include "cc/occlusion_tracker.h"
#include "cc/overdraw_metrics.h"
#include "cc/single_thread_proxy.h"
#include "cc/tile_draw_quad.h"
#include "cc/tiled_layer_impl.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include <public/WebTransformationMatrix.h>

using namespace cc;
using WebKit::WebTransformationMatrix;

namespace {

class TestOcclusionTrackerImpl : public OcclusionTrackerImpl {
public:
    TestOcclusionTrackerImpl(const IntRect& scissorRectInScreen, bool recordMetricsForFrame = true)
        : OcclusionTrackerImpl(scissorRectInScreen, recordMetricsForFrame)
        , m_scissorRectInScreen(scissorRectInScreen)
    {
    }

protected:
    virtual IntRect layerScissorRectInTargetSurface(const LayerImpl* layer) const { return m_scissorRectInScreen; }

private:
    IntRect m_scissorRectInScreen;
};

typedef LayerIterator<LayerImpl, std::vector<LayerImpl*>, RenderSurfaceImpl, LayerIteratorActions::FrontToBack> LayerIteratorType;

static scoped_ptr<TiledLayerImpl> makeLayer(TiledLayerImpl* parent, const WebTransformationMatrix& drawTransform, const IntRect& layerRect, float opacity, bool opaque, const IntRect& layerOpaqueRect, std::vector<LayerImpl*>& surfaceLayerList)
{
    scoped_ptr<TiledLayerImpl> layer = TiledLayerImpl::create(1);
    scoped_ptr<LayerTilingData> tiler = LayerTilingData::create(IntSize(100, 100), LayerTilingData::NoBorderTexels);
    tiler->setBounds(layerRect.size());
    layer->setTilingData(*tiler);
    layer->setSkipsDraw(false);
    layer->setDrawTransform(drawTransform);
    layer->setScreenSpaceTransform(drawTransform);
    layer->setVisibleContentRect(layerRect);
    layer->setDrawOpacity(opacity);
    layer->setContentsOpaque(opaque);
    layer->setBounds(layerRect.size());
    layer->setContentBounds(layerRect.size());

    ResourceProvider::ResourceId resourceId = 1;
    for (int i = 0; i < tiler->numTilesX(); ++i)
        for (int j = 0; j < tiler->numTilesY(); ++j) {
            IntRect tileOpaqueRect = opaque ? tiler->tileBounds(i, j) : intersection(tiler->tileBounds(i, j), layerOpaqueRect);
            layer->pushTileProperties(i, j, resourceId++, tileOpaqueRect, false);
        }

    IntRect rectInTarget = MathUtil::mapClippedRect(layer->drawTransform(), layer->visibleContentRect());
    if (!parent) {
        layer->createRenderSurface();
        surfaceLayerList.push_back(layer.get());
        layer->renderSurface()->layerList().push_back(layer.get());
    } else {
        layer->setRenderTarget(parent->renderTarget());
        parent->renderSurface()->layerList().push_back(layer.get());
        rectInTarget.unite(MathUtil::mapClippedRect(parent->drawTransform(), parent->visibleContentRect()));
    }
    layer->setDrawableContentRect(rectInTarget);

    return layer.Pass();
}

static void appendQuads(QuadList& quadList, SharedQuadStateList& sharedStateList, TiledLayerImpl* layer, LayerIteratorType& it, OcclusionTrackerImpl& occlusionTracker)
{
    occlusionTracker.enterLayer(it);
    QuadCuller quadCuller(quadList, sharedStateList, layer, &occlusionTracker, false, false);
    AppendQuadsData data;
    layer->appendQuads(quadCuller, data);
    occlusionTracker.leaveLayer(it);
    ++it;
}

#define DECLARE_AND_INITIALIZE_TEST_QUADS               \
    DebugScopedSetImplThread impl;                      \
    QuadList quadList;                                \
    SharedQuadStateList sharedStateList;              \
    std::vector<LayerImpl*> renderSurfaceLayerList;   \
    WebTransformationMatrix childTransform;             \
    IntSize rootSize = IntSize(300, 300);               \
    IntRect rootRect = IntRect(IntPoint(), rootSize);   \
    IntSize childSize = IntSize(200, 200);              \
    IntRect childRect = IntRect(IntPoint(), childSize);

TEST(QuadCullerTest, verifyNoCulling)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), WebTransformationMatrix(), childRect, 1, false, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 13u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 90000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 40000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 0, 1);
}

TEST(QuadCullerTest, verifyCullChildLinesUpTopLeft)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), WebTransformationMatrix(), childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 9u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 90000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 40000, 1);
}

TEST(QuadCullerTest, verifyCullWhenChildOpacityNotOne)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), childTransform, childRect, 0.9f, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 13u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 90000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 40000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 0, 1);
}

TEST(QuadCullerTest, verifyCullWhenChildOpaqueFlagFalse)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), childTransform, childRect, 1, false, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 13u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 90000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 40000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 0, 1);
}

TEST(QuadCullerTest, verifyCullCenterTileOnly)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    childTransform.translate(50, 50);

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), childTransform, childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    ASSERT_EQ(quadList.size(), 12u);

    gfx::Rect quadVisibleRect1 = quadList[5]->quadVisibleRect();
    EXPECT_EQ(quadVisibleRect1.height(), 50);

    gfx::Rect quadVisibleRect3 = quadList[7]->quadVisibleRect();
    EXPECT_EQ(quadVisibleRect3.width(), 50);

    // Next index is 8, not 9, since centre quad culled.
    gfx::Rect quadVisibleRect4 = quadList[8]->quadVisibleRect();
    EXPECT_EQ(quadVisibleRect4.width(), 50);
    EXPECT_EQ(quadVisibleRect4.x(), 250);

    gfx::Rect quadVisibleRect6 = quadList[10]->quadVisibleRect();
    EXPECT_EQ(quadVisibleRect6.height(), 50);
    EXPECT_EQ(quadVisibleRect6.y(), 250);

    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 100000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 30000, 1);
}

TEST(QuadCullerTest, verifyCullCenterTileNonIntegralSize1)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    childTransform.translate(100, 100);

    // Make the root layer's quad have extent (99.1, 99.1) -> (200.9, 200.9) to make
    // sure it doesn't get culled due to transform rounding.
    WebTransformationMatrix rootTransform;
    rootTransform.translate(99.1, 99.1);
    rootTransform.scale(1.018);

    rootRect = childRect = IntRect(0, 0, 100, 100);

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, rootTransform, rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), childTransform, childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 2u);

    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 20363, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 0, 1);
}

TEST(QuadCullerTest, verifyCullCenterTileNonIntegralSize2)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    // Make the child's quad slightly smaller than, and centred over, the root layer tile.
    // Verify the child does not cause the quad below to be culled due to rounding.
    childTransform.translate(100.1, 100.1);
    childTransform.scale(0.982);

    WebTransformationMatrix rootTransform;
    rootTransform.translate(100, 100);

    rootRect = childRect = IntRect(0, 0, 100, 100);

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, rootTransform, rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), childTransform, childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 2u);

    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 19643, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 0, 1);
}

TEST(QuadCullerTest, verifyCullChildLinesUpBottomRight)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    childTransform.translate(100, 100);

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), childTransform, childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 9u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 90000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 40000, 1);
}

TEST(QuadCullerTest, verifyCullSubRegion)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    childTransform.translate(50, 50);

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    IntRect childOpaqueRect(childRect.x() + childRect.width() / 4, childRect.y() + childRect.height() / 4, childRect.width() / 2, childRect.height() / 2);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), childTransform, childRect, 1, false, childOpaqueRect, renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 12u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 90000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 30000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 10000, 1);
}

TEST(QuadCullerTest, verifyCullSubRegion2)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    childTransform.translate(50, 10);

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    IntRect childOpaqueRect(childRect.x() + childRect.width() / 4, childRect.y() + childRect.height() / 4, childRect.width() / 2, childRect.height() * 3 / 4);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), childTransform, childRect, 1, false, childOpaqueRect, renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 12u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 90000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 25000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 15000, 1);
}

TEST(QuadCullerTest, verifyCullSubRegionCheckOvercull)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    childTransform.translate(50, 49);

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    IntRect childOpaqueRect(childRect.x() + childRect.width() / 4, childRect.y() + childRect.height() / 4, childRect.width() / 2, childRect.height() / 2);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), childTransform, childRect, 1, false, childOpaqueRect, renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 13u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 90000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 30000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 10000, 1);
}

TEST(QuadCullerTest, verifyNonAxisAlignedQuadsDontOcclude)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    // Use a small rotation so as to not disturb the geometry significantly.
    childTransform.rotate(1);

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), childTransform, childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 13u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 130000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 0, 1);
}

// This test requires some explanation: here we are rotating the quads to be culled.
// The 2x2 tile child layer remains in the top-left corner, unrotated, but the 3x3
// tile parent layer is rotated by 1 degree. Of the four tiles the child would
// normally occlude, three will move (slightly) out from under the child layer, and
// one moves further under the child. Only this last tile should be culled.
TEST(QuadCullerTest, verifyNonAxisAlignedQuadsSafelyCulled)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    // Use a small rotation so as to not disturb the geometry significantly.
    WebTransformationMatrix parentTransform;
    parentTransform.rotate(1);

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, parentTransform, rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), WebTransformationMatrix(), childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(-100, -100, 1000, 1000));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 12u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 100600, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 29400, 1);
}

TEST(QuadCullerTest, verifyCullOutsideScissorOverTile)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), WebTransformationMatrix(), childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(200, 100, 100, 100));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 1u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 10000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 120000, 1);
}

TEST(QuadCullerTest, verifyCullOutsideScissorOverCulledTile)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), WebTransformationMatrix(), childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(100, 100, 100, 100));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 1u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 10000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 120000, 1);
}

TEST(QuadCullerTest, verifyCullOutsideScissorOverPartialTiles)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), WebTransformationMatrix(), childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(50, 50, 200, 200));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 9u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 40000, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 90000, 1);
}

TEST(QuadCullerTest, verifyCullOutsideScissorOverNoTiles)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), WebTransformationMatrix(), childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(500, 500, 100, 100));
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 0u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 130000, 1);
}

TEST(QuadCullerTest, verifyWithoutMetrics)
{
    DECLARE_AND_INITIALIZE_TEST_QUADS

    scoped_ptr<TiledLayerImpl> rootLayer = makeLayer(0, WebTransformationMatrix(), rootRect, 1, true, IntRect(), renderSurfaceLayerList);
    scoped_ptr<TiledLayerImpl> childLayer = makeLayer(rootLayer.get(), WebTransformationMatrix(), childRect, 1, true, IntRect(), renderSurfaceLayerList);
    TestOcclusionTrackerImpl occlusionTracker(IntRect(50, 50, 200, 200), false);
    LayerIteratorType it = LayerIteratorType::begin(&renderSurfaceLayerList);

    appendQuads(quadList, sharedStateList, childLayer.get(), it, occlusionTracker);
    appendQuads(quadList, sharedStateList, rootLayer.get(), it, occlusionTracker);
    EXPECT_EQ(quadList.size(), 9u);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnOpaque(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsDrawnTranslucent(), 0, 1);
    EXPECT_NEAR(occlusionTracker.overdrawMetrics().pixelsCulledForDrawing(), 0, 1);
}


} // namespace
