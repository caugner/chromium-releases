// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/autofill/autofill_external_delegate.h"
#include "chrome/browser/autofill/autofill_manager.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/favicon/favicon_tab_helper.h"
#include "chrome/browser/history/history_tab_helper.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/password_manager/password_manager.h"
#include "chrome/browser/password_manager/password_manager_delegate_impl.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ssl/ssl_tab_helper.h"
#include "chrome/browser/ui/android/window_android_helper.h"
#include "chrome/browser/ui/autofill/tab_autofill_manager_delegate.h"
#include "chrome/browser/ui/blocked_content/blocked_content_tab_helper.h"
#include "chrome/browser/ui/bookmarks/bookmark_tab_helper.h"
#include "chrome/browser/ui/find_bar/find_tab_helper.h"
#include "chrome/browser/ui/prefs/prefs_tab_helper.h"
#include "chrome/browser/ui/sync/tab_contents_synced_tab_delegate.h"
#include "chrome/browser/ui/tab_contents/core_tab_helper.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "content/public/browser/android/content_view_core.h"
#include "content/public/browser/web_contents.h"

TabContents* TabAndroid::GetOrCreateTabContents(
    content::WebContents* web_contents) {
  TabContents* tab_contents = TabContents::FromWebContents(web_contents);
  if (!tab_contents) {
    tab_contents = TabContents::Factory::CreateTabContents(web_contents);
    InitTabHelpers(web_contents);
  }
  return tab_contents;
}

void TabAndroid::InitTabHelpers(content::WebContents* contents) {
  // TODO(nileshagrawal): Currently this is not used by Chrome for Android,
  // as it uses TabContents. When TabContents is not created for Android,
  // this will be used to initialize all the tab helpers.

  // SessionTabHelper comes first because it sets up the tab ID, and other
  // helpers may rely on that.
  SessionTabHelper::CreateForWebContents(contents);

  TabAutofillManagerDelegate::CreateForWebContents(contents);
  AutofillManager::CreateForWebContentsAndDelegate(
      contents, TabAutofillManagerDelegate::FromWebContents(contents));
  AutofillExternalDelegate::CreateForWebContentsAndManager(
      contents, AutofillManager::FromWebContents(contents));
  AutofillManager::FromWebContents(contents)->SetExternalDelegate(
      AutofillExternalDelegate::FromWebContents(contents));
  BlockedContentTabHelper::CreateForWebContents(contents);
  BookmarkTabHelper::CreateForWebContents(contents);
  CoreTabHelper::CreateForWebContents(contents);
  FaviconTabHelper::CreateForWebContents(contents);
  FindTabHelper::CreateForWebContents(contents);
  HistoryTabHelper::CreateForWebContents(contents);
  InfoBarTabHelper::CreateForWebContents(contents);
  PasswordManagerDelegateImpl::CreateForWebContents(contents);
  PasswordManager::CreateForWebContentsAndDelegate(
      contents, PasswordManagerDelegateImpl::FromWebContents(contents));
  PrefsTabHelper::CreateForWebContents(contents);
  SSLTabHelper::CreateForWebContents(contents);
  TabContentsSyncedTabDelegate::CreateForWebContents(contents);
  TabSpecificContentSettings::CreateForWebContents(contents);
  WindowAndroidHelper::CreateForWebContents(contents);
}

TabContents* TabAndroid::InitTabContentsFromView(JNIEnv* env,
                                                 jobject content_view) {
  content::ContentViewCore* content_view_core =
      content::ContentViewCore::GetNativeContentViewCore(env, content_view);
  DCHECK(content_view_core);
  DCHECK(content_view_core->GetWebContents());
  return GetOrCreateTabContents(content_view_core->GetWebContents());
}

TabAndroid::TabAndroid() : tab_id_(-1) {
}

TabAndroid::~TabAndroid() {
}

void TabAndroid::RunExternalProtocolDialog(const GURL& url) {
}
