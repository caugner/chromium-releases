// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FakeWebCompositorOutputSurface_h
#define FakeWebCompositorOutputSurface_h

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "cc/test/fake_web_compositor_software_output_device.h"
#include <public/WebCompositorOutputSurface.h>
#include <public/WebGraphicsContext3D.h>

namespace WebKit {

class FakeWebCompositorOutputSurface : public WebCompositorOutputSurface {
public:
    static inline scoped_ptr<FakeWebCompositorOutputSurface> create(scoped_ptr<WebGraphicsContext3D> context3D)
    {
        return make_scoped_ptr(new FakeWebCompositorOutputSurface(context3D.Pass()));
    }

    static inline scoped_ptr<FakeWebCompositorOutputSurface> createSoftware(scoped_ptr<WebCompositorSoftwareOutputDevice> softwareDevice)
    {
        return make_scoped_ptr(new FakeWebCompositorOutputSurface(softwareDevice.Pass()));
    }

    virtual bool bindToClient(WebCompositorOutputSurfaceClient* client) OVERRIDE
    {
        if (!m_context3D)
            return true;
        DCHECK(client);
        if (!m_context3D->makeContextCurrent())
            return false;
        m_client = client;
        return true;
    }

    virtual const Capabilities& capabilities() const OVERRIDE
    {
        return m_capabilities;
    }

    virtual WebGraphicsContext3D* context3D() const OVERRIDE
    {
        return m_context3D.get();
    }
    virtual WebCompositorSoftwareOutputDevice* softwareDevice() const OVERRIDE
    {
        return m_softwareDevice.get();
    }

    virtual void sendFrameToParentCompositor(const WebCompositorFrame&) OVERRIDE
    {
    }

private:
    explicit FakeWebCompositorOutputSurface(scoped_ptr<WebGraphicsContext3D> context3D)
    {
        m_context3D = context3D.Pass();
    }

    explicit FakeWebCompositorOutputSurface(scoped_ptr<WebCompositorSoftwareOutputDevice> softwareDevice)
    {
        m_softwareDevice = softwareDevice.Pass();
    }

    scoped_ptr<WebGraphicsContext3D> m_context3D;
    scoped_ptr<WebCompositorSoftwareOutputDevice> m_softwareDevice;
    Capabilities m_capabilities;
    WebCompositorOutputSurfaceClient* m_client;
};

} // namespace WebKit

#endif // FakeWebCompositorOutputSurface_h
