// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/native/aw_contents.h"

#include "android_webview/browser/net_disk_cache_remover.h"
#include "android_webview/browser/renderer_host/aw_render_view_host_ext.h"
#include "android_webview/native/aw_browser_dependency_factory.h"
#include "android_webview/native/aw_contents_io_thread_client_impl.h"
#include "android_webview/native/aw_web_contents_delegate.h"
#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/supports_user_data.h"
#include "chrome/browser/component/navigation_interception/intercept_navigation_delegate.h"
#include "content/public/browser/android/content_view_core.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/cert_store.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/ssl_status.h"
#include "jni/AwContents_jni.h"
#include "net/base/x509_certificate.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF16;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF16ToJavaString;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaRef;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;
using content::BrowserThread;
using content::ContentViewCore;
using content::WebContents;
using navigation_interception::InterceptNavigationDelegate;

namespace android_webview {

namespace {

const void* kAwContentsUserDataKey = &kAwContentsUserDataKey;

class AwContentsUserData : public base::SupportsUserData::Data {
 public:
  AwContentsUserData(AwContents* ptr) : contents_(ptr) {}

  static AwContents* GetContents(WebContents* web_contents) {
    if (!web_contents)
      return NULL;
    AwContentsUserData* data = reinterpret_cast<AwContentsUserData*>(
        web_contents->GetUserData(kAwContentsUserDataKey));
    return data ? data->contents_ : NULL;
  }

 private:
  AwContents* contents_;
};

}  // namespace

// static
AwContents* AwContents::FromWebContents(WebContents* web_contents) {
  return AwContentsUserData::GetContents(web_contents);
}

AwContents::AwContents(JNIEnv* env,
                       jobject obj,
                       jobject web_contents_delegate,
                       bool private_browsing)
    : java_ref_(env, obj),
      web_contents_delegate_(
          new AwWebContentsDelegate(env, web_contents_delegate)) {
  android_webview::AwBrowserDependencyFactory* dependency_factory =
      android_webview::AwBrowserDependencyFactory::GetInstance();

  web_contents_.reset(dependency_factory->CreateWebContents(private_browsing));

  DCHECK(!AwContents::FromWebContents(web_contents_.get()));
  web_contents_->SetUserData(kAwContentsUserDataKey,
                             new AwContentsUserData(this));

  web_contents_->SetDelegate(web_contents_delegate_.get());
  render_view_host_ext_.reset(new AwRenderViewHostExt(web_contents_.get()));
}

AwContents::~AwContents() {
  DCHECK(AwContents::FromWebContents(web_contents_.get()) == this);
  web_contents_->RemoveUserData(kAwContentsUserDataKey);
  if (find_helper_.get())
    find_helper_->SetListener(NULL);
}

jint AwContents::GetWebContents(JNIEnv* env, jobject obj) {
  return reinterpret_cast<jint>(web_contents_.get());
}

void AwContents::Destroy(JNIEnv* env, jobject obj) {
  delete this;
}

namespace {
// |message| is passed as base::Owned, so it will automatically be deleted
// when the callback goes out of scope.
void DocumentHasImagesCallback(ScopedJavaGlobalRef<jobject>* message,
                               bool has_images) {
  Java_AwContents_onDocumentHasImagesResponse(AttachCurrentThread(),
                                              has_images,
                                              message->obj());
}
}  // namespace

void AwContents::DocumentHasImages(JNIEnv* env, jobject obj, jobject message) {
  ScopedJavaGlobalRef<jobject>* j_message = new ScopedJavaGlobalRef<jobject>();
  j_message->Reset(env, message);
  render_view_host_ext_->DocumentHasImages(
      base::Bind(&DocumentHasImagesCallback, base::Owned(j_message)));
}

namespace {
void GenerateMHTMLCallback(ScopedJavaGlobalRef<jobject>* callback,
                           const FilePath& path, int64 size) {
  JNIEnv* env = AttachCurrentThread();
  // Android files are UTF8, so the path conversion below is safe.
  Java_AwContents_generateMHTMLCallback(
      env,
      ConvertUTF8ToJavaString(env, path.AsUTF8Unsafe()).obj(),
      size, callback->obj());
}
}  // namespace

void AwContents::GenerateMHTML(JNIEnv* env, jobject obj,
                               jstring jpath, jobject callback) {
  ScopedJavaGlobalRef<jobject>* j_callback = new ScopedJavaGlobalRef<jobject>();
  j_callback->Reset(env, callback);
  web_contents_->GenerateMHTML(
      FilePath(ConvertJavaStringToUTF8(env, jpath)),
      base::Bind(&GenerateMHTMLCallback, base::Owned(j_callback)));
}

void AwContents::RunJavaScriptDialog(
    content::JavaScriptMessageType message_type,
    const GURL& origin_url,
    const string16& message_text,
    const string16& default_prompt_text,
    const ScopedJavaLocalRef<jobject>& js_result) {
  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;

  ScopedJavaLocalRef<jstring> jurl(ConvertUTF8ToJavaString(
      env, origin_url.spec()));
  ScopedJavaLocalRef<jstring> jmessage(ConvertUTF16ToJavaString(
      env, message_text));
  switch (message_type) {
    case content::JAVASCRIPT_MESSAGE_TYPE_ALERT:
      Java_AwContents_handleJsAlert(
          env, obj.obj(), jurl.obj(), jmessage.obj(), js_result.obj());
      break;
    case content::JAVASCRIPT_MESSAGE_TYPE_CONFIRM:
      Java_AwContents_handleJsConfirm(
          env, obj.obj(), jurl.obj(), jmessage.obj(), js_result.obj());
      break;
    case content::JAVASCRIPT_MESSAGE_TYPE_PROMPT: {
      ScopedJavaLocalRef<jstring> jdefault_value(
          ConvertUTF16ToJavaString(env, default_prompt_text));
      Java_AwContents_handleJsPrompt(
          env, obj.obj(), jurl.obj(), jmessage.obj(),
          jdefault_value.obj(), js_result.obj());
      break;
    }
    default:
      NOTREACHED();
  }
}

void AwContents::RunBeforeUnloadDialog(
    const GURL& origin_url,
    const string16& message_text,
    const ScopedJavaLocalRef<jobject>& js_result) {
  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;

  ScopedJavaLocalRef<jstring> jurl(ConvertUTF8ToJavaString(
      env, origin_url.spec()));
  ScopedJavaLocalRef<jstring> jmessage(ConvertUTF16ToJavaString(
      env, message_text));
  Java_AwContents_handleJsBeforeUnload(
          env, obj.obj(), jurl.obj(), jmessage.obj(), js_result.obj());
}

void AwContents::onReceivedHttpAuthRequest(
    const JavaRef<jobject>& handler,
    const std::string& host,
    const std::string& realm) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jstring> jhost = ConvertUTF8ToJavaString(env, host);
  ScopedJavaLocalRef<jstring> jrealm = ConvertUTF8ToJavaString(env, realm);
  Java_AwContents_onReceivedHttpAuthRequest(env, java_ref_.get(env).obj(),
                                            handler.obj(), jhost.obj(),
                                            jrealm.obj());
}

void AwContents::SetIoThreadClient(JNIEnv* env, jobject obj, jobject client) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  AwContentsIoThreadClientImpl::Associate(
      web_contents_.get(), ScopedJavaLocalRef<jobject>(env, client));
}

void AwContents::SetInterceptNavigationDelegate(JNIEnv* env,
                                                jobject obj,
                                                jobject delegate) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  InterceptNavigationDelegate::Associate(
      web_contents_.get(),
      make_scoped_ptr(new InterceptNavigationDelegate(env, delegate)));
}

static jint Init(JNIEnv* env,
                 jobject obj,
                 jobject web_contents_delegate,
                 jboolean private_browsing) {
  AwContents* tab = new AwContents(env, obj, web_contents_delegate,
                                   private_browsing);
  return reinterpret_cast<jint>(tab);
}

bool RegisterAwContents(JNIEnv* env) {
  return RegisterNativesImpl(env) >= 0;
}

jint AwContents::FindAllSync(JNIEnv* env, jobject obj, jstring search_string) {
  return GetFindHelper()->FindAllSync(
      ConvertJavaStringToUTF16(env, search_string));
}

void AwContents::FindAllAsync(JNIEnv* env, jobject obj, jstring search_string) {
  GetFindHelper()->FindAllAsync(ConvertJavaStringToUTF16(env, search_string));
}

void AwContents::FindNext(JNIEnv* env, jobject obj, jboolean forward) {
  GetFindHelper()->FindNext(forward);
}

void AwContents::ClearMatches(JNIEnv* env, jobject obj) {
  GetFindHelper()->ClearMatches();
}

void AwContents::ClearCache(
    JNIEnv* env,
    jobject obj,
    jboolean include_disk_files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  render_view_host_ext_->ClearCache();

  if (include_disk_files) {
    RemoveHttpDiskCache(web_contents_->GetBrowserContext(),
                        web_contents_->GetRoutingID());
  }
}

FindHelper* AwContents::GetFindHelper() {
  if (!find_helper_.get()) {
    find_helper_.reset(new FindHelper(web_contents_.get()));
    find_helper_->SetListener(this);
  }
  return find_helper_.get();
}

void AwContents::OnFindResultReceived(int active_ordinal,
                                      int match_count,
                                      bool finished) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;

  Java_AwContents_onFindResultReceived(
      env, obj.obj(), active_ordinal, match_count, finished);
}

base::android::ScopedJavaLocalRef<jbyteArray>
    AwContents::GetCertificate(JNIEnv* env,
                               jobject obj) {
  content::NavigationEntry* entry =
      web_contents_->GetController().GetActiveEntry();
  if (!entry)
    return ScopedJavaLocalRef<jbyteArray>();
  // Get the certificate
  int cert_id = entry->GetSSL().cert_id;
  scoped_refptr<net::X509Certificate> cert;
  bool ok = content::CertStore::GetInstance()->RetrieveCert(cert_id, &cert);
  if (!ok)
    return ScopedJavaLocalRef<jbyteArray>();

  // Convert the certificate and return it
  std::string der_string;
  net::X509Certificate::GetDEREncoded(cert->os_cert_handle(), &der_string);
  return base::android::ToJavaByteArray(env,
      reinterpret_cast<const uint8*>(der_string.data()), der_string.length());
}

}  // namespace android_webview
