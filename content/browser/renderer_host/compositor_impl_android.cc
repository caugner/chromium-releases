// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/compositor_impl_android.h"

#include <android/bitmap.h>
#include <android/native_window_jni.h>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "content/browser/gpu/browser_gpu_channel_host_factory.h"
#include "content/browser/gpu/gpu_surface_tracker.h"
#include "content/browser/renderer_host/image_transport_factory_android.h"
#include "content/common/gpu/client/gl_helper.h"
#include "content/common/gpu/client/gpu_channel_host.h"
#include "content/common/gpu/client/webgraphicscontext3d_command_buffer_impl.h"
#include "content/common/gpu/gpu_process_launch_causes.h"
#include "content/public/common/content_switches.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"
#include "third_party/WebKit/Source/Platform/chromium/public/Platform.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebCompositorSupport.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebCompositorOutputSurface.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebGraphicsContext3D.h"
#include "ui/gfx/android/java_bitmap.h"


namespace gfx {
class JavaBitmap;
}

namespace {

static bool g_initialized = false;

// Adapts a pure WebGraphicsContext3D into a WebCompositorOutputSurface.
class WebGraphicsContextToOutputSurfaceAdapter :
    public WebKit::WebCompositorOutputSurface {
public:
    explicit WebGraphicsContextToOutputSurfaceAdapter(
        WebKit::WebGraphicsContext3D* context)
        : m_context3D(context)
        , m_client(0)
    {
    }

    virtual bool bindToClient(
        WebKit::WebCompositorOutputSurfaceClient* client) OVERRIDE
    {
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

    virtual WebKit::WebGraphicsContext3D* context3D() const OVERRIDE
    {
        return m_context3D.get();
    }

    virtual void sendFrameToParentCompositor(
        const WebKit::WebCompositorFrame&) OVERRIDE
    {
    }

private:
    scoped_ptr<WebKit::WebGraphicsContext3D> m_context3D;
    Capabilities m_capabilities;
    WebKit::WebCompositorOutputSurfaceClient* m_client;
};

} // anonymous namespace

namespace content {

// static
Compositor* Compositor::Create(Client* client) {
  return client ? new CompositorImpl(client) : NULL;
}

// static
void Compositor::Initialize() {
  g_initialized = true;
  // Android WebView runs in single process, and depends on the renderer to
  // perform WebKit::Platform initialization for the entire process. The
  // renderer, however, does that lazily which in practice means it waits
  // until the first page load request.
  // The WebView-specific rendering code isn't ready yet so we only want to
  // trick the rest of it into thinking the Compositor is initialized, which
  // keeps us from crashing.
  // See BUG 152904.
  if (WebKit::Platform::current() == NULL) {
    LOG(WARNING) << "CompositorImpl(Android)::Initialize(): WebKit::Platform "
                 << "is not initialized, COMPOSITOR IS NOT INITIALIZED "
                 << "(this is OK and expected if you're running Android"
                 << "WebView tests).";
    // We only ever want to run this hack in single process mode.
    CHECK(CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kSingleProcess));
    return;
  }
  WebKit::Platform::current()->compositorSupport()->initialize(NULL);
}

// static
bool CompositorImpl::IsInitialized() {
  return g_initialized;
}

CompositorImpl::CompositorImpl(Compositor::Client* client)
    : window_(NULL),
      surface_id_(0),
      client_(client) {
  DCHECK(client);
  root_layer_.reset(
      WebKit::Platform::current()->compositorSupport()->createLayer());
}

CompositorImpl::~CompositorImpl() {
}

void CompositorImpl::Composite() {
  if (host_.get())
    host_->composite();
}

void CompositorImpl::SetRootLayer(WebKit::WebLayer* root_layer) {
  root_layer_->removeAllChildren();
  root_layer_->addChild(root_layer);
}

void CompositorImpl::SetWindowSurface(ANativeWindow* window) {
  GpuSurfaceTracker* tracker = GpuSurfaceTracker::Get();

  if (window_) {
    tracker->RemoveSurface(surface_id_);
    ANativeWindow_release(window_);
    window_ = NULL;
    surface_id_ = 0;
    host_.reset();
  }

  if (window) {
    window_ = window;
    ANativeWindow_acquire(window);
    surface_id_ = tracker->AddSurfaceForNativeWidget(window);
    tracker->SetSurfaceHandle(
        surface_id_,
        gfx::GLSurfaceHandle(gfx::kDummyPluginWindow, false));

    DCHECK(!host_.get());
    WebKit::WebLayerTreeView::Settings settings;
    settings.refreshRate = 60.0;
    WebKit::WebCompositorSupport* compositor_support =
        WebKit::Platform::current()->compositorSupport();
    host_.reset(
        compositor_support->createLayerTreeView(this, *root_layer_, settings));
    host_->setVisible(true);
    host_->setSurfaceReady();
    host_->setViewportSize(size_);
  }
}

void CompositorImpl::SetWindowBounds(const gfx::Size& size) {
  if (size_ == size)
    return;

  size_ = size;
  host_->setViewportSize(size);
  root_layer_->setBounds(size);
}

bool CompositorImpl::CompositeAndReadback(void *pixels, const gfx::Rect& rect) {
  if (host_.get())
    return host_->compositeAndReadback(pixels, rect);
  else
    return false;
}

WebKit::WebGLId CompositorImpl::GenerateTexture(gfx::JavaBitmap& bitmap) {
  unsigned int texture_id = BuildBasicTexture();
  WebKit::WebGraphicsContext3D* context =
      ImageTransportFactoryAndroid::GetInstance()->GetContext3D();
  if (texture_id == 0 || context->isContextLost())
    return 0;
  WebKit::WebGLId format = GetGLFormatForBitmap(bitmap);
  WebKit::WebGLId type = GetGLTypeForBitmap(bitmap);

  context->texImage2D(GL_TEXTURE_2D,
                      0,
                      format,
                      bitmap.size().width(),
                      bitmap.size().height(),
                      0,
                      format,
                      type,
                      bitmap.pixels());
  DCHECK(context->getError() == GL_NO_ERROR);
  return texture_id;
}

WebKit::WebGLId CompositorImpl::GenerateCompressedTexture(gfx::Size& size,
                                                          int data_size,
                                                          void* data) {
  unsigned int texture_id = BuildBasicTexture();
  WebKit::WebGraphicsContext3D* context =
        ImageTransportFactoryAndroid::GetInstance()->GetContext3D();
  if (texture_id == 0 || context->isContextLost())
    return 0;
  context->compressedTexImage2D(GL_TEXTURE_2D,
                                0,
                                GL_ETC1_RGB8_OES,
                                size.width(),
                                size.height(),
                                0,
                                data_size,
                                data);
  DCHECK(context->getError() == GL_NO_ERROR);
  return texture_id;
}

void CompositorImpl::DeleteTexture(WebKit::WebGLId texture_id) {
  WebKit::WebGraphicsContext3D* context =
      ImageTransportFactoryAndroid::GetInstance()->GetContext3D();
  if (context->isContextLost())
    return;
  context->deleteTexture(texture_id);
  DCHECK(context->getError() == GL_NO_ERROR);
}

void CompositorImpl::CopyTextureToBitmap(WebKit::WebGLId texture_id,
                                         gfx::JavaBitmap& bitmap) {
  GLHelper* helper = ImageTransportFactoryAndroid::GetInstance()->GetGLHelper();
  helper->ReadbackTextureSync(texture_id,
                              bitmap.size(),
                              static_cast<unsigned char*> (bitmap.pixels()));
}

void CompositorImpl::updateAnimations(double frameBeginTime) {
}

void CompositorImpl::layout() {
}

void CompositorImpl::applyScrollAndScale(const WebKit::WebSize& scrollDelta,
                                     float scaleFactor) {
}

WebKit::WebCompositorOutputSurface* CompositorImpl::createOutputSurface() {
  DCHECK(window_ && surface_id_);
  WebKit::WebGraphicsContext3D::Attributes attrs;
  attrs.shareResources = true;
  attrs.noAutomaticFlushes = true;
  GpuChannelHostFactory* factory = BrowserGpuChannelHostFactory::instance();
  GURL url("chrome://gpu/Compositor::createContext3D");
  base::WeakPtr<WebGraphicsContext3DSwapBuffersClient> swap_client;
  scoped_ptr<WebGraphicsContext3DCommandBufferImpl> context(
      new WebGraphicsContext3DCommandBufferImpl(
          surface_id_,
          url,
          factory,
          swap_client));
  if (!context->Initialize(
      attrs,
      false,
      CAUSE_FOR_GPU_LAUNCH_WEBGRAPHICSCONTEXT3DCOMMANDBUFFERIMPL_INITIALIZE)) {
    LOG(ERROR) << "Failed to create 3D context for compositor.";
    return NULL;
  }

  return new WebGraphicsContextToOutputSurfaceAdapter(context.release());
}

void CompositorImpl::didRecreateOutputSurface(bool success) {
}

void CompositorImpl::didCommit() {
}

void CompositorImpl::didCommitAndDrawFrame() {
}

void CompositorImpl::didCompleteSwapBuffers() {
  client_->OnSwapBuffersCompleted();
}

void CompositorImpl::scheduleComposite() {
  client_->ScheduleComposite();
}

WebKit::WebGLId CompositorImpl::BuildBasicTexture() {
  WebKit::WebGraphicsContext3D* context =
            ImageTransportFactoryAndroid::GetInstance()->GetContext3D();
  if (context->isContextLost())
    return 0;
  WebKit::WebGLId texture_id = context->createTexture();
  context->bindTexture(GL_TEXTURE_2D, texture_id);
  context->texParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  context->texParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  context->texParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  context->texParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  DCHECK(context->getError() == GL_NO_ERROR);
  return texture_id;
}

WebKit::WGC3Denum CompositorImpl::GetGLFormatForBitmap(
    gfx::JavaBitmap& bitmap) {
  switch (bitmap.format()) {
    case ANDROID_BITMAP_FORMAT_A_8:
      return GL_ALPHA;
      break;
    case ANDROID_BITMAP_FORMAT_RGBA_4444:
      return GL_RGBA;
      break;
    case ANDROID_BITMAP_FORMAT_RGBA_8888:
      return GL_RGBA;
      break;
    case ANDROID_BITMAP_FORMAT_RGB_565:
    default:
      return GL_RGB;
  }
}

WebKit::WGC3Denum CompositorImpl::GetGLTypeForBitmap(gfx::JavaBitmap& bitmap) {
  switch (bitmap.format()) {
    case ANDROID_BITMAP_FORMAT_A_8:
      return GL_UNSIGNED_BYTE;
      break;
    case ANDROID_BITMAP_FORMAT_RGBA_4444:
      return GL_UNSIGNED_SHORT_4_4_4_4;
      break;
    case ANDROID_BITMAP_FORMAT_RGBA_8888:
      return GL_UNSIGNED_BYTE;
      break;
    case ANDROID_BITMAP_FORMAT_RGB_565:
    default:
      return GL_UNSIGNED_SHORT_5_6_5;
  }
}

} // namespace content
