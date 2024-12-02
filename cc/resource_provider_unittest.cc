// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/resource_provider.h"

#include "base/logging.h"
#include "cc/graphics_context.h"
#include "cc/scoped_ptr_deque.h"
#include "cc/scoped_ptr_hash_map.h"
#include "cc/single_thread_proxy.h" // For DebugScopedSetImplThread
#include "cc/test/compositor_fake_web_graphics_context_3d.h"
#include "cc/test/fake_web_compositor_output_surface.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"
#include <public/WebGraphicsContext3D.h>

using namespace cc;
using namespace WebKit;

namespace {

size_t textureSize(const IntSize& size, WGC3Denum format)
{
    unsigned int componentsPerPixel = 4;
    unsigned int bytesPerComponent = 1;
    return size.width() * size.height() * componentsPerPixel * bytesPerComponent;
}

struct Texture {
    Texture(const IntSize& size, WGC3Denum format)
        : size(size)
        , format(format)
        , data(new uint8_t[textureSize(size, format)])
    {
    }

    IntSize size;
    WGC3Denum format;
    scoped_array<uint8_t> data;
};

// Shared data between multiple ResourceProviderContext. This contains mailbox
// contents as well as information about sync points.
class ContextSharedData {
public:
    static scoped_ptr<ContextSharedData> create() { return make_scoped_ptr(new ContextSharedData()); }

    unsigned insertSyncPoint() { return m_nextSyncPoint++; }

    void genMailbox(WGC3Dbyte* mailbox)
    {
        memset(mailbox, 0, sizeof(WGC3Dbyte[64]));
        memcpy(mailbox, &m_nextMailBox, sizeof(m_nextMailBox));
        ++m_nextMailBox;
    }

    void produceTexture(const WGC3Dbyte* mailboxName, unsigned syncPoint, scoped_ptr<Texture> texture)
    {
        unsigned mailbox = 0;
        memcpy(&mailbox, mailboxName, sizeof(mailbox));
        ASSERT_TRUE(mailbox && mailbox < m_nextMailBox);
        m_textures.set(mailbox, texture.Pass());
        ASSERT_LT(m_syncPointForMailbox[mailbox], syncPoint);
        m_syncPointForMailbox[mailbox] = syncPoint;
    }

    scoped_ptr<Texture> consumeTexture(const WGC3Dbyte* mailboxName, unsigned syncPoint)
    {
        unsigned mailbox = 0;
        memcpy(&mailbox, mailboxName, sizeof(mailbox));
        DCHECK(mailbox && mailbox < m_nextMailBox);

        // If the latest sync point the context has waited on is before the sync
        // point for when the mailbox was set, pretend we never saw that
        // produceTexture.
        if (m_syncPointForMailbox[mailbox] < syncPoint)
            return scoped_ptr<Texture>();
        return m_textures.take(mailbox);
    }

private:
    ContextSharedData()
        : m_nextSyncPoint(1)
        , m_nextMailBox(1)
    { }

    unsigned m_nextSyncPoint;
    unsigned m_nextMailBox;
    typedef ScopedPtrHashMap<unsigned, Texture> TextureMap;
    TextureMap m_textures;
    base::hash_map<unsigned, unsigned> m_syncPointForMailbox;
};

class ResourceProviderContext : public CompositorFakeWebGraphicsContext3D {
public:
    static scoped_ptr<ResourceProviderContext> create(ContextSharedData* sharedData) { return make_scoped_ptr(new ResourceProviderContext(Attributes(), sharedData)); }

    virtual unsigned insertSyncPoint()
    {
        unsigned syncPoint = m_sharedData->insertSyncPoint();
        // Commit the produceTextureCHROMIUM calls at this point, so that
        // they're associated with the sync point.
        for (PendingProduceTextureList::iterator it = m_pendingProduceTextures.begin(); it != m_pendingProduceTextures.end(); ++it)
            m_sharedData->produceTexture((*it)->mailbox, syncPoint, (*it)->texture.Pass());
        m_pendingProduceTextures.clear();
        return syncPoint;
    }

    virtual void waitSyncPoint(unsigned syncPoint)
    {
        m_lastWaitedSyncPoint = std::max(syncPoint, m_lastWaitedSyncPoint);
    }

    virtual void bindTexture(WGC3Denum target, WebGLId texture)
    {
      ASSERT_EQ(target, GL_TEXTURE_2D);
      ASSERT_TRUE(!texture || m_textures.find(texture) != m_textures.end());
      m_currentTexture = texture;
    }

    virtual WebGLId createTexture()
    {
        WebGLId id = CompositorFakeWebGraphicsContext3D::createTexture();
        m_textures.add(id, scoped_ptr<Texture>());
        return id;
    }

    virtual void deleteTexture(WebGLId id)
    {
        TextureMap::iterator it = m_textures.find(id);
        ASSERT_FALSE(it == m_textures.end());
        m_textures.erase(it);
        if (m_currentTexture == id)
            m_currentTexture = 0;
    }

    virtual void texStorage2DEXT(WGC3Denum target, WGC3Dint levels, WGC3Duint internalformat,
                                 WGC3Dint width, WGC3Dint height)
    {
        ASSERT_TRUE(m_currentTexture);
        ASSERT_EQ(target, GL_TEXTURE_2D);
        ASSERT_EQ(levels, 1);
        WGC3Denum format = GL_RGBA;
        switch (internalformat) {
        case GL_RGBA8_OES:
            break;
        case GL_BGRA8_EXT:
            format = GL_BGRA_EXT;
            break;
        default:
            NOTREACHED();
        }
        allocateTexture(IntSize(width, height), format);
    }

    virtual void texImage2D(WGC3Denum target, WGC3Dint level, WGC3Denum internalformat, WGC3Dsizei width, WGC3Dsizei height, WGC3Dint border, WGC3Denum format, WGC3Denum type, const void* pixels)
    {
        ASSERT_TRUE(m_currentTexture);
        ASSERT_EQ(target, GL_TEXTURE_2D);
        ASSERT_FALSE(level);
        ASSERT_EQ(internalformat, format);
        ASSERT_FALSE(border);
        ASSERT_EQ(type, GL_UNSIGNED_BYTE);
        allocateTexture(IntSize(width, height), format);
        if (pixels)
            setPixels(0, 0, width, height, pixels);
    }

    virtual void texSubImage2D(WGC3Denum target, WGC3Dint level, WGC3Dint xoffset, WGC3Dint yoffset, WGC3Dsizei width, WGC3Dsizei height, WGC3Denum format, WGC3Denum type, const void* pixels)
    {
        ASSERT_TRUE(m_currentTexture);
        ASSERT_EQ(target, GL_TEXTURE_2D);
        ASSERT_FALSE(level);
        ASSERT_TRUE(m_textures.get(m_currentTexture));
        ASSERT_EQ(m_textures.get(m_currentTexture)->format, format);
        ASSERT_EQ(type, GL_UNSIGNED_BYTE);
        ASSERT_TRUE(pixels);
        setPixels(xoffset, yoffset, width, height, pixels);
    }

    virtual void genMailboxCHROMIUM(WGC3Dbyte* mailbox) { return m_sharedData->genMailbox(mailbox); }
    virtual void produceTextureCHROMIUM(WGC3Denum target, const WGC3Dbyte* mailbox)
    {
        ASSERT_TRUE(m_currentTexture);
        ASSERT_EQ(target, GL_TEXTURE_2D);

        // Delay moving the texture into the mailbox until the next
        // insertSyncPoint, so that it is not visible to other contexts that
        // haven't waited on that sync point.
        scoped_ptr<PendingProduceTexture> pending(new PendingProduceTexture);
        memcpy(pending->mailbox, mailbox, sizeof(pending->mailbox));
        pending->texture = m_textures.take(m_currentTexture);
        m_textures.set(m_currentTexture, scoped_ptr<Texture>());
        m_pendingProduceTextures.append(pending.Pass());
    }

    virtual void consumeTextureCHROMIUM(WGC3Denum target, const WGC3Dbyte* mailbox)
    {
        ASSERT_TRUE(m_currentTexture);
        ASSERT_EQ(target, GL_TEXTURE_2D);
        m_textures.set(m_currentTexture, m_sharedData->consumeTexture(mailbox, m_lastWaitedSyncPoint));
    }

    void getPixels(const IntSize& size, WGC3Denum format, uint8_t* pixels)
    {
        ASSERT_TRUE(m_currentTexture);
        Texture* texture = m_textures.get(m_currentTexture);
        ASSERT_TRUE(texture);
        ASSERT_EQ(texture->size, size);
        ASSERT_EQ(texture->format, format);
        memcpy(pixels, texture->data.get(), textureSize(size, format));
    }

    int textureCount()
    {
        return m_textures.size();
    }

protected:
    ResourceProviderContext(const Attributes& attrs, ContextSharedData* sharedData)
        : CompositorFakeWebGraphicsContext3D(attrs)
        , m_sharedData(sharedData)
        , m_currentTexture(0)
        , m_lastWaitedSyncPoint(0)
    { }

private:
    void allocateTexture(const IntSize& size, WGC3Denum format)
    {
        ASSERT_TRUE(m_currentTexture);
        m_textures.set(m_currentTexture, make_scoped_ptr(new Texture(size, format)));
    }

    void setPixels(int xoffset, int yoffset, int width, int height, const void* pixels)
    {
        ASSERT_TRUE(m_currentTexture);
        Texture* texture = m_textures.get(m_currentTexture);
        ASSERT_TRUE(texture);
        ASSERT_TRUE(xoffset >= 0 && xoffset+width <= texture->size.width());
        ASSERT_TRUE(yoffset >= 0 && yoffset+height <= texture->size.height());
        ASSERT_TRUE(pixels);
        size_t inPitch = textureSize(IntSize(width, 1), texture->format);
        size_t outPitch = textureSize(IntSize(texture->size.width(), 1), texture->format);
        uint8_t* dest = texture->data.get() + yoffset * outPitch + textureSize(IntSize(xoffset, 1), texture->format);
        const uint8_t* src = static_cast<const uint8_t*>(pixels);
        for (int i = 0; i < height; ++i) {
            memcpy(dest, src, inPitch);
            dest += outPitch;
            src += inPitch;
        }
    }

    typedef ScopedPtrHashMap<WebGLId, Texture> TextureMap;
    struct PendingProduceTexture {
        WGC3Dbyte mailbox[64];
        scoped_ptr<Texture> texture;
    };
    typedef ScopedPtrDeque<PendingProduceTexture> PendingProduceTextureList;
    ContextSharedData* m_sharedData;
    WebGLId m_currentTexture;
    TextureMap m_textures;
    unsigned m_lastWaitedSyncPoint;
    PendingProduceTextureList m_pendingProduceTextures;
};

class ResourceProviderTest : public testing::TestWithParam<ResourceProvider::ResourceType> {
public:
    ResourceProviderTest()
        : m_sharedData(ContextSharedData::create())
        , m_context(FakeWebCompositorOutputSurface::create(ResourceProviderContext::create(m_sharedData.get()).PassAs<WebKit::WebGraphicsContext3D>().PassAs<WebKit::WebGraphicsContext3D>()))
        , m_resourceProvider(ResourceProvider::create(m_context.get()))
    {
        m_resourceProvider->setDefaultResourceType(GetParam());
    }

    ResourceProviderContext* context() { return static_cast<ResourceProviderContext*>(m_context->context3D()); }

    void getResourcePixels(ResourceProvider::ResourceId id, const IntSize& size, WGC3Denum format, uint8_t* pixels)
    {
        if (GetParam() == ResourceProvider::GLTexture) {
            ResourceProvider::ScopedReadLockGL lockGL(m_resourceProvider.get(), id);
            ASSERT_NE(0U, lockGL.textureId());
            context()->bindTexture(GL_TEXTURE_2D, lockGL.textureId());
            context()->getPixels(size, format, pixels);
        } else if (GetParam() == ResourceProvider::Bitmap) {
            ResourceProvider::ScopedReadLockSoftware lockSoftware(m_resourceProvider.get(), id);
            memcpy(pixels, lockSoftware.skBitmap()->getPixels(), lockSoftware.skBitmap()->getSize());
        }
    }

    void expectNumResources(int count)
    {
        EXPECT_EQ(count, static_cast<int>(m_resourceProvider->numResources()));
        if (GetParam() == ResourceProvider::GLTexture)
            EXPECT_EQ(count, context()->textureCount());
    }

protected:
    DebugScopedSetImplThread implThread;
    scoped_ptr<ContextSharedData> m_sharedData;
    scoped_ptr<GraphicsContext> m_context;
    scoped_ptr<ResourceProvider> m_resourceProvider;
};

TEST_P(ResourceProviderTest, Basic)
{
    IntSize size(1, 1);
    WGC3Denum format = GL_RGBA;
    int pool = 1;
    size_t pixelSize = textureSize(size, format);
    ASSERT_EQ(4U, pixelSize);

    ResourceProvider::ResourceId id = m_resourceProvider->createResource(pool, size, format, ResourceProvider::TextureUsageAny);
    expectNumResources(1);

    uint8_t data[4] = {1, 2, 3, 4};
    IntRect rect(IntPoint(), size);
    m_resourceProvider->upload(id, data, rect, rect, IntSize());

    uint8_t result[4] = {0};
    getResourcePixels(id, size, format, result);
    EXPECT_EQ(0, memcmp(data, result, pixelSize));

    m_resourceProvider->deleteResource(id);
    expectNumResources(0);
}

TEST_P(ResourceProviderTest, DeleteOwnedResources)
{
    IntSize size(1, 1);
    WGC3Denum format = GL_RGBA;
    int pool = 1;

    const int count = 3;
    for (int i = 0; i < count; ++i)
        m_resourceProvider->createResource(pool, size, format, ResourceProvider::TextureUsageAny);
    expectNumResources(3);

    m_resourceProvider->deleteOwnedResources(pool+1);
    expectNumResources(3);

    m_resourceProvider->deleteOwnedResources(pool);
    expectNumResources(0);
}

TEST_P(ResourceProviderTest, Upload)
{
    IntSize size(2, 2);
    WGC3Denum format = GL_RGBA;
    int pool = 1;
    size_t pixelSize = textureSize(size, format);
    ASSERT_EQ(16U, pixelSize);

    ResourceProvider::ResourceId id = m_resourceProvider->createResource(pool, size, format, ResourceProvider::TextureUsageAny);

    uint8_t image[16] = {0};
    IntRect imageRect(IntPoint(), size);
    m_resourceProvider->upload(id, image, imageRect, imageRect, IntSize());

    for (uint8_t i = 0 ; i < pixelSize; ++i)
        image[i] = i;

    uint8_t result[16] = {0};
    {
        IntRect sourceRect(0, 0, 1, 1);
        IntSize destOffset(0, 0);
        m_resourceProvider->upload(id, image, imageRect, sourceRect, destOffset);

        uint8_t expected[16] = {0, 1, 2, 3,   0, 0, 0, 0,
                                0, 0, 0, 0,   0, 0, 0, 0};
        getResourcePixels(id, size, format, result);
        EXPECT_EQ(0, memcmp(expected, result, pixelSize));
    }
    {
        IntRect sourceRect(0, 0, 1, 1);
        IntSize destOffset(1, 1);
        m_resourceProvider->upload(id, image, imageRect, sourceRect, destOffset);

        uint8_t expected[16] = {0, 1, 2, 3,   0, 0, 0, 0,
                                0, 0, 0, 0,   0, 1, 2, 3};
        getResourcePixels(id, size, format, result);
        EXPECT_EQ(0, memcmp(expected, result, pixelSize));
    }
    {
        IntRect sourceRect(1, 0, 1, 1);
        IntSize destOffset(0, 1);
        m_resourceProvider->upload(id, image, imageRect, sourceRect, destOffset);

        uint8_t expected[16] = {0, 1, 2, 3,   0, 0, 0, 0,
                                4, 5, 6, 7,   0, 1, 2, 3};
        getResourcePixels(id, size, format, result);
        EXPECT_EQ(0, memcmp(expected, result, pixelSize));
    }
    {
        IntRect offsetImageRect(IntPoint(100, 100), size);
        IntRect sourceRect(100, 100, 1, 1);
        IntSize destOffset(1, 0);
        m_resourceProvider->upload(id, image, offsetImageRect, sourceRect, destOffset);

        uint8_t expected[16] = {0, 1, 2, 3,   0, 1, 2, 3,
                                4, 5, 6, 7,   0, 1, 2, 3};
        getResourcePixels(id, size, format, result);
        EXPECT_EQ(0, memcmp(expected, result, pixelSize));
    }


    m_resourceProvider->deleteResource(id);
}

TEST_P(ResourceProviderTest, TransferResources)
{
    // Resource transfer is only supported with GL textures for now.
    if (GetParam() != ResourceProvider::GLTexture)
        return;

    scoped_ptr<GraphicsContext> childContext(FakeWebCompositorOutputSurface::create(ResourceProviderContext::create(m_sharedData.get()).PassAs<WebKit::WebGraphicsContext3D>()));
    scoped_ptr<ResourceProvider> childResourceProvider(ResourceProvider::create(childContext.get()));

    IntSize size(1, 1);
    WGC3Denum format = GL_RGBA;
    int pool = 1;
    size_t pixelSize = textureSize(size, format);
    ASSERT_EQ(4U, pixelSize);

    ResourceProvider::ResourceId id1 = childResourceProvider->createResource(pool, size, format, ResourceProvider::TextureUsageAny);
    uint8_t data1[4] = {1, 2, 3, 4};
    IntRect rect(IntPoint(), size);
    childResourceProvider->upload(id1, data1, rect, rect, IntSize());

    ResourceProvider::ResourceId id2 = childResourceProvider->createResource(pool, size, format, ResourceProvider::TextureUsageAny);
    uint8_t data2[4] = {5, 5, 5, 5};
    childResourceProvider->upload(id2, data2, rect, rect, IntSize());

    int childPool = 2;
    int childId = m_resourceProvider->createChild(childPool);

    {
        // Transfer some resources to the parent.
        ResourceProvider::ResourceIdArray resourceIdsToTransfer;
        resourceIdsToTransfer.push_back(id1);
        resourceIdsToTransfer.push_back(id2);
        ResourceProvider::TransferableResourceList list = childResourceProvider->prepareSendToParent(resourceIdsToTransfer);
        EXPECT_NE(0u, list.syncPoint);
        EXPECT_EQ(2u, list.resources.size());
        EXPECT_TRUE(childResourceProvider->inUseByConsumer(id1));
        EXPECT_TRUE(childResourceProvider->inUseByConsumer(id2));
        m_resourceProvider->receiveFromChild(childId, list);
    }

    EXPECT_EQ(2u, m_resourceProvider->numResources());
    EXPECT_EQ(2u, m_resourceProvider->mailboxCount());
    ResourceProvider::ResourceIdMap resourceMap = m_resourceProvider->getChildToParentMap(childId);
    ResourceProvider::ResourceId mappedId1 = resourceMap[id1];
    ResourceProvider::ResourceId mappedId2 = resourceMap[id2];
    EXPECT_NE(0u, mappedId1);
    EXPECT_NE(0u, mappedId2);
    EXPECT_FALSE(m_resourceProvider->inUseByConsumer(id1));
    EXPECT_FALSE(m_resourceProvider->inUseByConsumer(id2));

    uint8_t result[4] = {0};
    getResourcePixels(mappedId1, size, format, result);
    EXPECT_EQ(0, memcmp(data1, result, pixelSize));

    getResourcePixels(mappedId2, size, format, result);
    EXPECT_EQ(0, memcmp(data2, result, pixelSize));

    {
        // Check that transfering again the same resource from the child to the
        // parent is a noop.
        ResourceProvider::ResourceIdArray resourceIdsToTransfer;
        resourceIdsToTransfer.push_back(id1);
        ResourceProvider::TransferableResourceList list = childResourceProvider->prepareSendToParent(resourceIdsToTransfer);
        EXPECT_EQ(0u, list.syncPoint);
        EXPECT_EQ(0u, list.resources.size());
    }

    {
        // Transfer resources back from the parent to the child.
        ResourceProvider::ResourceIdArray resourceIdsToTransfer;
        resourceIdsToTransfer.push_back(mappedId1);
        resourceIdsToTransfer.push_back(mappedId2);
        ResourceProvider::TransferableResourceList list = m_resourceProvider->prepareSendToChild(childId, resourceIdsToTransfer);
        EXPECT_NE(0u, list.syncPoint);
        EXPECT_EQ(2u, list.resources.size());
        childResourceProvider->receiveFromParent(list);
    }
    EXPECT_EQ(0u, m_resourceProvider->mailboxCount());
    EXPECT_EQ(2u, childResourceProvider->mailboxCount());
    EXPECT_FALSE(childResourceProvider->inUseByConsumer(id1));
    EXPECT_FALSE(childResourceProvider->inUseByConsumer(id2));

    ResourceProviderContext* childContext3D = static_cast<ResourceProviderContext*>(childContext->context3D());
    {
        ResourceProvider::ScopedReadLockGL lock(childResourceProvider.get(), id1);
        ASSERT_NE(0U, lock.textureId());
        childContext3D->bindTexture(GL_TEXTURE_2D, lock.textureId());
        childContext3D->getPixels(size, format, result);
        EXPECT_EQ(0, memcmp(data1, result, pixelSize));
    }
    {
        ResourceProvider::ScopedReadLockGL lock(childResourceProvider.get(), id2);
        ASSERT_NE(0U, lock.textureId());
        childContext3D->bindTexture(GL_TEXTURE_2D, lock.textureId());
        childContext3D->getPixels(size, format, result);
        EXPECT_EQ(0, memcmp(data2, result, pixelSize));
    }

    {
        // Transfer resources to the parent again.
        ResourceProvider::ResourceIdArray resourceIdsToTransfer;
        resourceIdsToTransfer.push_back(id1);
        resourceIdsToTransfer.push_back(id2);
        ResourceProvider::TransferableResourceList list = childResourceProvider->prepareSendToParent(resourceIdsToTransfer);
        EXPECT_NE(0u, list.syncPoint);
        EXPECT_EQ(2u, list.resources.size());
        EXPECT_TRUE(childResourceProvider->inUseByConsumer(id1));
        EXPECT_TRUE(childResourceProvider->inUseByConsumer(id2));
        m_resourceProvider->receiveFromChild(childId, list);
    }

    EXPECT_EQ(2u, m_resourceProvider->numResources());
    m_resourceProvider->destroyChild(childId);
    EXPECT_EQ(0u, m_resourceProvider->numResources());
    EXPECT_EQ(0u, m_resourceProvider->mailboxCount());
}

TEST_P(ResourceProviderTest, DeleteTransferredResources)
{
    // Resource transfer is only supported with GL textures for now.
    if (GetParam() != ResourceProvider::GLTexture)
        return;

    scoped_ptr<GraphicsContext> childContext(FakeWebCompositorOutputSurface::create(ResourceProviderContext::create(m_sharedData.get()).PassAs<WebKit::WebGraphicsContext3D>()));
    scoped_ptr<ResourceProvider> childResourceProvider(ResourceProvider::create(childContext.get()));

    IntSize size(1, 1);
    WGC3Denum format = GL_RGBA;
    int pool = 1;
    size_t pixelSize = textureSize(size, format);
    ASSERT_EQ(4U, pixelSize);

    ResourceProvider::ResourceId id = childResourceProvider->createResource(pool, size, format, ResourceProvider::TextureUsageAny);
    uint8_t data[4] = {1, 2, 3, 4};
    IntRect rect(IntPoint(), size);
    childResourceProvider->upload(id, data, rect, rect, IntSize());

    int childPool = 2;
    int childId = m_resourceProvider->createChild(childPool);

    {
        // Transfer some resource to the parent.
        ResourceProvider::ResourceIdArray resourceIdsToTransfer;
        resourceIdsToTransfer.push_back(id);
        ResourceProvider::TransferableResourceList list = childResourceProvider->prepareSendToParent(resourceIdsToTransfer);
        EXPECT_NE(0u, list.syncPoint);
        EXPECT_EQ(1u, list.resources.size());
        EXPECT_TRUE(childResourceProvider->inUseByConsumer(id));
        m_resourceProvider->receiveFromChild(childId, list);
    }

    // Delete textures in the child, while they are transfered.
    childResourceProvider->deleteResource(id);
    EXPECT_EQ(1u, childResourceProvider->numResources());

    {
        // Transfer resources back from the parent to the child.
        ResourceProvider::ResourceIdMap resourceMap = m_resourceProvider->getChildToParentMap(childId);
        ResourceProvider::ResourceId mappedId = resourceMap[id];
        EXPECT_NE(0u, mappedId);
        ResourceProvider::ResourceIdArray resourceIdsToTransfer;
        resourceIdsToTransfer.push_back(mappedId);
        ResourceProvider::TransferableResourceList list = m_resourceProvider->prepareSendToChild(childId, resourceIdsToTransfer);
        EXPECT_NE(0u, list.syncPoint);
        EXPECT_EQ(1u, list.resources.size());
        childResourceProvider->receiveFromParent(list);
    }
    EXPECT_EQ(0u, childResourceProvider->numResources());
}

INSTANTIATE_TEST_CASE_P(ResourceProviderTests,
                        ResourceProviderTest,
                        ::testing::Values(ResourceProvider::GLTexture,
                                          ResourceProvider::Bitmap));

} // namespace
