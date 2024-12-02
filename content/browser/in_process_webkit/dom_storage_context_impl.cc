// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/in_process_webkit/dom_storage_context_impl.h"

#ifdef ENABLE_NEW_DOM_STORAGE_BACKEND
// This class is replaced by a new implementation in
// content/browser/dom_storage/dom_storage_context_impl_new.h
#else

#include <algorithm>

#include "base/bind.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/string_util.h"
#include "content/browser/in_process_webkit/dom_storage_area.h"
#include "content/browser/in_process_webkit/dom_storage_namespace.h"
#include "content/common/dom_storage_common.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSecurityOrigin.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebString.h"
#include "webkit/glue/webkit_glue.h"
#include "webkit/quota/special_storage_policy.h"

using content::BrowserThread;
using content::DOMStorageContext;
using WebKit::WebSecurityOrigin;

const FilePath::CharType DOMStorageContextImpl::kLocalStorageDirectory[] =
    FILE_PATH_LITERAL("Local Storage");

const FilePath::CharType DOMStorageContextImpl::kLocalStorageExtension[] =
    FILE_PATH_LITERAL(".localstorage");

namespace {

void ClearLocalState(const FilePath& domstorage_path,
                     quota::SpecialStoragePolicy* special_storage_policy,
                     bool clear_all_databases) {
  file_util::FileEnumerator file_enumerator(
      domstorage_path, false, file_util::FileEnumerator::FILES);
  for (FilePath file_path = file_enumerator.Next(); !file_path.empty();
       file_path = file_enumerator.Next()) {
    if (file_path.Extension() ==
            DOMStorageContextImpl::kLocalStorageExtension) {
      GURL origin(WebSecurityOrigin::createFromDatabaseIdentifier(
          webkit_glue::FilePathToWebString(file_path.BaseName())).toString());
      if (special_storage_policy &&
          special_storage_policy->IsStorageProtected(origin))
        continue;
      if (!clear_all_databases &&
          !special_storage_policy->IsStorageSessionOnly(origin)) {
        continue;
      }
      file_util::Delete(file_path, false);
    }
  }
}

}  // namespace

DOMStorageContextImpl::DOMStorageContextImpl(
    const FilePath& data_path,
    quota::SpecialStoragePolicy* special_storage_policy)
    : last_storage_area_id_(0),
      last_session_storage_namespace_id_on_ui_thread_(kLocalStorageNamespaceId),
      last_session_storage_namespace_id_on_io_thread_(kLocalStorageNamespaceId),
      clear_local_state_on_exit_(false),
      save_session_state_(false),
      special_storage_policy_(special_storage_policy),
      webkit_message_loop_(
          BrowserThread::GetMessageLoopProxyForThread(
              BrowserThread::WEBKIT_DEPRECATED)) {
  data_path_ = data_path;
}

DOMStorageContextImpl::~DOMStorageContextImpl() {
  // This should not go away until all DOM Storage message filters have gone
  // away.  And they remove themselves from this list.
  DCHECK(message_filter_set_.empty());

  for (StorageNamespaceMap::iterator iter(storage_namespace_map_.begin());
       iter != storage_namespace_map_.end(); ++iter) {
    delete iter->second;
  }

  if (save_session_state_)
    return;

  bool has_session_only_databases =
      special_storage_policy_.get() &&
      special_storage_policy_->HasSessionOnlyOrigins();

  // Clearning only session-only databases, and there are none.
  if (!clear_local_state_on_exit_ && !has_session_only_databases)
    return;

  // Not being on the WEBKIT thread here means we are running in a unit test
  // where no clean up is needed.
  if (BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED)) {
    ClearLocalState(data_path_.Append(kLocalStorageDirectory),
                    special_storage_policy_,
                    clear_local_state_on_exit_);
  }
}

int64 DOMStorageContextImpl::AllocateStorageAreaId() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  return ++last_storage_area_id_;
}

int64 DOMStorageContextImpl::AllocateSessionStorageNamespaceId() {
  if (BrowserThread::CurrentlyOn(BrowserThread::UI))
    return ++last_session_storage_namespace_id_on_ui_thread_;
  return --last_session_storage_namespace_id_on_io_thread_;
}

int64 DOMStorageContextImpl::CloneSessionStorage(int64 original_id) {
  DCHECK(!BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  int64 clone_id = AllocateSessionStorageNamespaceId();
  BrowserThread::PostTask(
      BrowserThread::WEBKIT_DEPRECATED, FROM_HERE,
      base::Bind(&DOMStorageContextImpl::CompleteCloningSessionStorage,
                 this, original_id, clone_id));
  return clone_id;
}

void DOMStorageContextImpl::RegisterStorageArea(DOMStorageArea* storage_area) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  int64 id = storage_area->id();
  DCHECK(!GetStorageArea(id));
  storage_area_map_[id] = storage_area;
}

void DOMStorageContextImpl::UnregisterStorageArea(
    DOMStorageArea* storage_area) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  int64 id = storage_area->id();
  DCHECK(GetStorageArea(id));
  storage_area_map_.erase(id);
}

DOMStorageArea* DOMStorageContextImpl::GetStorageArea(int64 id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  StorageAreaMap::iterator iter = storage_area_map_.find(id);
  if (iter == storage_area_map_.end())
    return NULL;
  return iter->second;
}

void DOMStorageContextImpl::DeleteSessionStorageNamespace(int64 namespace_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED) ||
         !BrowserThread::IsMessageLoopValid(BrowserThread::WEBKIT_DEPRECATED));
  StorageNamespaceMap::iterator iter =
      storage_namespace_map_.find(namespace_id);
  if (iter == storage_namespace_map_.end())
    return;
  DCHECK(iter->second->dom_storage_type() == DOM_STORAGE_SESSION);
  delete iter->second;
  storage_namespace_map_.erase(iter);
}

DOMStorageNamespace* DOMStorageContextImpl::GetStorageNamespace(
    int64 id, bool allocation_allowed) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  StorageNamespaceMap::iterator iter = storage_namespace_map_.find(id);
  if (iter != storage_namespace_map_.end())
    return iter->second;
  if (!allocation_allowed)
    return NULL;
  if (id == kLocalStorageNamespaceId)
    return CreateLocalStorage();
  return CreateSessionStorage(id);
}

void DOMStorageContextImpl::RegisterMessageFilter(
    DOMStorageMessageFilter* message_filter) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(message_filter_set_.find(message_filter) ==
         message_filter_set_.end());
  message_filter_set_.insert(message_filter);
}

void DOMStorageContextImpl::UnregisterMessageFilter(
    DOMStorageMessageFilter* message_filter) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(message_filter_set_.find(message_filter) !=
         message_filter_set_.end());
  message_filter_set_.erase(message_filter);
}

const DOMStorageContextImpl::MessageFilterSet*
DOMStorageContextImpl::GetMessageFilterSet() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  return &message_filter_set_;
}

void DOMStorageContextImpl::PurgeMemory() {
  if (!webkit_message_loop_->RunsTasksOnCurrentThread()) {
    webkit_message_loop_->PostTask(
        FROM_HERE,
        base::Bind(&DOMStorageContextImpl::PurgeMemory, this));
    return;
  }

  // It is only safe to purge the memory from the LocalStorage namespace,
  // because it is backed by disk and can be reloaded later.  If we purge a
  // SessionStorage namespace, its data will be gone forever, because it isn't
  // currently backed by disk.
  DOMStorageNamespace* local_storage =
      GetStorageNamespace(kLocalStorageNamespaceId, false);
  if (local_storage)
    local_storage->PurgeMemory();
}

void DOMStorageContextImpl::DeleteDataModifiedSince(const base::Time& cutoff) {
  if (!webkit_message_loop_->RunsTasksOnCurrentThread()) {
    webkit_message_loop_->PostTask(
        FROM_HERE,
        base::Bind(
            &DOMStorageContextImpl::DeleteDataModifiedSince, this, cutoff));
    return;
  }

  // Make sure that we don't delete a database that's currently being accessed
  // by unloading all of the databases temporarily.
  PurgeMemory();

  file_util::FileEnumerator file_enumerator(
      data_path_.Append(kLocalStorageDirectory), false,
      file_util::FileEnumerator::FILES);
  for (FilePath path = file_enumerator.Next(); !path.value().empty();
       path = file_enumerator.Next()) {
    GURL origin(WebSecurityOrigin::createFromDatabaseIdentifier(
        webkit_glue::FilePathToWebString(path.BaseName())).toString());
    if (special_storage_policy_->IsStorageProtected(origin))
      continue;

    file_util::FileEnumerator::FindInfo find_info;
    file_enumerator.GetFindInfo(&find_info);
    if (file_util::HasFileBeenModifiedSince(find_info, cutoff))
      file_util::Delete(path, false);
  }
}

void DOMStorageContextImpl::DeleteLocalStorageFile(const FilePath& file_path) {
  if (!webkit_message_loop_->RunsTasksOnCurrentThread()) {
    webkit_message_loop_->PostTask(
        FROM_HERE,
        base::Bind(
            &DOMStorageContextImpl::DeleteLocalStorageFile, this, file_path));
    return;
  }

  // Make sure that we don't delete a database that's currently being accessed
  // by unloading all of the databases temporarily.
  // TODO(bulach): both this method and DeleteDataModifiedSince could purge
  // only the memory used by the specific file instead of all memory at once.
  // See http://crbug.com/32000
  PurgeMemory();
  file_util::Delete(file_path, false);
}

void DOMStorageContextImpl::DeleteForOrigin(const string16& origin_id) {
  DeleteLocalStorageFile(GetFilePath(origin_id));
}

void DOMStorageContextImpl::DeleteAllLocalStorageFiles() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));

  // Make sure that we don't delete a database that's currently being accessed
  // by unloading all of the databases temporarily.
  PurgeMemory();

  file_util::FileEnumerator file_enumerator(
      data_path_.Append(kLocalStorageDirectory), false,
      file_util::FileEnumerator::FILES);
  for (FilePath file_path = file_enumerator.Next(); !file_path.empty();
       file_path = file_enumerator.Next()) {
    if (file_path.Extension() == kLocalStorageExtension)
      file_util::Delete(file_path, false);
  }
}

void DOMStorageContextImpl::SetClearLocalState(bool clear_local_state) {
  if (!webkit_message_loop_->RunsTasksOnCurrentThread()) {
    webkit_message_loop_->PostTask(
        FROM_HERE,
        base::Bind(
            &DOMStorageContextImpl::SetClearLocalState,
            this, clear_local_state));
    return;
  }
  clear_local_state_on_exit_ = clear_local_state;
}

void DOMStorageContextImpl::SaveSessionState() {
  if (!webkit_message_loop_->RunsTasksOnCurrentThread()) {
    webkit_message_loop_->PostTask(
        FROM_HERE,
        base::Bind(&DOMStorageContextImpl::SaveSessionState, this));
    return;
  }
  save_session_state_ = true;
}

DOMStorageNamespace* DOMStorageContextImpl::CreateLocalStorage() {
  FilePath dir_path;
  if (!data_path_.empty())
    dir_path = data_path_.Append(kLocalStorageDirectory);
  DOMStorageNamespace* new_namespace =
      DOMStorageNamespace::CreateLocalStorageNamespace(this, dir_path);
  RegisterStorageNamespace(new_namespace);
  return new_namespace;
}

DOMStorageNamespace* DOMStorageContextImpl::CreateSessionStorage(
    int64 namespace_id) {
  DOMStorageNamespace* new_namespace =
      DOMStorageNamespace::CreateSessionStorageNamespace(this, namespace_id);
  RegisterStorageNamespace(new_namespace);
  return new_namespace;
}

void DOMStorageContextImpl::RegisterStorageNamespace(
    DOMStorageNamespace* storage_namespace) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  int64 id = storage_namespace->id();
  DCHECK(!GetStorageNamespace(id, false));
  storage_namespace_map_[id] = storage_namespace;
}

void DOMStorageContextImpl::CompleteCloningSessionStorage(
    int64 existing_id, int64 clone_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT_DEPRECATED));
  DOMStorageNamespace* existing_namespace =
      GetStorageNamespace(existing_id, false);
  // If nothing exists, then there's nothing to clone.
  if (existing_namespace)
    RegisterStorageNamespace(existing_namespace->Copy(clone_id));
}

void DOMStorageContextImpl::GetAllStorageFiles(
    const GetAllStorageFilesCallback& callback) {
  if (!webkit_message_loop_->RunsTasksOnCurrentThread()) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    webkit_message_loop_->PostTask(
        FROM_HERE,
        base::Bind(
            &DOMStorageContextImpl::GetAllStorageFiles, this, callback));
    return;
  }

  std::vector<FilePath> files;
  file_util::FileEnumerator file_enumerator(
      data_path_.Append(kLocalStorageDirectory), false,
      file_util::FileEnumerator::FILES);
  for (FilePath file_path = file_enumerator.Next(); !file_path.empty();
       file_path = file_enumerator.Next()) {
    if (file_path.Extension() == kLocalStorageExtension)
      files.push_back(file_path);
  }

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&DOMStorageContextImpl::RunAllStorageFilesCallback,
                 this, files, callback));
}

void DOMStorageContextImpl::RunAllStorageFilesCallback(
    const std::vector<FilePath>& files,
    const GetAllStorageFilesCallback& callback) {
  callback.Run(files);
}

FilePath DOMStorageContextImpl::GetFilePath(const string16& origin_id) const {
  FilePath storage_dir = data_path_.Append(kLocalStorageDirectory);
  FilePath::StringType id = webkit_glue::WebStringToFilePathString(origin_id);
  return storage_dir.Append(id.append(kLocalStorageExtension));
}

#endif  // ENABLE_NEW_DOM_STORAGE_BACKEND
