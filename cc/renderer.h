// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CCRenderer_h
#define CCRenderer_h

#include "FloatQuad.h"
#include "base/basictypes.h"
#include "cc/layer_tree_host.h"
#include "cc/managed_memory_policy.h"
#include "cc/render_pass.h"

namespace cc {

class ScopedTexture;

class RendererClient {
public:
    virtual const IntSize& deviceViewportSize() const = 0;
    virtual const LayerTreeSettings& settings() const = 0;
    virtual void didLoseContext() = 0;
    virtual void onSwapBuffersComplete() = 0;
    virtual void setFullRootLayerDamage() = 0;
    virtual void setManagedMemoryPolicy(const ManagedMemoryPolicy& policy) = 0;
    virtual void enforceManagedMemoryPolicy(const ManagedMemoryPolicy& policy) = 0;
protected:
    virtual ~RendererClient() { }
};

class Renderer {
public:
    // This enum defines the various resource pools for the ResourceProvider
    // where textures get allocated.
    enum ResourcePool {
      ImplPool = 1, // This pool is for textures that get allocated on the impl thread (e.g. RenderSurfaces).
      ContentPool // This pool is for textures that get allocated on the main thread (e.g. tiles).
    };

    virtual ~Renderer() { }

    virtual const RendererCapabilities& capabilities() const = 0;

    const LayerTreeSettings& settings() const { return m_client->settings(); }

    gfx::Size viewportSize() { return m_client->deviceViewportSize(); }
    int viewportWidth() { return viewportSize().width(); }
    int viewportHeight() { return viewportSize().height(); }

    virtual void viewportChanged() { }

    virtual void decideRenderPassAllocationsForFrame(const RenderPassList&) { }
    virtual bool haveCachedResourcesForRenderPassId(RenderPass::Id) const;

    virtual void drawFrame(const RenderPassList&, const RenderPassIdHashMap&) = 0;

    // waits for rendering to finish
    virtual void finish() = 0;

    virtual void doNoOp() { }
    // puts backbuffer onscreen
    virtual bool swapBuffers() = 0;

    virtual void getFramebufferPixels(void *pixels, const IntRect&) = 0;

    virtual bool isContextLost();

    virtual void setVisible(bool) = 0;

    virtual void sendManagedMemoryStats(size_t bytesVisible, size_t bytesVisibleAndNearby, size_t bytesAllocated) = 0;

protected:
    explicit Renderer(RendererClient* client)
        : m_client(client)
    {
    }

    RendererClient* m_client;

    DISALLOW_COPY_AND_ASSIGN(Renderer);
};

}

#endif // CCRenderer_h
