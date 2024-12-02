// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CCDelegatedRendererLayerImpl_h
#define CCDelegatedRendererLayerImpl_h

#include "cc/layer_impl.h"
#include "cc/scoped_ptr_vector.h"

namespace cc {

class DelegatedRendererLayerImpl : public LayerImpl {
public:
    static scoped_ptr<DelegatedRendererLayerImpl> create(int id) { return make_scoped_ptr(new DelegatedRendererLayerImpl(id)); }
    virtual ~DelegatedRendererLayerImpl();

    virtual bool descendantDrawsContent() OVERRIDE;
    virtual bool hasContributingDelegatedRenderPasses() const OVERRIDE;

    // This gives ownership of the RenderPasses to the layer.
    void setRenderPasses(ScopedPtrVector<RenderPass>&);
    void clearRenderPasses();

    virtual void didLoseContext() OVERRIDE;

    virtual RenderPass::Id firstContributingRenderPassId() const OVERRIDE;
    virtual RenderPass::Id nextContributingRenderPassId(RenderPass::Id) const OVERRIDE;

    void appendContributingRenderPasses(RenderPassSink&);
    virtual void appendQuads(QuadSink&, AppendQuadsData&) OVERRIDE;

private:
    explicit DelegatedRendererLayerImpl(int);

    RenderPass::Id convertDelegatedRenderPassId(RenderPass::Id delegatedRenderPassId) const;

    void appendRenderPassQuads(QuadSink&, AppendQuadsData&, RenderPass* fromDelegatedRenderPass) const;

    virtual const char* layerTypeAsString() const OVERRIDE;

    ScopedPtrVector<RenderPass> m_renderPassesInDrawOrder;
    base::hash_map<RenderPass::Id, int> m_renderPassesIndexById;
};

}

#endif // CCDelegatedRendererLayerImpl_h
