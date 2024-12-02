// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CCIOSurfaceLayerImpl_h
#define CCIOSurfaceLayerImpl_h

#include "IntSize.h"
#include "cc/layer_impl.h"

namespace cc {

class IOSurfaceLayerImpl : public LayerImpl {
public:
    static scoped_ptr<IOSurfaceLayerImpl> create(int id)
    {
        return make_scoped_ptr(new IOSurfaceLayerImpl(id));
    }
    virtual ~IOSurfaceLayerImpl();

    void setIOSurfaceProperties(unsigned ioSurfaceId, const IntSize&);

    virtual void appendQuads(QuadSink&, AppendQuadsData&) OVERRIDE;

    virtual void willDraw(ResourceProvider*) OVERRIDE;
    virtual void didLoseContext() OVERRIDE;

    virtual void dumpLayerProperties(std::string*, int indent) const OVERRIDE;

private:
    explicit IOSurfaceLayerImpl(int);

    virtual const char* layerTypeAsString() const OVERRIDE;

    unsigned m_ioSurfaceId;
    IntSize m_ioSurfaceSize;
    bool m_ioSurfaceChanged;
    unsigned m_ioSurfaceTextureId;
};

}

#endif // CCIOSurfaceLayerImpl_h
