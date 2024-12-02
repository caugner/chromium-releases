// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LayerUpdater_h
#define LayerUpdater_h

#include "base/memory/ref_counted.h"
#include "cc/prioritized_texture.h"
#include "third_party/khronos/GLES2/gl2.h"

namespace cc {

class IntRect;
class IntSize;
class TextureManager;
struct RenderingStats;
class ResourceUpdateQueue;

class LayerUpdater : public base::RefCounted<LayerUpdater> {
public:
    // Allows updaters to store per-resource update properties.
    class Resource {
    public:
        virtual ~Resource();

        PrioritizedTexture* texture() { return m_texture.get(); }
        void swapTextureWith(scoped_ptr<PrioritizedTexture>& texture) { m_texture.swap(texture); }
        // TODO(reveman): partialUpdate should be a property of this class
        // instead of an argument passed to update().
        virtual void update(ResourceUpdateQueue&, const IntRect& sourceRect, const IntSize& destOffset, bool partialUpdate, RenderingStats&) = 0;
    protected:
        explicit Resource(scoped_ptr<PrioritizedTexture> texture);

    private:
        scoped_ptr<PrioritizedTexture> m_texture;
    };

    LayerUpdater() { }

    virtual scoped_ptr<Resource> createResource(PrioritizedTextureManager*) = 0;
    // The |resultingOpaqueRect| gives back a region of the layer that was painted opaque. If the layer is marked opaque in the updater,
    // then this region should be ignored in preference for the entire layer's area.
    virtual void prepareToUpdate(const IntRect& contentRect, const IntSize& tileSize, float contentsWidthScale, float contentsHeightScale, IntRect& resultingOpaqueRect, RenderingStats&) { }

    // Set true by the layer when it is known that the entire output is going to be opaque.
    virtual void setOpaque(bool) { }

protected:
    virtual ~LayerUpdater() { }

private:
    friend class base::RefCounted<LayerUpdater>;
};

}  // namespace cc

#endif // LayerUpdater_h
