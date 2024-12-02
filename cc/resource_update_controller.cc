// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/resource_update_controller.h"

#include "base/debug/trace_event.h"
#include "cc/prioritized_texture.h"
#include "cc/proxy.h"
#include "cc/resource_provider.h"
#include "cc/texture_copier.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/skia/include/gpu/SkGpuDevice.h"
#include <limits>
#include <public/WebGraphicsContext3D.h>
#include <public/WebSharedGraphicsContext3D.h>

using WebKit::WebGraphicsContext3D;
using WebKit::WebSharedGraphicsContext3D;

namespace {

// Number of partial updates we allow.
const size_t partialTextureUpdatesMax = 12;

// Measured in seconds.
const double textureUpdateTickRate = 0.004;

// Measured in seconds.
const double uploaderBusyTickRate = 0.001;

// Flush interval when performing texture uploads.
const int textureUploadFlushPeriod = 4;

// Number of blocking update intervals to allow.
const size_t maxBlockingUpdateIntervals = 4;

scoped_ptr<SkCanvas> createAcceleratedCanvas(
    GrContext* grContext, cc::IntSize canvasSize, unsigned textureId)
{
    GrPlatformTextureDesc textureDesc;
    textureDesc.fFlags = kRenderTarget_GrPlatformTextureFlag;
    textureDesc.fWidth = canvasSize.width();
    textureDesc.fHeight = canvasSize.height();
    textureDesc.fConfig = kSkia8888_GrPixelConfig;
    textureDesc.fTextureHandle = textureId;
    SkAutoTUnref<GrTexture> target(
        grContext->createPlatformTexture(textureDesc));
    SkAutoTUnref<SkDevice> device(new SkGpuDevice(grContext, target.get()));
    return make_scoped_ptr(new SkCanvas(device.get()));
}

}  // namespace

namespace cc {

size_t ResourceUpdateController::maxPartialTextureUpdates()
{
    return partialTextureUpdatesMax;
}

size_t ResourceUpdateController::maxFullUpdatesPerTick(
    ResourceProvider* resourceProvider)
{
    double texturesPerSecond = resourceProvider->estimatedUploadsPerSecond();
    size_t texturesPerTick = floor(textureUpdateTickRate * texturesPerSecond);
    return texturesPerTick ? texturesPerTick : 1;
}

ResourceUpdateController::ResourceUpdateController(ResourceUpdateControllerClient* client, Thread* thread, scoped_ptr<ResourceUpdateQueue> queue, ResourceProvider* resourceProvider)
    : m_client(client)
    , m_timer(new Timer(thread, this))
    , m_queue(queue.Pass())
    , m_resourceProvider(resourceProvider)
    , m_textureUpdatesPerTick(maxFullUpdatesPerTick(resourceProvider))
    , m_firstUpdateAttempt(true)
{
}

ResourceUpdateController::~ResourceUpdateController()
{
}

void ResourceUpdateController::performMoreUpdates(
    base::TimeTicks timeLimit)
{
    m_timeLimit = timeLimit;

    // Update already in progress.
    if (m_timer->isActive())
        return;

    // Call updateMoreTexturesNow() directly unless it's the first update
    // attempt. This ensures that we empty the update queue in a finite
    // amount of time.
    if (m_firstUpdateAttempt) {
        // Post a 0-delay task when no updates were left. When it runs,
        // readyToFinalizeTextureUpdates() will be called.
        if (!updateMoreTexturesIfEnoughTimeRemaining())
            m_timer->startOneShot(0);

        m_firstUpdateAttempt = false;
    } else
        updateMoreTexturesNow();
}

void ResourceUpdateController::discardUploadsToEvictedResources()
{
    m_queue->clearUploadsToEvictedResources();
}

void ResourceUpdateController::updateTexture(ResourceUpdate update)
{
    if (update.picture) {
        PrioritizedTexture* texture = update.texture;
        IntRect pictureRect = update.content_rect;
        IntRect sourceRect = update.source_rect;
        IntSize destOffset = update.dest_offset;

        texture->acquireBackingTexture(m_resourceProvider);
        DCHECK(texture->haveBackingTexture());

        DCHECK(m_resourceProvider->resourceType(texture->resourceId()) ==
               ResourceProvider::GLTexture);

        WebGraphicsContext3D* paintContext = Proxy::hasImplThread() ?
            WebSharedGraphicsContext3D::compositorThreadContext() :
            WebSharedGraphicsContext3D::mainThreadContext();
        GrContext* paintGrContext = Proxy::hasImplThread() ?
            WebSharedGraphicsContext3D::compositorThreadGrContext() :
            WebSharedGraphicsContext3D::mainThreadGrContext();

        // Flush the context in which the backing texture is created so that it
        // is available in other shared contexts. It is important to do here
        // because the backing texture is created in one context while it is
        // being written to in another.
        m_resourceProvider->flush();
        ResourceProvider::ScopedWriteLockGL lock(
            m_resourceProvider, texture->resourceId());

        // Make sure ganesh uses the correct GL context.
        paintContext->makeContextCurrent();

        // Create an accelerated canvas to draw on.
        scoped_ptr<SkCanvas> canvas = createAcceleratedCanvas(
            paintGrContext, texture->size(), lock.textureId());

        // The compositor expects the textures to be upside-down so it can flip
        // the final composited image. Ganesh renders the image upright so we
        // need to do a y-flip.
        canvas->translate(0.0, texture->size().height());
        canvas->scale(1.0, -1.0);
        // Clip to the destination on the texture that must be updated.
        canvas->clipRect(SkRect::MakeXYWH(destOffset.width(),
                                          destOffset.height(),
                                          sourceRect.width(),
                                          sourceRect.height()));
        // Translate the origin of pictureRect to destOffset.
        // Note that destOffset is defined relative to sourceRect.
        canvas->translate(
            pictureRect.x() - sourceRect.x() + destOffset.width(),
            pictureRect.y() - sourceRect.y() + destOffset.height());
        canvas->drawPicture(*update.picture);

        // Flush ganesh context so that all the rendered stuff appears on the
        // texture.
        paintGrContext->flush();

        // Flush the GL context so rendering results from this context are
        // visible in the compositor's context.
        paintContext->flush();
    }

    if (update.bitmap) {
        update.bitmap->lockPixels();
        update.texture->upload(
            m_resourceProvider,
            static_cast<const uint8_t*>(update.bitmap->getPixels()),
            update.content_rect,
            update.source_rect,
            update.dest_offset);
        update.bitmap->unlockPixels();
    }
}

void ResourceUpdateController::finalize()
{
    size_t uploadCount = 0;
    while (m_queue->fullUploadSize()) {
        if (!(uploadCount % textureUploadFlushPeriod) && uploadCount)
            m_resourceProvider->shallowFlushIfSupported();

        updateTexture(m_queue->takeFirstFullUpload());
        uploadCount++;
    }

    while (m_queue->partialUploadSize()) {
        if (!(uploadCount % textureUploadFlushPeriod) && uploadCount)
            m_resourceProvider->shallowFlushIfSupported();

        updateTexture(m_queue->takeFirstPartialUpload());
        uploadCount++;
    }

    if (uploadCount)
        m_resourceProvider->shallowFlushIfSupported();

    if (m_queue->copySize()) {
        TextureCopier* copier = m_resourceProvider->textureCopier();
        while (m_queue->copySize())
            copier->copyTexture(m_queue->takeFirstCopy());

        // If we've performed any texture copies, we need to insert a flush
        // here into the compositor context before letting the main thread
        // proceed as it may make draw calls to the source texture of one of
        // our copy operations.
        copier->flush();
    }
}

void ResourceUpdateController::onTimerFired()
{
    ResourceProvider::debugNotifyEnterZone(0xB000000);
    if (!updateMoreTexturesIfEnoughTimeRemaining())
        m_client->readyToFinalizeTextureUpdates();
    ResourceProvider::debugNotifyLeaveZone();
}

base::TimeTicks ResourceUpdateController::now() const
{
    return base::TimeTicks::Now();
}

base::TimeDelta ResourceUpdateController::updateMoreTexturesTime() const
{
    return base::TimeDelta::FromMilliseconds(textureUpdateTickRate * 1000);
}

size_t ResourceUpdateController::updateMoreTexturesSize() const
{
    return m_textureUpdatesPerTick;
}

size_t ResourceUpdateController::maxBlockingUpdates() const
{
    return updateMoreTexturesSize() * maxBlockingUpdateIntervals;
}

bool ResourceUpdateController::updateMoreTexturesIfEnoughTimeRemaining()
{
    // Blocking uploads will increase when we're too aggressive in our upload
    // time estimate. We use a different timeout here to prevent unnecessary
    // amounts of idle time when blocking uploads have reached the max.
    if (m_resourceProvider->numBlockingUploads() >= maxBlockingUpdates()) {
        m_timer->startOneShot(uploaderBusyTickRate);
        return true;
    }

    if (!m_queue->fullUploadSize())
        return false;

    bool hasTimeRemaining = m_timeLimit.is_null() ||
        this->now() < m_timeLimit - updateMoreTexturesTime();
    if (hasTimeRemaining)
        updateMoreTexturesNow();

    return true;
}

void ResourceUpdateController::updateMoreTexturesNow()
{
    size_t uploads = std::min(
        m_queue->fullUploadSize(), updateMoreTexturesSize());
    m_timer->startOneShot(
        updateMoreTexturesTime().InSecondsF() / updateMoreTexturesSize() *
        uploads);

    if (!uploads)
        return;

    size_t uploadCount = 0;
    while (m_queue->fullUploadSize() && uploadCount < uploads) {
        if (!(uploadCount % textureUploadFlushPeriod) && uploadCount)
            m_resourceProvider->shallowFlushIfSupported();
        updateTexture(m_queue->takeFirstFullUpload());
        uploadCount++;
    }
    m_resourceProvider->shallowFlushIfSupported();
}

}  // namespace cc
