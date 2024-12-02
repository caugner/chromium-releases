// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CCRenderPassTestCommon_h
#define CCRenderPassTestCommon_h

#include "cc/render_pass.h"

namespace WebKitTests {

class TestRenderPass : public cc::RenderPass {
public:
    static scoped_ptr<TestRenderPass> create(Id id, gfx::Rect outputRect, const WebKit::WebTransformationMatrix& transformToRootTarget) {
        return make_scoped_ptr(new TestRenderPass(id, outputRect, transformToRootTarget));
    }

    cc::QuadList& quadList() { return m_quadList; }
    cc::SharedQuadStateList& sharedQuadStateList() { return m_sharedQuadStateList; }

    void appendQuad(scoped_ptr<cc::DrawQuad> quad) { m_quadList.append(quad.Pass()); }
    void appendSharedQuadState(scoped_ptr<cc::SharedQuadState> state) { m_sharedQuadStateList.append(state.Pass()); }

protected:
    TestRenderPass(Id id, gfx::Rect outputRect, const WebKit::WebTransformationMatrix& transformToRootTarget)
        : RenderPass(id, outputRect, transformToRootTarget) { }
};

}  // namespace WebKitTests

#endif // CCRenderPassTestCommon_h
