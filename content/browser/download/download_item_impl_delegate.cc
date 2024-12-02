// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/download/download_item_impl_delegate.h"

#include "base/logging.h"
#include "content/browser/download/download_item_impl.h"

namespace content {

// Infrastructure in DownloadItemImplDelegate to assert invariant that
// delegate always outlives all attached DownloadItemImpls.
DownloadItemImplDelegate::DownloadItemImplDelegate()
    : count_(0) {}

DownloadItemImplDelegate::~DownloadItemImplDelegate() {
  DCHECK_EQ(0, count_);
}

void DownloadItemImplDelegate::Attach() {
  ++count_;
}

void DownloadItemImplDelegate::Detach() {
  DCHECK_LT(0, count_);
  --count_;
}

void DownloadItemImplDelegate::DetermineDownloadTarget(
    DownloadItemImpl* download, const DownloadTargetCallback& callback) {
  // TODO(rdsmith/asanka): Do something useful if forced file path is null.
  FilePath target_path(download->GetForcedFilePath());
  callback.Run(target_path,
               DownloadItem::TARGET_DISPOSITION_OVERWRITE,
               DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS,
               target_path);
}

void DownloadItemImplDelegate::ReadyForDownloadCompletion(
    DownloadItemImpl* download,
    const base::Closure& complete_callback) {
  complete_callback.Run();
}

bool DownloadItemImplDelegate::ShouldOpenDownload(DownloadItemImpl* download) {
  return false;
}

bool DownloadItemImplDelegate::ShouldOpenFileBasedOnExtension(
    const FilePath& path) {
  return false;
}

void DownloadItemImplDelegate::CheckForFileRemoval(
    DownloadItemImpl* download_item) {}

BrowserContext* DownloadItemImplDelegate::GetBrowserContext() const {
  return NULL;
}

DownloadFileManager* DownloadItemImplDelegate::GetDownloadFileManager() {
  return NULL;
}

void DownloadItemImplDelegate::UpdatePersistence(DownloadItemImpl* download) {}

void DownloadItemImplDelegate::DownloadStopped(DownloadItemImpl* download) {}

void DownloadItemImplDelegate::DownloadCompleted(DownloadItemImpl* download) {}

void DownloadItemImplDelegate::DownloadOpened(DownloadItemImpl* download) {}

void DownloadItemImplDelegate::DownloadRemoved(DownloadItemImpl* download) {}

void DownloadItemImplDelegate::DownloadRenamedToIntermediateName(
    DownloadItemImpl* download) {}

void DownloadItemImplDelegate::DownloadRenamedToFinalName(
    DownloadItemImpl* download) {}

void DownloadItemImplDelegate::AssertStateConsistent(
    DownloadItemImpl* download) const {}

}  // namespace content
