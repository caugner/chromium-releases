// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/video_layer_impl.h"

#include "NotImplemented.h"
#include "cc/io_surface_draw_quad.h"
#include "cc/layer_tree_host_impl.h"
#include "cc/proxy.h"
#include "cc/quad_sink.h"
#include "cc/resource_provider.h"
#include "cc/stream_video_draw_quad.h"
#include "cc/texture_draw_quad.h"
#include "cc/yuv_video_draw_quad.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"
#include <public/WebVideoFrame.h>

namespace cc {

VideoLayerImpl::VideoLayerImpl(int id, WebKit::WebVideoFrameProvider* provider)
    : LayerImpl(id)
    , m_provider(provider)
    , m_frame(0)
    , m_externalTextureResource(0)
{
    // This matrix is the default transformation for stream textures, and flips on the Y axis.
    m_streamTextureMatrix = WebKit::WebTransformationMatrix(
        1, 0, 0, 0,
        0, -1, 0, 0,
        0, 0, 1, 0,
        0, 1, 0, 1);

    // This only happens during a commit on the compositor thread while the main
    // thread is blocked. That makes this a thread-safe call to set the video
    // frame provider client that does not require a lock. The same is true of
    // the call in the destructor.
    DCHECK(Proxy::isMainThreadBlocked());
    m_provider->setVideoFrameProviderClient(this);
}

VideoLayerImpl::~VideoLayerImpl()
{
    // See comment in constructor for why this doesn't need a lock.
    DCHECK(Proxy::isMainThreadBlocked());
    if (m_provider) {
        m_provider->setVideoFrameProviderClient(0);
        m_provider = 0;
    }
    freePlaneData(layerTreeHostImpl()->resourceProvider());

#ifndef NDEBUG
    for (unsigned i = 0; i < WebKit::WebVideoFrame::maxPlanes; ++i)
      DCHECK(!m_framePlanes[i].resourceId);
    DCHECK(!m_externalTextureResource);
#endif
}

void VideoLayerImpl::stopUsingProvider()
{
    // Block the provider from shutting down until this client is done
    // using the frame.
    base::AutoLock locker(m_providerLock);
    DCHECK(!m_frame);
    m_provider = 0;
}

// Convert WebKit::WebVideoFrame::Format to GraphicsContext3D's format enum values.
static GLenum convertVFCFormatToGC3DFormat(const WebKit::WebVideoFrame& frame)
{
    switch (frame.format()) {
    case WebKit::WebVideoFrame::FormatYV12:
    case WebKit::WebVideoFrame::FormatYV16:
        return GL_LUMINANCE;
    case WebKit::WebVideoFrame::FormatNativeTexture:
        return frame.textureTarget();
    case WebKit::WebVideoFrame::FormatInvalid:
    case WebKit::WebVideoFrame::FormatRGB32:
    case WebKit::WebVideoFrame::FormatEmpty:
    case WebKit::WebVideoFrame::FormatI420:
        notImplemented();
    }
    return GL_INVALID_VALUE;
}

void VideoLayerImpl::willDraw(ResourceProvider* resourceProvider)
{
    DCHECK(Proxy::isImplThread());
    LayerImpl::willDraw(resourceProvider);

    // Explicitly acquire and release the provider mutex so it can be held from
    // willDraw to didDraw. Since the compositor thread is in the middle of
    // drawing, the layer will not be destroyed before didDraw is called.
    // Therefore, the only thing that will prevent this lock from being released
    // is the GPU process locking it. As the GPU process can't cause the
    // destruction of the provider (calling stopUsingProvider), holding this
    // lock should not cause a deadlock.
    m_providerLock.Acquire();

    willDrawInternal(resourceProvider);
    freeUnusedPlaneData(resourceProvider);

    if (!m_frame)
        m_providerLock.Release();
}

void VideoLayerImpl::willDrawInternal(ResourceProvider* resourceProvider)
{
    DCHECK(Proxy::isImplThread());
    DCHECK(!m_externalTextureResource);

    if (!m_provider) {
        m_frame = 0;
        return;
    }

    m_frame = m_provider->getCurrentFrame();

    if (!m_frame)
        return;

    m_format = convertVFCFormatToGC3DFormat(*m_frame);

    if (m_format == GL_INVALID_VALUE) {
        m_provider->putCurrentFrame(m_frame);
        m_frame = 0;
        return;
    }

    if (m_frame->planes() > WebKit::WebVideoFrame::maxPlanes) {
        m_provider->putCurrentFrame(m_frame);
        m_frame = 0;
        return;
    }

    if (!allocatePlaneData(resourceProvider)) {
        m_provider->putCurrentFrame(m_frame);
        m_frame = 0;
        return;
    }

    if (!copyPlaneData(resourceProvider)) {
        m_provider->putCurrentFrame(m_frame);
        m_frame = 0;
        return;
    }

    if (m_format == GL_TEXTURE_2D)
        m_externalTextureResource = resourceProvider->createResourceFromExternalTexture(m_frame->textureId());
}

void VideoLayerImpl::appendQuads(QuadSink& quadSink, AppendQuadsData& appendQuadsData)
{
    DCHECK(Proxy::isImplThread());

    if (!m_frame)
        return;

    SharedQuadState* sharedQuadState = quadSink.useSharedQuadState(createSharedQuadState());
    appendDebugBorderQuad(quadSink, sharedQuadState, appendQuadsData);

    // FIXME: When we pass quads out of process, we need to double-buffer, or
    // otherwise synchonize use of all textures in the quad.

    IntRect quadRect(IntPoint(), contentBounds());

    switch (m_format) {
    case GL_LUMINANCE: {
        // YUV software decoder.
        const FramePlane& yPlane = m_framePlanes[WebKit::WebVideoFrame::yPlane];
        const FramePlane& uPlane = m_framePlanes[WebKit::WebVideoFrame::uPlane];
        const FramePlane& vPlane = m_framePlanes[WebKit::WebVideoFrame::vPlane];
        scoped_ptr<YUVVideoDrawQuad> yuvVideoQuad = YUVVideoDrawQuad::create(sharedQuadState, quadRect, yPlane, uPlane, vPlane);
        quadSink.append(yuvVideoQuad.PassAs<DrawQuad>(), appendQuadsData);
        break;
    }
    case GL_RGBA: {
        // RGBA software decoder.
        const FramePlane& plane = m_framePlanes[WebKit::WebVideoFrame::rgbPlane];
        float widthScaleFactor = static_cast<float>(plane.visibleSize.width()) / plane.size.width();

        bool premultipliedAlpha = true;
        FloatRect uvRect(0, 0, widthScaleFactor, 1);
        bool flipped = false;
        scoped_ptr<TextureDrawQuad> textureQuad = TextureDrawQuad::create(sharedQuadState, quadRect, plane.resourceId, premultipliedAlpha, uvRect, flipped);
        quadSink.append(textureQuad.PassAs<DrawQuad>(), appendQuadsData);
        break;
    }
    case GL_TEXTURE_2D: {
        // NativeTexture hardware decoder.
        bool premultipliedAlpha = true;
        FloatRect uvRect(0, 0, 1, 1);
        bool flipped = false;
        scoped_ptr<TextureDrawQuad> textureQuad = TextureDrawQuad::create(sharedQuadState, quadRect, m_externalTextureResource, premultipliedAlpha, uvRect, flipped);
        quadSink.append(textureQuad.PassAs<DrawQuad>(), appendQuadsData);
        break;
    }
    case GL_TEXTURE_RECTANGLE_ARB: {
        IntSize textureSize(m_frame->width(), m_frame->height()); 
        scoped_ptr<IOSurfaceDrawQuad> ioSurfaceQuad = IOSurfaceDrawQuad::create(sharedQuadState, quadRect, textureSize, m_frame->textureId(), IOSurfaceDrawQuad::Unflipped);
        quadSink.append(ioSurfaceQuad.PassAs<DrawQuad>(), appendQuadsData);
        break;
    }
    case GL_TEXTURE_EXTERNAL_OES: {
        // StreamTexture hardware decoder.
        scoped_ptr<StreamVideoDrawQuad> streamVideoQuad = StreamVideoDrawQuad::create(sharedQuadState, quadRect, m_frame->textureId(), m_streamTextureMatrix);
        quadSink.append(streamVideoQuad.PassAs<DrawQuad>(), appendQuadsData);
        break;
    }
    default:
        CRASH(); // Someone updated convertVFCFormatToGC3DFormat above but update this!
    }
}

void VideoLayerImpl::didDraw(ResourceProvider* resourceProvider)
{
    DCHECK(Proxy::isImplThread());
    LayerImpl::didDraw(resourceProvider);

    if (!m_frame)
        return;

    if (m_format == GL_TEXTURE_2D) {
        DCHECK(m_externalTextureResource);
        // FIXME: the following assert will not be true when sending resources to a
        // parent compositor. We will probably need to hold on to m_frame for
        // longer, and have several "current frames" in the pipeline.
        DCHECK(!resourceProvider->inUseByConsumer(m_externalTextureResource));
        resourceProvider->deleteResource(m_externalTextureResource);
        m_externalTextureResource = 0;
    }

    m_provider->putCurrentFrame(m_frame);
    m_frame = 0;

    m_providerLock.Release();
}

static int videoFrameDimension(int originalDimension, unsigned plane, int format)
{
    if (format == WebKit::WebVideoFrame::FormatYV12 && plane != WebKit::WebVideoFrame::yPlane)
        return originalDimension / 2;
    return originalDimension;
}

static bool hasPaddingBytes(const WebKit::WebVideoFrame& frame, unsigned plane)
{
    return frame.stride(plane) > videoFrameDimension(frame.width(), plane, frame.format());
}

IntSize VideoLayerImpl::computeVisibleSize(const WebKit::WebVideoFrame& frame, unsigned plane)
{
    int visibleWidth = videoFrameDimension(frame.width(), plane, frame.format());
    int originalWidth = visibleWidth;
    int visibleHeight = videoFrameDimension(frame.height(), plane, frame.format());

    // When there are dead pixels at the edge of the texture, decrease
    // the frame width by 1 to prevent the rightmost pixels from
    // interpolating with the dead pixels.
    if (hasPaddingBytes(frame, plane))
        --visibleWidth;

    // In YV12, every 2x2 square of Y values corresponds to one U and
    // one V value. If we decrease the width of the UV plane, we must decrease the
    // width of the Y texture by 2 for proper alignment. This must happen
    // always, even if Y's texture does not have padding bytes.
    if (plane == WebKit::WebVideoFrame::yPlane && frame.format() == WebKit::WebVideoFrame::FormatYV12) {
        if (hasPaddingBytes(frame, WebKit::WebVideoFrame::uPlane))
            visibleWidth = originalWidth - 2;
    }

    return IntSize(visibleWidth, visibleHeight);
}

bool VideoLayerImpl::FramePlane::allocateData(ResourceProvider* resourceProvider)
{
    if (resourceId)
        return true;

    resourceId = resourceProvider->createResource(Renderer::ImplPool, size, format, ResourceProvider::TextureUsageAny);
    return resourceId;
}

void VideoLayerImpl::FramePlane::freeData(ResourceProvider* resourceProvider)
{
    if (!resourceId)
        return;

    resourceProvider->deleteResource(resourceId);
    resourceId = 0;
}

bool VideoLayerImpl::allocatePlaneData(ResourceProvider* resourceProvider)
{
    int maxTextureSize = resourceProvider->maxTextureSize();
    for (unsigned planeIndex = 0; planeIndex < m_frame->planes(); ++planeIndex) {
        VideoLayerImpl::FramePlane& plane = m_framePlanes[planeIndex];

        IntSize requiredTextureSize(m_frame->stride(planeIndex), videoFrameDimension(m_frame->height(), planeIndex, m_frame->format()));
        // FIXME: Remove the test against maxTextureSize when tiled layers are implemented.
        if (requiredTextureSize.isZero() || requiredTextureSize.width() > maxTextureSize || requiredTextureSize.height() > maxTextureSize)
            return false;

        if (plane.size != requiredTextureSize || plane.format != m_format) {
            plane.freeData(resourceProvider);
            plane.size = requiredTextureSize;
            plane.format = m_format;
        }

        if (!plane.resourceId) {
            if (!plane.allocateData(resourceProvider))
                return false;
            plane.visibleSize = computeVisibleSize(*m_frame, planeIndex);
        }
    }
    return true;
}

bool VideoLayerImpl::copyPlaneData(ResourceProvider* resourceProvider)
{
    size_t softwarePlaneCount = m_frame->planes();
    if (!softwarePlaneCount)
        return true;

    for (size_t softwarePlaneIndex = 0; softwarePlaneIndex < softwarePlaneCount; ++softwarePlaneIndex) {
        VideoLayerImpl::FramePlane& plane = m_framePlanes[softwarePlaneIndex];
        const uint8_t* softwarePlanePixels = static_cast<const uint8_t*>(m_frame->data(softwarePlaneIndex));
        IntRect planeRect(IntPoint(), plane.size);
        resourceProvider->upload(plane.resourceId, softwarePlanePixels, planeRect, planeRect, IntSize());
    }
    return true;
}

void VideoLayerImpl::freePlaneData(ResourceProvider* resourceProvider)
{
    for (unsigned i = 0; i < WebKit::WebVideoFrame::maxPlanes; ++i)
        m_framePlanes[i].freeData(resourceProvider);
}

void VideoLayerImpl::freeUnusedPlaneData(ResourceProvider* resourceProvider)
{
    unsigned firstUnusedPlane = m_frame ? m_frame->planes() : 0;
    for (unsigned i = firstUnusedPlane; i < WebKit::WebVideoFrame::maxPlanes; ++i)
        m_framePlanes[i].freeData(resourceProvider);
}

void VideoLayerImpl::didReceiveFrame()
{
    setNeedsRedraw();
}

void VideoLayerImpl::didUpdateMatrix(const float matrix[16])
{
    m_streamTextureMatrix = WebKit::WebTransformationMatrix(
        matrix[0], matrix[1], matrix[2], matrix[3],
        matrix[4], matrix[5], matrix[6], matrix[7],
        matrix[8], matrix[9], matrix[10], matrix[11],
        matrix[12], matrix[13], matrix[14], matrix[15]);
    setNeedsRedraw();
}

void VideoLayerImpl::didLoseContext()
{
    freePlaneData(layerTreeHostImpl()->resourceProvider());
}

void VideoLayerImpl::setNeedsRedraw()
{
    layerTreeHostImpl()->setNeedsRedraw();
}

void VideoLayerImpl::dumpLayerProperties(std::string* str, int indent) const
{
    str->append(indentString(indent));
    str->append("video layer\n");
    LayerImpl::dumpLayerProperties(str, indent);
}

const char* VideoLayerImpl::layerTypeAsString() const
{
    return "VideoLayer";
}

}
