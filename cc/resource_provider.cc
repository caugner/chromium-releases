// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/resource_provider.h"

#include <limits.h>

#include "IntRect.h"
#include "base/debug/alias.h"
#include "base/hash_tables.h"
#include "base/stl_util.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "cc/gl_renderer.h" // For the GLC() macro.
#include "cc/proxy.h"
#include "cc/texture_uploader.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"

#include <public/WebGraphicsContext3D.h>

using WebKit::WebGraphicsContext3D;

namespace {
   // Temporary variables for debugging crashes in issue 151428 in canary.
   // Do not use these!
   const int g_debugMaxResourcesTracked = 64;
   unsigned int g_debugZone = 0;
   int64 g_debugResDestroyedCount = 0;
   cc::ResourceProvider::ResourceId g_debugResDestroyed[g_debugMaxResourcesTracked] = { 0 };
}

namespace cc {

static GLenum textureToStorageFormat(GLenum textureFormat)
{
    GLenum storageFormat = GL_RGBA8_OES;
    switch (textureFormat) {
    case GL_RGBA:
        break;
    case GL_BGRA_EXT:
        storageFormat = GL_BGRA8_EXT;
        break;
    default:
        NOTREACHED();
        break;
    }

    return storageFormat;
}

static bool isTextureFormatSupportedForStorage(GLenum format)
{
    return (format == GL_RGBA || format == GL_BGRA_EXT);
}

ResourceProvider::TransferableResourceList::TransferableResourceList()
{
}

ResourceProvider::TransferableResourceList::~TransferableResourceList()
{
}

ResourceProvider::Resource::Resource()
    : glId(0)
    , pixels(0)
    , pool(0)
    , lockForReadCount(0)
    , lockedForWrite(false)
    , external(false)
    , exported(false)
    , markedForDeletion(false)
    , size()
    , format(0)
    , type(static_cast<ResourceType>(0))
{
}

ResourceProvider::Resource::Resource(unsigned textureId, int pool, const IntSize& size, GLenum format)
    : glId(textureId)
    , pixels(0)
    , pool(pool)
    , lockForReadCount(0)
    , lockedForWrite(false)
    , external(false)
    , exported(false)
    , markedForDeletion(false)
    , size(size)
    , format(format)
    , type(GLTexture)
{
}

ResourceProvider::Resource::Resource(uint8_t* pixels, int pool, const IntSize& size, GLenum format)
    : glId(0)
    , pixels(pixels)
    , pool(pool)
    , lockForReadCount(0)
    , lockedForWrite(false)
    , external(false)
    , exported(false)
    , markedForDeletion(false)
    , size(size)
    , format(format)
    , type(Bitmap)
{
}

ResourceProvider::Child::Child()
{
}

ResourceProvider::Child::~Child()
{
}

scoped_ptr<ResourceProvider> ResourceProvider::create(GraphicsContext* context)
{
    scoped_ptr<ResourceProvider> resourceProvider(new ResourceProvider(context));
    if (!resourceProvider->initialize())
        return scoped_ptr<ResourceProvider>();
    return resourceProvider.Pass();
}

ResourceProvider::~ResourceProvider()
{
    WebGraphicsContext3D* context3d = m_context->context3D();
    if (!context3d || !context3d->makeContextCurrent())
        return;
    m_textureUploader.reset();
    m_textureCopier.reset();
}

WebGraphicsContext3D* ResourceProvider::graphicsContext3D()
{
    DCHECK(Proxy::isImplThread());
    return m_context->context3D();
}

bool ResourceProvider::inUseByConsumer(ResourceId id)
{
    DCHECK(Proxy::isImplThread());
    ResourceMap::iterator it = m_resources.find(id);
    CHECK(it != m_resources.end());
    Resource* resource = &it->second;
    return !!resource->lockForReadCount || resource->exported;
}

ResourceProvider::ResourceId ResourceProvider::createResource(int pool, const IntSize& size, GLenum format, TextureUsageHint hint)
{
    switch (m_defaultResourceType) {
    case GLTexture:
        return createGLTexture(pool, size, format, hint);
    case Bitmap:
        DCHECK(format == GL_RGBA);
        return createBitmap(pool, size);
    }

    CRASH();
    return 0;
}

ResourceProvider::ResourceId ResourceProvider::createGLTexture(int pool, const IntSize& size, GLenum format, TextureUsageHint hint)
{
    DCHECK(Proxy::isImplThread());
    unsigned textureId = 0;
    WebGraphicsContext3D* context3d = m_context->context3D();
    DCHECK(context3d);
    GLC(context3d, textureId = context3d->createTexture());
    GLC(context3d, context3d->bindTexture(GL_TEXTURE_2D, textureId));
    GLC(context3d, context3d->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GLC(context3d, context3d->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GLC(context3d, context3d->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GLC(context3d, context3d->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    if (m_useTextureUsageHint && hint == TextureUsageFramebuffer)
        GLC(context3d, context3d->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_USAGE_ANGLE, GL_FRAMEBUFFER_ATTACHMENT_ANGLE));
    if (m_useTextureStorageExt && isTextureFormatSupportedForStorage(format)) {
        GLenum storageFormat = textureToStorageFormat(format);
        GLC(context3d, context3d->texStorage2DEXT(GL_TEXTURE_2D, 1, storageFormat, size.width(), size.height()));
    } else
        GLC(context3d, context3d->texImage2D(GL_TEXTURE_2D, 0, format, size.width(), size.height(), 0, format, GL_UNSIGNED_BYTE, 0));
    ResourceId id = m_nextId++;
    Resource resource(textureId, pool, size, format);
    m_resources[id] = resource;
    return id;
}

ResourceProvider::ResourceId ResourceProvider::createBitmap(int pool, const IntSize& size)
{
    DCHECK(Proxy::isImplThread());

    uint8_t* pixels = new uint8_t[size.width() * size.height() * 4];

    ResourceId id = m_nextId++;
    Resource resource(pixels, pool, size, GL_RGBA);
    m_resources[id] = resource;
    return id;
}

ResourceProvider::ResourceId ResourceProvider::createResourceFromExternalTexture(unsigned textureId)
{
    DCHECK(Proxy::isImplThread());
    DCHECK(m_context->context3D());
    ResourceId id = m_nextId++;
    Resource resource(textureId, 0, IntSize(), 0);
    resource.external = true;
    m_resources[id] = resource;
    return id;
}

void ResourceProvider::deleteResource(ResourceId id)
{
    DCHECK(Proxy::isImplThread());
    ResourceMap::iterator it = m_resources.find(id);
    CHECK(it != m_resources.end());
    Resource* resource = &it->second;
    DCHECK(!resource->lockedForWrite);
    DCHECK(!resource->lockForReadCount);
    DCHECK(!resource->markedForDeletion);

    if (resource->exported) {
        resource->markedForDeletion = true;
        return;
    } else
        deleteResourceInternal(it);
}

void ResourceProvider::deleteResourceInternal(ResourceMap::iterator it)
{
    Resource* resource = &it->second;
    if (resource->glId && !resource->external) {
        WebGraphicsContext3D* context3d = m_context->context3D();
        DCHECK(context3d);
        GLC(context3d, context3d->deleteTexture(resource->glId));
    }
    if (resource->pixels)
        delete resource->pixels;

    g_debugResDestroyed[g_debugResDestroyedCount % g_debugMaxResourcesTracked] = (*it).first | g_debugZone;
    m_resources.erase(it);
}

void ResourceProvider::deleteOwnedResources(int pool)
{
    DCHECK(Proxy::isImplThread());
    ResourceIdArray toDelete;
    for (ResourceMap::iterator it = m_resources.begin(); it != m_resources.end(); ++it) {
        if (it->second.pool == pool && !it->second.external && !it->second.markedForDeletion)
            toDelete.push_back(it->first);
    }
    for (ResourceIdArray::iterator it = toDelete.begin(); it != toDelete.end(); ++it)
        deleteResource(*it);
}

ResourceProvider::ResourceType ResourceProvider::resourceType(ResourceId id)
{
    ResourceMap::iterator it = m_resources.find(id);
    CHECK(it != m_resources.end());
    Resource* resource = &it->second;
    return resource->type;
}

void ResourceProvider::upload(ResourceId id, const uint8_t* image, const IntRect& imageRect, const IntRect& sourceRect, const IntSize& destOffset)
{
    DCHECK(Proxy::isImplThread());
    ResourceMap::iterator it = m_resources.find(id);
    CHECK(it != m_resources.end());
    Resource* resource = &it->second;
    DCHECK(!resource->lockedForWrite);
    DCHECK(!resource->lockForReadCount);
    DCHECK(!resource->external);
    DCHECK(!resource->exported);

    if (resource->glId) {
        WebGraphicsContext3D* context3d = m_context->context3D();
        DCHECK(context3d);
        DCHECK(m_textureUploader.get());
        context3d->bindTexture(GL_TEXTURE_2D, resource->glId);
        m_textureUploader->upload(image,
                                  imageRect,
                                  sourceRect,
                                  destOffset,
                                  resource->format,
                                  resource->size);
    }

    if (resource->pixels) {
        SkBitmap srcFull;
        srcFull.setConfig(SkBitmap::kARGB_8888_Config, imageRect.width(), imageRect.height());
        srcFull.setPixels(const_cast<uint8_t*>(image));
        SkBitmap srcSubset;
        SkIRect skSourceRect = SkIRect::MakeXYWH(sourceRect.x(), sourceRect.y(), sourceRect.width(), sourceRect.height());
        skSourceRect.offset(-imageRect.x(), -imageRect.y());
        srcFull.extractSubset(&srcSubset, skSourceRect);

        ScopedWriteLockSoftware lock(this, id);
        SkCanvas* dest = lock.skCanvas();
        dest->writePixels(srcSubset, destOffset.width(), destOffset.height());
    }
}

size_t ResourceProvider::numBlockingUploads()
{
    if (!m_textureUploader)
        return 0;

    return m_textureUploader->numBlockingUploads();
}

void ResourceProvider::markPendingUploadsAsNonBlocking()
{
    if (!m_textureUploader)
        return;

    m_textureUploader->markPendingUploadsAsNonBlocking();
}

double ResourceProvider::estimatedUploadsPerSecond()
{
    if (!m_textureUploader)
        return 0.0;

    return m_textureUploader->estimatedTexturesPerSecond();
}

void ResourceProvider::flush()
{
    DCHECK(Proxy::isImplThread());
    WebGraphicsContext3D* context3d = m_context->context3D();
    if (context3d)
        context3d->flush();
}

bool ResourceProvider::shallowFlushIfSupported()
{
    DCHECK(Proxy::isImplThread());
    WebGraphicsContext3D* context3d = m_context->context3D();
    if (!context3d || !m_useShallowFlush)
        return false;

    context3d->shallowFlushCHROMIUM();
    return true;
}

const ResourceProvider::Resource* ResourceProvider::lockForRead(ResourceId id)
{
    DCHECK(Proxy::isImplThread());
    ResourceMap::iterator it = m_resources.find(id);

    if (it == m_resources.end()) {
        int resourceCount = m_resources.size();
        int64 resDestroyedCount = g_debugResDestroyedCount;
        ResourceId resDestroyed[g_debugMaxResourcesTracked];
        for (int64 i = 0; i < g_debugMaxResourcesTracked; ++i)
            resDestroyed[i] = g_debugResDestroyed[i];
        ResourceId resToDestroy = id;

        base::debug::Alias(&resourceCount);
        base::debug::Alias(&resDestroyedCount);
        for (int64 i = 0; i < g_debugMaxResourcesTracked; ++i)
            base::debug::Alias(&resDestroyed[i]);
        base::debug::Alias(&resToDestroy);
        CHECK(it != m_resources.end());
    }

    Resource* resource = &it->second;
    DCHECK(!resource->lockedForWrite);
    DCHECK(!resource->exported);
    resource->lockForReadCount++;
    return resource;
}

void ResourceProvider::unlockForRead(ResourceId id)
{
    DCHECK(Proxy::isImplThread());
    ResourceMap::iterator it = m_resources.find(id);
    CHECK(it != m_resources.end());
    Resource* resource = &it->second;
    DCHECK(resource->lockForReadCount > 0);
    DCHECK(!resource->exported);
    resource->lockForReadCount--;
}

const ResourceProvider::Resource* ResourceProvider::lockForWrite(ResourceId id)
{
    DCHECK(Proxy::isImplThread());
    ResourceMap::iterator it = m_resources.find(id);
    CHECK(it != m_resources.end());
    Resource* resource = &it->second;
    DCHECK(!resource->lockedForWrite);
    DCHECK(!resource->lockForReadCount);
    DCHECK(!resource->exported);
    DCHECK(!resource->external);
    resource->lockedForWrite = true;
    return resource;
}

void ResourceProvider::unlockForWrite(ResourceId id)
{
    DCHECK(Proxy::isImplThread());
    ResourceMap::iterator it = m_resources.find(id);
    CHECK(it != m_resources.end());
    Resource* resource = &it->second;
    DCHECK(resource->lockedForWrite);
    DCHECK(!resource->exported);
    DCHECK(!resource->external);
    resource->lockedForWrite = false;
}

ResourceProvider::ScopedReadLockGL::ScopedReadLockGL(ResourceProvider* resourceProvider, ResourceProvider::ResourceId resourceId)
    : m_resourceProvider(resourceProvider)
    , m_resourceId(resourceId)
    , m_textureId(resourceProvider->lockForRead(resourceId)->glId)
{
    DCHECK(m_textureId);
}

ResourceProvider::ScopedReadLockGL::~ScopedReadLockGL()
{
    m_resourceProvider->unlockForRead(m_resourceId);
}

ResourceProvider::ScopedWriteLockGL::ScopedWriteLockGL(ResourceProvider* resourceProvider, ResourceProvider::ResourceId resourceId)
    : m_resourceProvider(resourceProvider)
    , m_resourceId(resourceId)
    , m_textureId(resourceProvider->lockForWrite(resourceId)->glId)
{
    DCHECK(m_textureId);
}

ResourceProvider::ScopedWriteLockGL::~ScopedWriteLockGL()
{
    m_resourceProvider->unlockForWrite(m_resourceId);
}

void ResourceProvider::populateSkBitmapWithResource(SkBitmap* skBitmap, const Resource* resource)
{
    DCHECK(resource->pixels);
    DCHECK(resource->format == GL_RGBA);
    skBitmap->setConfig(SkBitmap::kARGB_8888_Config, resource->size.width(), resource->size.height());
    skBitmap->setPixels(resource->pixels);
}

ResourceProvider::ScopedReadLockSoftware::ScopedReadLockSoftware(ResourceProvider* resourceProvider, ResourceProvider::ResourceId resourceId)
    : m_resourceProvider(resourceProvider)
    , m_resourceId(resourceId)
{
    ResourceProvider::populateSkBitmapWithResource(&m_skBitmap, resourceProvider->lockForRead(resourceId));
}

ResourceProvider::ScopedReadLockSoftware::~ScopedReadLockSoftware()
{
    m_resourceProvider->unlockForRead(m_resourceId);
}

ResourceProvider::ScopedWriteLockSoftware::ScopedWriteLockSoftware(ResourceProvider* resourceProvider, ResourceProvider::ResourceId resourceId)
    : m_resourceProvider(resourceProvider)
    , m_resourceId(resourceId)
{
    ResourceProvider::populateSkBitmapWithResource(&m_skBitmap, resourceProvider->lockForWrite(resourceId));
    m_skCanvas.reset(new SkCanvas(m_skBitmap));
}

ResourceProvider::ScopedWriteLockSoftware::~ScopedWriteLockSoftware()
{
    m_resourceProvider->unlockForWrite(m_resourceId);
}

ResourceProvider::ResourceProvider(GraphicsContext* context)
    : m_context(context)
    , m_nextId(1)
    , m_nextChild(1)
    , m_defaultResourceType(GLTexture)
    , m_useTextureStorageExt(false)
    , m_useTextureUsageHint(false)
    , m_useShallowFlush(false)
    , m_maxTextureSize(0)
{
}

bool ResourceProvider::initialize()
{
    DCHECK(Proxy::isImplThread());
    WebGraphicsContext3D* context3d = m_context->context3D();
    if (!context3d) {
        m_maxTextureSize = INT_MAX / 2;
        return true;
    }
    if (!context3d->makeContextCurrent())
        return false;

    std::string extensionsString = UTF16ToASCII(context3d->getString(GL_EXTENSIONS));
    std::vector<std::string> extensions;
    base::SplitString(extensionsString, ' ', &extensions);
    bool useMapSub = false;
    bool useBindUniform = false;
    for (size_t i = 0; i < extensions.size(); ++i) {
        if (extensions[i] == "GL_EXT_texture_storage")
            m_useTextureStorageExt = true;
        else if (extensions[i] == "GL_ANGLE_texture_usage")
            m_useTextureUsageHint = true;
        else if (extensions[i] == "GL_CHROMIUM_map_sub")
            useMapSub = true;
        else if (extensions[i] == "GL_CHROMIUM_shallow_flush")
            m_useShallowFlush = true;
        else if (extensions[i] == "GL_CHROMIUM_bind_uniform_location")
            useBindUniform = true;
    }

    m_textureCopier = AcceleratedTextureCopier::create(context3d, useBindUniform);

    m_textureUploader = TextureUploader::create(context3d, useMapSub);
    GLC(context3d, context3d->getIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize));
    return true;
}

int ResourceProvider::createChild(int pool)
{
    DCHECK(Proxy::isImplThread());
    Child childInfo;
    childInfo.pool = pool;
    int child = m_nextChild++;
    m_children[child] = childInfo;
    return child;
}

void ResourceProvider::destroyChild(int child)
{
    DCHECK(Proxy::isImplThread());
    ChildMap::iterator it = m_children.find(child);
    DCHECK(it != m_children.end());
    deleteOwnedResources(it->second.pool);
    m_children.erase(it);
    trimMailboxDeque();
}

const ResourceProvider::ResourceIdMap& ResourceProvider::getChildToParentMap(int child) const
{
    DCHECK(Proxy::isImplThread());
    ChildMap::const_iterator it = m_children.find(child);
    DCHECK(it != m_children.end());
    return it->second.childToParentMap;
}

ResourceProvider::TransferableResourceList ResourceProvider::prepareSendToParent(const ResourceIdArray& resources)
{
    DCHECK(Proxy::isImplThread());
    TransferableResourceList list;
    list.syncPoint = 0;
    WebGraphicsContext3D* context3d = m_context->context3D();
    if (!context3d || !context3d->makeContextCurrent()) {
        // FIXME: Implement this path for software compositing.
        return list;
    }
    for (ResourceIdArray::const_iterator it = resources.begin(); it != resources.end(); ++it) {
        TransferableResource resource;
        if (transferResource(context3d, *it, &resource)) {
            m_resources.find(*it)->second.exported = true;
            list.resources.push_back(resource);
        }
    }
    if (list.resources.size())
        list.syncPoint = context3d->insertSyncPoint();
    return list;
}

ResourceProvider::TransferableResourceList ResourceProvider::prepareSendToChild(int child, const ResourceIdArray& resources)
{
    DCHECK(Proxy::isImplThread());
    TransferableResourceList list;
    list.syncPoint = 0;
    WebGraphicsContext3D* context3d = m_context->context3D();
    if (!context3d || !context3d->makeContextCurrent()) {
        // FIXME: Implement this path for software compositing.
        return list;
    }
    Child& childInfo = m_children.find(child)->second;
    for (ResourceIdArray::const_iterator it = resources.begin(); it != resources.end(); ++it) {
        TransferableResource resource;
        if (!transferResource(context3d, *it, &resource))
            NOTREACHED();
        DCHECK(childInfo.parentToChildMap.find(*it) != childInfo.parentToChildMap.end());
        resource.id = childInfo.parentToChildMap[*it];
        childInfo.parentToChildMap.erase(*it);
        childInfo.childToParentMap.erase(resource.id);
        list.resources.push_back(resource);
        deleteResource(*it);
    }
    if (list.resources.size())
        list.syncPoint = context3d->insertSyncPoint();
    return list;
}

void ResourceProvider::receiveFromChild(int child, const TransferableResourceList& resources)
{
    DCHECK(Proxy::isImplThread());
    WebGraphicsContext3D* context3d = m_context->context3D();
    if (!context3d || !context3d->makeContextCurrent()) {
        // FIXME: Implement this path for software compositing.
        return;
    }
    if (resources.syncPoint) {
        // NOTE: If the parent is a browser and the child a renderer, the parent
        // is not supposed to have its context wait, because that could induce
        // deadlocks and/or security issues. The caller is responsible for
        // waiting asynchronously, and resetting syncPoint before calling this.
        // However if the parent is a renderer (e.g. browser tag), it may be ok
        // (and is simpler) to wait.
        GLC(context3d, context3d->waitSyncPoint(resources.syncPoint));
    }
    Child& childInfo = m_children.find(child)->second;
    for (TransferableResourceArray::const_iterator it = resources.resources.begin(); it != resources.resources.end(); ++it) {
        unsigned textureId;
        GLC(context3d, textureId = context3d->createTexture());
        GLC(context3d, context3d->bindTexture(GL_TEXTURE_2D, textureId));
        GLC(context3d, context3d->consumeTextureCHROMIUM(GL_TEXTURE_2D, it->mailbox.name));
        ResourceId id = m_nextId++;
        Resource resource(textureId, childInfo.pool, it->size, it->format);
        m_resources[id] = resource;
        m_mailboxes.push_back(it->mailbox);
        childInfo.parentToChildMap[id] = it->id;
        childInfo.childToParentMap[it->id] = id;
    }
}

void ResourceProvider::receiveFromParent(const TransferableResourceList& resources)
{
    DCHECK(Proxy::isImplThread());
    WebGraphicsContext3D* context3d = m_context->context3D();
    if (!context3d || !context3d->makeContextCurrent()) {
        // FIXME: Implement this path for software compositing.
        return;
    }
    if (resources.syncPoint)
        GLC(context3d, context3d->waitSyncPoint(resources.syncPoint));
    for (TransferableResourceArray::const_iterator it = resources.resources.begin(); it != resources.resources.end(); ++it) {
        ResourceMap::iterator mapIterator = m_resources.find(it->id);
        DCHECK(mapIterator != m_resources.end());
        Resource* resource = &mapIterator->second;
        DCHECK(resource->exported);
        resource->exported = false;
        GLC(context3d, context3d->bindTexture(GL_TEXTURE_2D, resource->glId));
        GLC(context3d, context3d->consumeTextureCHROMIUM(GL_TEXTURE_2D, it->mailbox.name));
        m_mailboxes.push_back(it->mailbox);
        if (resource->markedForDeletion)
            deleteResourceInternal(mapIterator);
    }
}

bool ResourceProvider::transferResource(WebGraphicsContext3D* context, ResourceId id, TransferableResource* resource)
{
    DCHECK(Proxy::isImplThread());
    ResourceMap::const_iterator it = m_resources.find(id);
    CHECK(it != m_resources.end());
    const Resource* source = &it->second;
    DCHECK(!source->lockedForWrite);
    DCHECK(!source->lockForReadCount);
    DCHECK(!source->external);
    if (source->exported)
        return false;
    resource->id = id;
    resource->format = source->format;
    resource->size = source->size;
    if (m_mailboxes.size()) {
        resource->mailbox = m_mailboxes.front();
        m_mailboxes.pop_front();
    }
    else
        GLC(context, context->genMailboxCHROMIUM(resource->mailbox.name));
    GLC(context, context->bindTexture(GL_TEXTURE_2D, source->glId));
    GLC(context, context->produceTextureCHROMIUM(GL_TEXTURE_2D, resource->mailbox.name));
    return true;
}

void ResourceProvider::trimMailboxDeque()
{
    // Trim the mailbox deque to the maximum number of resources we may need to
    // send.
    // If we have a parent, any non-external resource not already transfered is
    // eligible to be sent to the parent. Otherwise, all resources belonging to
    // a child might need to be sent back to the child.
    size_t maxMailboxCount = 0;
    if (m_context->capabilities().hasParentCompositor) {
        for (ResourceMap::iterator it = m_resources.begin(); it != m_resources.end(); ++it) {
            if (!it->second.exported && !it->second.external)
                ++maxMailboxCount;
        }
    } else {
        base::hash_set<int> childPoolSet;
        for (ChildMap::iterator it = m_children.begin(); it != m_children.end(); ++it)
            childPoolSet.insert(it->second.pool);
        for (ResourceMap::iterator it = m_resources.begin(); it != m_resources.end(); ++it) {
            if (ContainsKey(childPoolSet, it->second.pool))
                ++maxMailboxCount;
        }
    }
    while (m_mailboxes.size() > maxMailboxCount)
        m_mailboxes.pop_front();
}

void ResourceProvider::debugNotifyEnterZone(unsigned int zone)
{
    g_debugZone = zone;
}

void ResourceProvider::debugNotifyLeaveZone()
{
    g_debugZone = 0;
}


}
