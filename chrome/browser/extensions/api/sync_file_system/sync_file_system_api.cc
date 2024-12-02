// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/sync_file_system/sync_file_system_api.h"

#include <string>

#include "base/bind.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync_file_system/sync_file_system_service.h"
#include "chrome/common/extensions/api/sync_file_system.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_client.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/quota/quota_manager.h"

using content::BrowserContext;
using content::BrowserThread;
using sync_file_system::SyncFileSystemServiceFactory;

namespace extensions {

namespace {

// This is the only supported cloud backend service for now.
const char kDriveCloudService[] = "drive";

// Error messages.
const char kNotSupportedService[] = "Cloud service %s not supported.";
const char kFileError[] = "File error %d.";
const char kQuotaError[] = "Quota error %d.";

}  // namespace

bool SyncFileSystemRequestFileSystemFunction::RunImpl() {
  std::string service_name;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &service_name));

  if (service_name != std::string(kDriveCloudService)) {
    error_ = base::StringPrintf(kNotSupportedService, service_name.c_str());
    return false;
  }

  // TODO(kinuko): Set up sync service, see if the user is signed in
  // and can access the service (i.e. Drive).

  // Initializes sync context for this extension and continue to open
  // a new file system.
  SyncFileSystemServiceFactory::GetForProfile(profile())->
      InitializeForApp(
          GetFileSystemContext(),
          service_name,
          source_url(),
          base::Bind(&self::DidInitializeFileSystemContext, this,
                     service_name));
  return true;
}

fileapi::FileSystemContext*
SyncFileSystemRequestFileSystemFunction::GetFileSystemContext() {
  return BrowserContext::GetStoragePartition(
      profile(),
      render_view_host()->GetSiteInstance())->GetFileSystemContext();
}

void SyncFileSystemRequestFileSystemFunction::DidInitializeFileSystemContext(
    const std::string& service_name,
    fileapi::SyncStatusCode status) {
  if (status != fileapi::SYNC_STATUS_OK) {
    // TODO(kinuko): Fix this. (http://crbug.com/153757)
    error_ = base::StringPrintf(kFileError, static_cast<int>(status));
    SendResponse(false);
    return;
  }
  GetFileSystemContext()->OpenSyncableFileSystem(
          service_name,
          source_url(),
          fileapi::kFileSystemTypeSyncable,
          true, /* create */
          base::Bind(&self::DidOpenFileSystem, this));
}

void SyncFileSystemRequestFileSystemFunction::DidOpenFileSystem(
    base::PlatformFileError error,
    const std::string& file_system_name,
    const GURL& root_url) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (error != base::PLATFORM_FILE_OK) {
    error_ = base::StringPrintf(kFileError, static_cast<int>(error));
    SendResponse(false);
    return;
  }

  DictionaryValue* dict = new DictionaryValue();
  SetResult(dict);
  dict->SetString("name", file_system_name);
  dict->SetString("root", root_url.spec());
  SendResponse(true);
}

bool SyncFileSystemGetUsageAndQuotaFunction::RunImpl() {
  std::string service_name;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &service_name));

  // TODO(calvinlo): For now only gDrive cloud service is supported.
  if (service_name != std::string(kDriveCloudService)) {
    error_ = base::StringPrintf(kNotSupportedService, service_name.c_str());
    return false;
  }

  scoped_refptr<quota::QuotaManager> quota_manager =
      BrowserContext::GetStoragePartition(
          profile(),
          render_view_host()->GetSiteInstance())->GetQuotaManager();

  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      Bind(&quota::QuotaManager::GetUsageAndQuota,
           quota_manager,
           source_url(),
           quota::kStorageTypeSyncable,
           Bind(&SyncFileSystemGetUsageAndQuotaFunction::DidGetUsageAndQuota,
                this)));

  return true;
}

void SyncFileSystemGetUsageAndQuotaFunction::DidGetUsageAndQuota(
      quota::QuotaStatusCode status, int64 usage, int64 quota) {
  // Repost to switch from IO thread to UI thread for SendResponse().
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        Bind(&SyncFileSystemGetUsageAndQuotaFunction::DidGetUsageAndQuota, this,
             status, usage, quota));
    return;
  }

  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // TODO(calvinlo): Convert QuotaStatusCode to error string
  // (http://crbug.com/156791).
  if (status != quota::kQuotaStatusOk) {
    error_ = base::StringPrintf(kQuotaError, static_cast<int>(status));
    SendResponse(false);
    return;
  }

  api::sync_file_system::StorageInfo info;
  info.usage_bytes = usage;
  info.quota_bytes = quota;
  results_ = api::sync_file_system::GetUsageAndQuota::Results::Create(info);
  SendResponse(true);
}

}  // namespace extensions
