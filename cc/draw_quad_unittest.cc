// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/draw_quad.h"

#include "cc/checkerboard_draw_quad.h"
#include "cc/debug_border_draw_quad.h"
#include "cc/io_surface_draw_quad.h"
#include "cc/render_pass_draw_quad.h"
#include "cc/solid_color_draw_quad.h"
#include "cc/stream_video_draw_quad.h"
#include "cc/test/geometry_test_utils.h"
#include "cc/texture_draw_quad.h"
#include "cc/tile_draw_quad.h"
#include "cc/yuv_video_draw_quad.h"
#include "testing/gtest/include/gtest/gtest.h"
#include <public/WebTransformationMatrix.h>

using WebKit::WebTransformationMatrix;

using namespace cc;

namespace {

TEST(DrawQuadTest, copySharedQuadState)
{
    WebTransformationMatrix quadTransform(1, 0.5, 0, 1, 0.5, 0);
    gfx::Rect visibleContentRect(10, 12, 14, 16);
    gfx::Rect clippedRectInTarget(19, 21, 23, 25);
    float opacity = 0.25;
    bool opaque = true;
    int id = 3;

    scoped_ptr<SharedQuadState> state(SharedQuadState::create(quadTransform, visibleContentRect, clippedRectInTarget, opacity, opaque));
    state->id = id;

    scoped_ptr<SharedQuadState> copy(state->copy());
    EXPECT_EQ(id, copy->id);
    EXPECT_EQ(quadTransform, copy->quadTransform);
    EXPECT_RECT_EQ(visibleContentRect, copy->visibleContentRect);
    EXPECT_RECT_EQ(clippedRectInTarget, copy->clippedRectInTarget);
    EXPECT_EQ(opacity, copy->opacity);
    EXPECT_EQ(opaque, copy->opaque);
}

scoped_ptr<SharedQuadState> createSharedQuadState()
{
    WebTransformationMatrix quadTransform(1, 0.5, 0, 1, 0.5, 0);
    gfx::Rect visibleContentRect(10, 12, 14, 16);
    gfx::Rect clippedRectInTarget(19, 21, 23, 25);
    float opacity = 1;
    bool opaque = false;
    int id = 3;

    scoped_ptr<SharedQuadState> state(SharedQuadState::create(quadTransform, visibleContentRect, clippedRectInTarget, opacity, opaque));
    state->id = id;
    return state.Pass();
}

void compareDrawQuad(DrawQuad* quad, DrawQuad* copy, SharedQuadState* copySharedState)
{
    EXPECT_EQ(quad->size(), copy->size());
    EXPECT_EQ(quad->material(), copy->material());
    EXPECT_EQ(quad->isDebugQuad(), copy->isDebugQuad());
    EXPECT_RECT_EQ(quad->quadRect(), copy->quadRect());
    EXPECT_RECT_EQ(quad->quadVisibleRect(), copy->quadVisibleRect());
    EXPECT_EQ(quad->opaqueRect(), copy->opaqueRect());
    EXPECT_EQ(quad->needsBlending(), copy->needsBlending());

    EXPECT_EQ(copySharedState, copy->sharedQuadState());
    EXPECT_EQ(copySharedState->id, copy->sharedQuadStateId());

    EXPECT_EQ(quad->sharedQuadStateId(), quad->sharedQuadState()->id);
    EXPECT_EQ(copy->sharedQuadStateId(), copy->sharedQuadState()->id);
}

#define CREATE_SHARED_STATE() \
    scoped_ptr<SharedQuadState> sharedState(createSharedQuadState()); \
    scoped_ptr<SharedQuadState> copySharedState(sharedState->copy()); \
    copySharedState->id = 5;

#define QUAD_DATA \
    gfx::Rect quadRect(30, 40, 50, 60); \
    gfx::Rect quadVisibleRect(40, 50, 30, 20);

#define SETUP_AND_COPY_QUAD(Type, quad) \
    quad->setQuadVisibleRect(quadVisibleRect); \
    scoped_ptr<DrawQuad> copy(quad->copy(copySharedState.get())); \
    compareDrawQuad(quad.get(), copy.get(), copySharedState.get()); \
    const Type* copyQuad = Type::materialCast(copy.get());

#define SETUP_AND_COPY_QUAD_1(Type, quad, a) \
    quad->setQuadVisibleRect(quadVisibleRect); \
    scoped_ptr<DrawQuad> copy(quad->copy(copySharedState.get(), a));    \
    compareDrawQuad(quad.get(), copy.get(), copySharedState.get()); \
    const Type* copyQuad = Type::materialCast(copy.get());

#define CREATE_QUAD_0(Type) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect)); \
    SETUP_AND_COPY_QUAD(Type, quad); \
    UNUSED_PARAM(copyQuad);

#define CREATE_QUAD_1(Type, a) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a)); \
    SETUP_AND_COPY_QUAD(Type, quad);

#define CREATE_QUAD_2(Type, a, b) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a, b)); \
    SETUP_AND_COPY_QUAD(Type, quad);

#define CREATE_QUAD_3(Type, a, b, c) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a, b, c)); \
    SETUP_AND_COPY_QUAD(Type, quad);

#define CREATE_QUAD_4(Type, a, b, c, d) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a, b, c, d)); \
    SETUP_AND_COPY_QUAD(Type, quad);

#define CREATE_QUAD_5(Type, a, b, c, d, e) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a, b, c, d, e)); \
    SETUP_AND_COPY_QUAD(Type, quad);

#define CREATE_QUAD_6(Type, a, b, c, d, e, f) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a, b, c, d, e, f)); \
    SETUP_AND_COPY_QUAD(Type, quad);

#define CREATE_QUAD_7(Type, a, b, c, d, e, f, g) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a, b, c, d, e, f, g)); \
    SETUP_AND_COPY_QUAD(Type, quad);

#define CREATE_QUAD_8(Type, a, b, c, d, e, f, g, h) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a, b, c, d, e, f, g, h)); \
    SETUP_AND_COPY_QUAD(Type, quad);

#define CREATE_QUAD_8_1(Type, a, b, c, d, e, f, g, h, copyA) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a, b, c, d, e, f, g, h)); \
    SETUP_AND_COPY_QUAD_1(Type, quad, copyA);

#define CREATE_QUAD_9(Type, a, b, c, d, e, f, g, h, i) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a, b, c, d, e, f, g, h, i)); \
    SETUP_AND_COPY_QUAD(Type, quad);

#define CREATE_QUAD_10(Type, a, b, c, d, e, f, g, h, i, j) \
    QUAD_DATA \
    scoped_ptr<Type> quad(Type::create(sharedState.get(), quadRect, a, b, c, d, e, f, g, h, i, j)); \
    SETUP_AND_COPY_QUAD(Type, quad);

TEST(DrawQuadTest, copyCheckerboardDrawQuad)
{
    SkColor color = 0xfabb0011;
    CREATE_SHARED_STATE();
    CREATE_QUAD_1(CheckerboardDrawQuad, color);
    EXPECT_EQ(color, copyQuad->color());
}

TEST(DrawQuadTest, copyDebugBorderDrawQuad)
{
    SkColor color = 0xfabb0011;
    int width = 99;
    CREATE_SHARED_STATE();
    CREATE_QUAD_2(DebugBorderDrawQuad, color, width);
    EXPECT_EQ(color, copyQuad->color());
    EXPECT_EQ(width, copyQuad->width());
}

TEST(DrawQuadTest, copyIOSurfaceDrawQuad)
{
    gfx::Size size(58, 95);
    unsigned textureId = 72;
    IOSurfaceDrawQuad::Orientation orientation = IOSurfaceDrawQuad::Unflipped;

    CREATE_SHARED_STATE();
    CREATE_QUAD_3(IOSurfaceDrawQuad, size, textureId, orientation);
    EXPECT_EQ(size, copyQuad->ioSurfaceSize());
    EXPECT_EQ(textureId, copyQuad->ioSurfaceTextureId());
    EXPECT_EQ(orientation, copyQuad->orientation());
}

TEST(DrawQuadTest, copyRenderPassDrawQuad)
{
    RenderPass::Id renderPassId(22, 64);
    bool isReplica = true;
    ResourceProvider::ResourceId maskResourceId = 78;
    gfx::Rect contentsChangedSinceLastFrame(42, 11, 74, 24);
    float maskTexCoordScaleX = 33;
    float maskTexCoordScaleY = 19;
    float maskTexCoordOffsetX = -45;
    float maskTexCoordOffsetY = -21;

    RenderPass::Id copiedRenderPassId(235, 11);

    CREATE_SHARED_STATE();
    CREATE_QUAD_8_1(RenderPassDrawQuad, renderPassId, isReplica, maskResourceId, contentsChangedSinceLastFrame, maskTexCoordScaleX, maskTexCoordScaleY, maskTexCoordOffsetX, maskTexCoordOffsetY, copiedRenderPassId);
    EXPECT_EQ(copiedRenderPassId, copyQuad->renderPassId());
    EXPECT_EQ(isReplica, copyQuad->isReplica());
    EXPECT_EQ(maskResourceId, copyQuad->maskResourceId());
    EXPECT_RECT_EQ(contentsChangedSinceLastFrame, copyQuad->contentsChangedSinceLastFrame());
    EXPECT_EQ(maskTexCoordScaleX, copyQuad->maskTexCoordScaleX());
    EXPECT_EQ(maskTexCoordScaleY, copyQuad->maskTexCoordScaleY());
    EXPECT_EQ(maskTexCoordOffsetX, copyQuad->maskTexCoordOffsetX());
    EXPECT_EQ(maskTexCoordOffsetY, copyQuad->maskTexCoordOffsetY());
}

TEST(DrawQuadTest, copySolidColorDrawQuad)
{
    SkColor color = 0x49494949;

    CREATE_SHARED_STATE();
    CREATE_QUAD_1(SolidColorDrawQuad, color);
    EXPECT_EQ(color, copyQuad->color());
}

TEST(DrawQuadTest, copyStreamVideoDrawQuad)
{
    unsigned textureId = 64;
    WebTransformationMatrix matrix(0.5, 1, 0.25, 0.75, 0, 1);

    CREATE_SHARED_STATE();
    CREATE_QUAD_2(StreamVideoDrawQuad, textureId, matrix);
    EXPECT_EQ(textureId, copyQuad->textureId());
    EXPECT_EQ(matrix, copyQuad->matrix());
}

TEST(DrawQuadTest, copyTextureDrawQuad)
{
    unsigned resourceId = 82;
    bool premultipliedAlpha = true;
    gfx::RectF uvRect(0.5, 224, -51, 36);
    bool flipped = true;

    CREATE_SHARED_STATE();
    CREATE_QUAD_4(TextureDrawQuad, resourceId, premultipliedAlpha, uvRect, flipped);
    EXPECT_EQ(resourceId, copyQuad->resourceId());
    EXPECT_EQ(premultipliedAlpha, copyQuad->premultipliedAlpha());
    EXPECT_FLOAT_RECT_EQ(uvRect, copyQuad->uvRect());
    EXPECT_EQ(flipped, copyQuad->flipped());
}

TEST(DrawQuadTest, copyTileDrawQuad)
{
    gfx::Rect opaqueRect(33, 44, 22, 33);
    unsigned resourceId = 104;
    gfx::Point textureOffset(-31, 47);
    gfx::Size textureSize(85, 32);
    GLint textureFilter = 82;
    bool swizzleContents = true;
    bool leftEdgeAA = true;
    bool topEdgeAA = true;
    bool rightEdgeAA = false;
    bool bottomEdgeAA = true;

    CREATE_SHARED_STATE();
    CREATE_QUAD_10(TileDrawQuad, opaqueRect, resourceId, textureOffset, textureSize, textureFilter, swizzleContents, leftEdgeAA, topEdgeAA, rightEdgeAA, bottomEdgeAA);
    EXPECT_RECT_EQ(opaqueRect, copyQuad->opaqueRect());
    EXPECT_EQ(resourceId, copyQuad->resourceId());
    EXPECT_EQ(textureOffset, copyQuad->textureOffset());
    EXPECT_EQ(textureSize, copyQuad->textureSize());
    EXPECT_EQ(textureFilter, copyQuad->textureFilter());
    EXPECT_EQ(swizzleContents, copyQuad->swizzleContents());
    EXPECT_EQ(leftEdgeAA, copyQuad->leftEdgeAA());
    EXPECT_EQ(topEdgeAA, copyQuad->topEdgeAA());
    EXPECT_EQ(rightEdgeAA, copyQuad->rightEdgeAA());
    EXPECT_EQ(bottomEdgeAA, copyQuad->bottomEdgeAA());
}

TEST(DrawQuadTest, copyYUVVideoDrawQuad)
{
    VideoLayerImpl::FramePlane yPlane;
    yPlane.resourceId = 45;
    yPlane.size = IntSize(34, 23);
    yPlane.format = 8;
    yPlane.visibleSize = IntSize(623, 235);
    VideoLayerImpl::FramePlane uPlane;
    uPlane.resourceId = 532;
    uPlane.size = IntSize(134, 16);
    uPlane.format = 2;
    uPlane.visibleSize = IntSize(126, 27);
    VideoLayerImpl::FramePlane vPlane;
    vPlane.resourceId = 4;
    vPlane.size = IntSize(456, 486);
    vPlane.format = 46;
    vPlane.visibleSize = IntSize(19, 45);

    CREATE_SHARED_STATE();
    CREATE_QUAD_3(YUVVideoDrawQuad, yPlane, uPlane, vPlane);
    EXPECT_EQ(yPlane.resourceId, copyQuad->yPlane().resourceId);
    EXPECT_EQ(yPlane.size, copyQuad->yPlane().size);
    EXPECT_EQ(yPlane.format, copyQuad->yPlane().format);
    EXPECT_EQ(yPlane.visibleSize, copyQuad->yPlane().visibleSize);
    EXPECT_EQ(uPlane.resourceId, copyQuad->uPlane().resourceId);
    EXPECT_EQ(uPlane.size, copyQuad->uPlane().size);
    EXPECT_EQ(uPlane.format, copyQuad->uPlane().format);
    EXPECT_EQ(uPlane.visibleSize, copyQuad->uPlane().visibleSize);
    EXPECT_EQ(vPlane.resourceId, copyQuad->vPlane().resourceId);
    EXPECT_EQ(vPlane.size, copyQuad->vPlane().size);
    EXPECT_EQ(vPlane.format, copyQuad->vPlane().format);
    EXPECT_EQ(vPlane.visibleSize, copyQuad->vPlane().visibleSize);
}

} // namespace
