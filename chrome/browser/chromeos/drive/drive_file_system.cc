// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/drive_file_system.h"

#include "base/bind.h"
#include "base/file_util.h"
#include "base/json/json_file_value_serializer.h"
#include "base/message_loop_proxy.h"
#include "base/metrics/histogram.h"
#include "base/platform_file.h"
#include "base/prefs/public/pref_change_registrar.h"
#include "base/stringprintf.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/values.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/drive_feed_loader.h"
#include "chrome/browser/chromeos/drive/drive_feed_processor.h"
#include "chrome/browser/chromeos/drive/drive_file_system_observer.h"
#include "chrome/browser/chromeos/drive/drive_file_system_util.h"
#include "chrome/browser/chromeos/drive/drive_files.h"
#include "chrome/browser/chromeos/drive/drive_scheduler.h"
#include "chrome/browser/chromeos/drive/file_system/copy_operation.h"
#include "chrome/browser/chromeos/drive/file_system/move_operation.h"
#include "chrome/browser/chromeos/drive/file_system/remove_operation.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/drive_service_interface.h"
#include "chrome/browser/google_apis/drive_uploader.h"
#include "chrome/browser/google_apis/gdata_util.h"
#include "chrome/browser/google_apis/task_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_details.h"
#include "net/base/mime_util.h"

using content::BrowserThread;

namespace drive {
namespace {

const char kMimeTypeJson[] = "application/json";
const char kEmptyFilePath[] = "/dev/null";

// Drive update polling interval for polling only mode (in seconds).
const int kFastPollingIntervalInSec = 60;

// Drive update polling interval when update notification is available (in
// seconds). Ideally we don't need this, but we do polling in case update
// notification doesn't work. http://crbug.com/157080
const int kSlowPollingIntervalInSec = 300;

//================================ Helper functions ============================

// Runs GetFileCallback with pointers dereferenced.
// Used for PostTaskAndReply().
void RunGetFileCallbackHelper(const GetFileCallback& callback,
                              DriveFileError* error,
                              FilePath* file_path,
                              std::string* mime_type,
                              DriveFileType* file_type) {
  DCHECK(error);
  DCHECK(file_path);
  DCHECK(mime_type);
  DCHECK(file_type);

  if (!callback.is_null())
    callback.Run(*error, *file_path, *mime_type, *file_type);
}

// Callback for cache file operations invoked by AddUploadedFileOnUIThread.
void OnCacheUpdatedForAddUploadedFile(
    const base::Closure& callback,
    DriveFileError /* error */,
    const std::string& /* resource_id */,
    const std::string& /* md5 */) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!callback.is_null())
    callback.Run();
}

// Helper function called upon completion of AddUploadFile invoked by
// OnTransferCompleted.
void OnAddUploadFileCompleted(
    const FileOperationCallback& callback,
    DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!callback.is_null())
    callback.Run(error);
}

// The class to wait for the initial load of root feed and runs the callback
// after the initialization.
class InitialLoadObserver : public DriveFileSystemObserver {
 public:
  InitialLoadObserver(DriveFileSystemInterface* file_system,
                      const FileOperationCallback& callback)
      : file_system_(file_system), callback_(callback) {
    DCHECK(!callback.is_null());
    file_system_->AddObserver(this);
  }

  virtual void OnInitialLoadFinished(DriveFileError error) OVERRIDE {
    base::MessageLoopProxy::current()->PostTask(FROM_HERE,
        base::Bind(callback_, error));
    file_system_->RemoveObserver(this);
    base::MessageLoopProxy::current()->DeleteSoon(FROM_HERE, this);
  }

 private:
  DriveFileSystemInterface* const file_system_;
  const FileOperationCallback callback_;
};

// The class to wait for the drive service to be ready to start operation.
class OperationReadinessObserver : public google_apis::DriveServiceObserver {
 public:
  OperationReadinessObserver(google_apis::DriveServiceInterface* drive_service,
                             const base::Closure& callback)
      : drive_service_(drive_service),
        callback_(callback) {
    DCHECK(!callback_.is_null());
    drive_service_->AddObserver(this);
  }

  // DriveServiceObserver override.
  virtual void OnReadyToPerformOperations() OVERRIDE {
    base::MessageLoopProxy::current()->PostTask(FROM_HERE, callback_);
    drive_service_->RemoveObserver(this);
    base::MessageLoopProxy::current()->DeleteSoon(FROM_HERE, this);
  }

 private:
  google_apis::DriveServiceInterface* drive_service_;
  base::Closure callback_;

  DISALLOW_COPY_AND_ASSIGN(OperationReadinessObserver);
};

// Called when LoadFeedIfNeeded() call from StartInitialFeedFetch() finishes.
void OnStartInitialFeedFetchFinished(DriveFileError error) {
  DVLOG(1) << "Loading from StartInitialFeedFetch() finished";
}

// Gets the file size of |local_file|.
void GetLocalFileSizeOnBlockingPool(const FilePath& local_file,
                                    DriveFileError* error,
                                    int64* file_size) {
  DCHECK(error);
  DCHECK(file_size);

  *file_size = 0;
  *error = file_util::GetFileSize(local_file, file_size) ?
      DRIVE_FILE_OK :
      DRIVE_FILE_ERROR_NOT_FOUND;
}

// Creates a temporary JSON file representing a document with |edit_url|
// and |resource_id| under |document_dir| on blocking pool.
void CreateDocumentJsonFileOnBlockingPool(
    const FilePath& document_dir,
    const GURL& edit_url,
    const std::string& resource_id,
    DriveFileError* error,
    FilePath* temp_file_path,
    std::string* mime_type,
    DriveFileType* file_type) {
  DCHECK(error);
  DCHECK(temp_file_path);
  DCHECK(mime_type);
  DCHECK(file_type);

  *error = DRIVE_FILE_ERROR_FAILED;

  if (file_util::CreateTemporaryFileInDir(document_dir, temp_file_path)) {
    std::string document_content = base::StringPrintf(
        "{\"url\": \"%s\", \"resource_id\": \"%s\"}",
        edit_url.spec().c_str(), resource_id.c_str());
    int document_size = static_cast<int>(document_content.size());
    if (file_util::WriteFile(*temp_file_path, document_content.data(),
                             document_size) == document_size) {
      *error = DRIVE_FILE_OK;
    }
  }

  *mime_type = kMimeTypeJson;
  *file_type = HOSTED_DOCUMENT;
  if (*error != DRIVE_FILE_OK)
      temp_file_path->clear();
}

// Gets the information of the file at local path |path|. The information is
// filled in |file_info|, and if it fails |result| will be assigned false.
void GetFileInfoOnBlockingPool(const FilePath& path,
                               base::PlatformFileInfo* file_info,
                               bool* result) {
  *result = file_util::GetFileInfo(path, file_info);
}

// Helper function for binding |path| to GetEntryInfoWithFilePathCallback and
// create GetEntryInfoCallback.
void RunGetEntryInfoWithFilePathCallback(
    const GetEntryInfoWithFilePathCallback& callback,
    const FilePath& path,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto) {
  if (!callback.is_null())
    callback.Run(error, path, entry_proto.Pass());
}

}  // namespace

// DriveFileSystem::FindFirstMissingParentDirectoryParams implementation.
struct DriveFileSystem::FindFirstMissingParentDirectoryParams {
  FindFirstMissingParentDirectoryParams(
      const std::vector<FilePath::StringType>& path_parts,
      const FindFirstMissingParentDirectoryCallback& callback)
      : path_parts(path_parts),
        index(0),
        callback(callback) {
    DCHECK(!callback.is_null());
  }
  ~FindFirstMissingParentDirectoryParams() {}

  std::vector<FilePath::StringType> path_parts;
  size_t index;
  FilePath current_path;
  GURL last_dir_content_url;
  const FindFirstMissingParentDirectoryCallback callback;
};

// DriveFileSystem::FindFirstMissingParentDirectoryResult implementation.
DriveFileSystem::FindFirstMissingParentDirectoryResult::
FindFirstMissingParentDirectoryResult()
    : error(DriveFileSystem::FIND_FIRST_FOUND_INVALID) {
}

void DriveFileSystem::FindFirstMissingParentDirectoryResult::Init(
    FindFirstMissingParentDirectoryError in_error,
    FilePath in_first_missing_parent_path,
    GURL in_last_dir_content_url) {
  error = in_error;
  first_missing_parent_path = in_first_missing_parent_path;
  last_dir_content_url = in_last_dir_content_url;
}

DriveFileSystem::FindFirstMissingParentDirectoryResult::
~FindFirstMissingParentDirectoryResult() {
}

// DriveFileSystem::CreateDirectoryParams struct implementation.
DriveFileSystem::CreateDirectoryParams::CreateDirectoryParams(
    const FilePath& created_directory_path,
    const FilePath& target_directory_path,
    bool is_exclusive,
    bool is_recursive,
    const FileOperationCallback& callback)
    : created_directory_path(created_directory_path),
      target_directory_path(target_directory_path),
      is_exclusive(is_exclusive),
      is_recursive(is_recursive),
      callback(callback) {
  DCHECK(!callback.is_null());
}

DriveFileSystem::CreateDirectoryParams::~CreateDirectoryParams() {
}

// DriveFileSystem::GetFileCompleteForOpenParams struct implementation.
struct DriveFileSystem::GetFileCompleteForOpenParams {
  GetFileCompleteForOpenParams(const std::string& resource_id,
                               const std::string& md5);
  std::string resource_id;
  std::string md5;
};

DriveFileSystem::GetFileCompleteForOpenParams::GetFileCompleteForOpenParams(
    const std::string& resource_id,
    const std::string& md5)
    : resource_id(resource_id),
      md5(md5) {
}

// DriveFileSystem::GetFileFromCacheParams struct implementation.
struct DriveFileSystem::GetFileFromCacheParams {
  GetFileFromCacheParams(
      const FilePath& virtual_file_path,
      const FilePath& local_tmp_path,
      const GURL& content_url,
      const std::string& resource_id,
      const std::string& md5,
      const std::string& mime_type,
      const GetFileCallback& get_file_callback,
      const google_apis::GetContentCallback& get_content_callback)
      : virtual_file_path(virtual_file_path),
        local_tmp_path(local_tmp_path),
        content_url(content_url),
        resource_id(resource_id),
        md5(md5),
        mime_type(mime_type),
        get_file_callback(get_file_callback),
        get_content_callback(get_content_callback) {
  }

  FilePath virtual_file_path;
  FilePath local_tmp_path;
  FilePath cache_file_path;
  GURL content_url;
  std::string resource_id;
  std::string md5;
  std::string mime_type;
  GetFileCallback get_file_callback;
  google_apis::GetContentCallback get_content_callback;
};

// DriveFileSystem::AddUploadedFileParams implementation.
struct DriveFileSystem::AddUploadedFileParams {
  AddUploadedFileParams(google_apis::UploadMode upload_mode,
                        const FilePath& directory_path,
                        scoped_ptr<google_apis::DocumentEntry> doc_entry,
                        const FilePath& file_content_path,
                        DriveCache::FileOperationType cache_operation,
                        const base::Closure& callback)
  : upload_mode(upload_mode),
    directory_path(directory_path),
    doc_entry(doc_entry.Pass()),
    file_content_path(file_content_path),
    cache_operation(cache_operation),
    callback(callback) {
  }

  google_apis::UploadMode upload_mode;
  FilePath directory_path;
  scoped_ptr<google_apis::DocumentEntry> doc_entry;
  FilePath file_content_path;
  DriveCache::FileOperationType cache_operation;
  base::Closure callback;
  std::string resource_id;
  std::string md5;
};

// DriveFileSystem::UpdateEntryParams implementation.
struct DriveFileSystem::UpdateEntryParams {
  UpdateEntryParams(const std::string& resource_id,
                    const std::string& md5,
                    const FilePath& file_content_path,
                    const base::Closure& callback)
                    : resource_id(resource_id),
                      md5(md5),
                      file_content_path(file_content_path),
                      callback(callback) {
  }

  std::string resource_id;
  std::string md5;
  FilePath file_content_path;
  base::Closure callback;
};


// DriveFileSystem class implementation.

DriveFileSystem::DriveFileSystem(
    Profile* profile,
    DriveCache* cache,
    google_apis::DriveServiceInterface* drive_service,
    google_apis::DriveUploaderInterface* uploader,
    DriveWebAppsRegistryInterface* webapps_registry,
    base::SequencedTaskRunner* blocking_task_runner)
    : profile_(profile),
      cache_(cache),
      uploader_(uploader),
      drive_service_(drive_service),
      webapps_registry_(webapps_registry),
      update_timer_(true /* retain_user_task */, true /* is_repeating */),
      hide_hosted_docs_(false),
      blocking_task_runner_(blocking_task_runner),
      scheduler_(new DriveScheduler(profile, &drive_operations_)),
      ALLOW_THIS_IN_INITIALIZER_LIST(ui_weak_ptr_factory_(this)),
      ui_weak_ptr_(ui_weak_ptr_factory_.GetWeakPtr()),
      polling_interval_sec_(kFastPollingIntervalInSec) {
  // Should be created from the file browser extension API on UI thread.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

void DriveFileSystem::Reload() {
  InitializeResourceMetadataAndFeedLoader();

  feed_loader_->ReloadFromServerIfNeeded(
      base::Bind(&DriveFileSystem::NotifyInitialLoadFinishedAndRun,
                 ui_weak_ptr_,
                 base::Bind(&DriveFileSystem::OnUpdateChecked,
                            ui_weak_ptr_)));
}

void DriveFileSystem::Initialize() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  drive_service_->Initialize(profile_);

  InitializeResourceMetadataAndFeedLoader();

  // Allocate the drive operation handlers.
  drive_operations_.Init(drive_service_,
                         this,  // DriveFileSystemInterface
                         cache_,
                         resource_metadata_.get(),
                         uploader_,
                         blocking_task_runner_,
                         this);  // OperationObserver

  PrefService* pref_service = profile_->GetPrefs();
  hide_hosted_docs_ = pref_service->GetBoolean(prefs::kDisableDriveHostedFiles);

  scheduler_->Initialize();

  InitializePreferenceObserver();
}

void DriveFileSystem::InitializeResourceMetadataAndFeedLoader() {
  resource_metadata_.reset(new DriveResourceMetadata);
  feed_loader_.reset(new DriveFeedLoader(resource_metadata_.get(),
                                         drive_service_,
                                         webapps_registry_,
                                         cache_,
                                         blocking_task_runner_));
  feed_loader_->AddObserver(this);
}

void DriveFileSystem::CheckForUpdates() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DVLOG(1) << "CheckForUpdates";

  if (resource_metadata_->origin() == INITIALIZED &&
      !feed_loader_->refreshing()) {
    feed_loader_->ReloadFromServerIfNeeded(
        base::Bind(&DriveFileSystem::OnUpdateChecked, ui_weak_ptr_));
  }
}

void DriveFileSystem::OnUpdateChecked(DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DVLOG(1) << "CheckForUpdates finished: " << error;
}

DriveFileSystem::~DriveFileSystem() {
  // This should be called from UI thread, from DriveSystemService shutdown.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  feed_loader_->RemoveObserver(this);

  // Cancel all the in-flight operations.
  // This asynchronously cancels the URL fetch operations.
  drive_service_->CancelAll();
}

void DriveFileSystem::AddObserver(DriveFileSystemObserver* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  observers_.AddObserver(observer);
}

void DriveFileSystem::RemoveObserver(DriveFileSystemObserver* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  observers_.RemoveObserver(observer);
}

void DriveFileSystem::StartInitialFeedFetch() {
  if (drive_service_->CanStartOperation()) {
    LoadFeedIfNeeded(base::Bind(&OnStartInitialFeedFetchFinished));
  } else {
    // Wait for the service to get ready. The observer deletes itself after
    // OnReadyToPerformOperations() gets called.
    new OperationReadinessObserver(
        drive_service_,
        base::Bind(&DriveFileSystem::LoadFeedIfNeeded,
                   ui_weak_ptr_,
                   base::Bind(&OnStartInitialFeedFetchFinished)));
  }
}

void DriveFileSystem::StartPolling() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DCHECK(!update_timer_.IsRunning());
  update_timer_.Start(FROM_HERE,
                      base::TimeDelta::FromSeconds(polling_interval_sec_),
                      base::Bind(&DriveFileSystem::CheckForUpdates,
                                 ui_weak_ptr_));
}

void DriveFileSystem::StopPolling() {
  // If unmount request comes from filesystem side, this method may be called
  // twice. First is just after unmounting on filesystem, second is after
  // unmounting on filemanager on JS. In other words, if this is called from
  // DriveSystemService::RemoveDriveMountPoint(), this will be called again from
  // FileBrowserEventRouter::HandleRemoteUpdateRequestOnUIThread().
  // We choose to stopping updates asynchronous without waiting for filemanager,
  // rather than waiting for completion of unmounting on filemanager.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (update_timer_.IsRunning())
    update_timer_.Stop();
}

void DriveFileSystem::SetPushNotificationEnabled(bool enabled) {
  polling_interval_sec_ = enabled ? kSlowPollingIntervalInSec :
                          kFastPollingIntervalInSec;
}

void DriveFileSystem::GetEntryInfoByResourceId(
    const std::string& resource_id,
    const GetEntryInfoWithFilePathCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::GetEntryInfoByResourceIdOnUIThread,
                 ui_weak_ptr_,
                 resource_id,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::GetEntryInfoByResourceIdOnUIThread(
    const std::string& resource_id,
    const GetEntryInfoWithFilePathCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  resource_metadata_->GetEntryInfoByResourceId(
      resource_id,
      base::Bind(&DriveFileSystem::GetEntryInfoByResourceIdAfterGetEntry,
                 ui_weak_ptr_,
                 callback));
}

void DriveFileSystem::GetEntryInfoByResourceIdAfterGetEntry(
    const GetEntryInfoWithFilePathCallback& callback,
    DriveFileError error,
    const FilePath& file_path,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error, FilePath(), scoped_ptr<DriveEntryProto>());
    return;
  }
  DCHECK(entry_proto.get());

  CheckLocalModificationAndRun(
      entry_proto.Pass(),
      base::Bind(&RunGetEntryInfoWithFilePathCallback,
                 callback,
                 file_path));
}

void DriveFileSystem::LoadFeedIfNeeded(const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (resource_metadata_->origin() == INITIALIZED) {
    // The feed has already been loaded, so we have nothing to do, but post a
    // task to the same thread, rather than calling it here, as
    // LoadFeedIfNeeded() is asynchronous.
    base::MessageLoopProxy::current()->PostTask(
        FROM_HERE,
        base::Bind(callback, DRIVE_FILE_OK));
    return;
  }

  if (feed_loader_->refreshing()) {
    // If root feed is not initialized but the initialization process has
    // already started, add an observer to execute the remaining task after
    // the end of the initialization.
    // The observer deletes itself after OnInitialLoadFinished() gets called.
    new InitialLoadObserver(this, callback);
    return;
  }

  // Load root feed from the disk cache.
  feed_loader_->LoadFromCache(base::Bind(&DriveFileSystem::OnFeedCacheLoaded,
                                         ui_weak_ptr_,
                                         callback));
}

void DriveFileSystem::TransferFileFromRemoteToLocal(
    const FilePath& remote_src_file_path,
    const FilePath& local_dest_file_path,
    const FileOperationCallback& callback) {

  drive_operations_.TransferFileFromRemoteToLocal(remote_src_file_path,
                                                  local_dest_file_path,
                                                  callback);
}

void DriveFileSystem::TransferFileFromLocalToRemote(
    const FilePath& local_src_file_path,
    const FilePath& remote_dest_file_path,
    const FileOperationCallback& callback) {

  drive_operations_.TransferFileFromLocalToRemote(local_src_file_path,
                                                  remote_dest_file_path,
                                                  callback);
}

void DriveFileSystem::Copy(const FilePath& src_file_path,
                           const FilePath& dest_file_path,
                           const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::CopyOnUIThread,
                 ui_weak_ptr_,
                 src_file_path,
                 dest_file_path,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::CopyOnUIThread(const FilePath& src_file_path,
                                     const FilePath& dest_file_path,
                                     const FileOperationCallback& callback) {
  drive_operations_.Copy(src_file_path, dest_file_path, callback);
}

void DriveFileSystem::Move(const FilePath& src_file_path,
                           const FilePath& dest_file_path,
                           const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::MoveOnUIThread,
                 ui_weak_ptr_,
                 src_file_path,
                 dest_file_path,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::MoveOnUIThread(const FilePath& src_file_path,
                                     const FilePath& dest_file_path,
                                     const FileOperationCallback& callback) {
  drive_operations_.Move(src_file_path, dest_file_path, callback);
}

void DriveFileSystem::Remove(const FilePath& file_path,
    bool is_recursive,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::RemoveOnUIThread,
                 ui_weak_ptr_,
                 file_path,
                 is_recursive,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::RemoveOnUIThread(
    const FilePath& file_path,
    bool is_recursive,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  scheduler_->Remove(file_path, is_recursive, callback);
}

void DriveFileSystem::CreateDirectory(
    const FilePath& directory_path,
    bool is_exclusive,
    bool is_recursive,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::CreateDirectoryOnUIThread,
                 ui_weak_ptr_,
                 directory_path,
                 is_exclusive,
                 is_recursive,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::CreateDirectoryOnUIThread(
    const FilePath& directory_path,
    bool is_exclusive,
    bool is_recursive,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  FindFirstMissingParentDirectory(
      directory_path,
      base::Bind(&DriveFileSystem::CreateDirectoryAfterFindFirstMissingPath,
                 ui_weak_ptr_,
                 directory_path,
                 is_exclusive,
                 is_recursive,
                 callback));
}

void DriveFileSystem::CreateDirectoryAfterFindFirstMissingPath(
    const FilePath& directory_path,
    bool is_exclusive,
    bool is_recursive,
    const FileOperationCallback& callback,
    const FindFirstMissingParentDirectoryResult& result) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  switch (result.error) {
    case FIND_FIRST_FOUND_INVALID: {
      callback.Run(DRIVE_FILE_ERROR_NOT_FOUND);
      return;
    }
    case FIND_FIRST_DIRECTORY_ALREADY_PRESENT: {
      callback.Run(is_exclusive ? DRIVE_FILE_ERROR_EXISTS : DRIVE_FILE_OK);
      return;
    }
    case FIND_FIRST_FOUND_MISSING: {
      // There is a missing folder to be created here, move on with the rest of
      // this function.
      break;
    }
    default: {
      NOTREACHED();
      break;
    }
  }

  // Do we have a parent directory here as well? We can't then create target
  // directory if this is not a recursive operation.
  if (directory_path !=  result.first_missing_parent_path && !is_recursive) {
    callback.Run(DRIVE_FILE_ERROR_NOT_FOUND);
    return;
  }

  drive_service_->CreateDirectory(
      result.last_dir_content_url,
      result.first_missing_parent_path.BaseName().value(),
      base::Bind(&DriveFileSystem::AddNewDirectory,
                 ui_weak_ptr_,
                 CreateDirectoryParams(
                     result.first_missing_parent_path,
                     directory_path,
                     is_exclusive,
                     is_recursive,
                     callback)));
}

void DriveFileSystem::CreateFile(const FilePath& file_path,
                                 bool is_exclusive,
                                 const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::CreateFileOnUIThread,
                 ui_weak_ptr_,
                 file_path,
                 is_exclusive,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::CreateFileOnUIThread(
    const FilePath& file_path,
    bool is_exclusive,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  // First, checks the existence of a file at |file_path|.
  resource_metadata_->GetEntryInfoByPath(
      file_path,
      base::Bind(&DriveFileSystem::OnGetEntryInfoForCreateFile,
                 ui_weak_ptr_,
                 file_path,
                 is_exclusive,
                 callback));
}

void DriveFileSystem::OnGetEntryInfoForCreateFile(
    const FilePath& file_path,
    bool is_exclusive,
    const FileOperationCallback& callback,
    DriveFileError result,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  // The |file_path| is invalid. It is an error.
  if (result != DRIVE_FILE_ERROR_NOT_FOUND &&
      result != DRIVE_FILE_OK) {
    callback.Run(result);
    return;
  }

  // An entry already exists at |file_path|.
  if (result == DRIVE_FILE_OK) {
    DCHECK(entry_proto.get());
    // If an exclusive mode is requested, or the entry is not a regular file,
    // it is an error.
    if (is_exclusive ||
        entry_proto->file_info().is_directory() ||
        entry_proto->file_specific_info().is_hosted_document()) {
      callback.Run(DRIVE_FILE_ERROR_EXISTS);
      return;
    }

    // Otherwise nothing more to do. Succeeded.
    callback.Run(DRIVE_FILE_OK);
    return;
  }

  // No entry found at |file_path|. Let's create a brand new file.
  // For now, it is implemented by uploading an empty file (/dev/null).
  // TODO(kinaba): http://crbug.com/135143. Implement in a nicer way.
  drive_operations_.TransferRegularFile(FilePath(kEmptyFilePath),
                                        file_path,
                                        callback);
}

void DriveFileSystem::GetFileByPath(
    const FilePath& file_path,
    const GetFileCallback& get_file_callback,
    const google_apis::GetContentCallback& get_content_callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!get_file_callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::GetFileByPathOnUIThread,
                 ui_weak_ptr_,
                 file_path,
                 google_apis::CreateRelayCallback(get_file_callback),
                 google_apis::CreateRelayCallback(get_content_callback)));
}

void DriveFileSystem::GetFileByPathOnUIThread(
    const FilePath& file_path,
    const GetFileCallback& get_file_callback,
    const google_apis::GetContentCallback& get_content_callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!get_file_callback.is_null());

  resource_metadata_->GetEntryInfoByPath(
      file_path,
      base::Bind(&DriveFileSystem::OnGetEntryInfoCompleteForGetFileByPath,
                 ui_weak_ptr_,
                 file_path,
                 google_apis::CreateRelayCallback(get_file_callback),
                 google_apis::CreateRelayCallback(get_content_callback)));
}

void DriveFileSystem::OnGetEntryInfoCompleteForGetFileByPath(
    const FilePath& file_path,
    const GetFileCallback& get_file_callback,
    const google_apis::GetContentCallback& get_content_callback,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!get_file_callback.is_null());

  if (error != DRIVE_FILE_OK) {
    get_file_callback.Run(error, FilePath(), std::string(), REGULAR_FILE);
    return;
  }
  DCHECK(entry_proto.get());

  GetResolvedFileByPath(file_path,
                        get_file_callback,
                        get_content_callback,
                        entry_proto.Pass());
}

void DriveFileSystem::GetResolvedFileByPath(
    const FilePath& file_path,
    const GetFileCallback& get_file_callback,
    const google_apis::GetContentCallback& get_content_callback,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!get_file_callback.is_null());
  DCHECK(entry_proto.get());

  if (!entry_proto->has_file_specific_info()) {
    get_file_callback.Run(DRIVE_FILE_ERROR_NOT_FOUND,
                          FilePath(),
                          std::string(),
                          REGULAR_FILE);
    return;
  }

  // For a hosted document, we create a special JSON file to represent the
  // document instead of fetching the document content in one of the exported
  // formats. The JSON file contains the edit URL and resource ID of the
  // document.
  if (entry_proto->file_specific_info().is_hosted_document()) {
    DriveFileError* error =
        new DriveFileError(DRIVE_FILE_OK);
    FilePath* temp_file_path = new FilePath;
    std::string* mime_type = new std::string;
    DriveFileType* file_type = new DriveFileType(REGULAR_FILE);
    google_apis::util::PostBlockingPoolSequencedTaskAndReply(
        FROM_HERE,
        blocking_task_runner_,
        base::Bind(&CreateDocumentJsonFileOnBlockingPool,
                   cache_->GetCacheDirectoryPath(
                       DriveCache::CACHE_TYPE_TMP_DOCUMENTS),
                   GURL(entry_proto->file_specific_info().alternate_url()),
                   entry_proto->resource_id(),
                   error,
                   temp_file_path,
                   mime_type,
                   file_type),
        base::Bind(&RunGetFileCallbackHelper,
                   get_file_callback,
                   base::Owned(error),
                   base::Owned(temp_file_path),
                   base::Owned(mime_type),
                   base::Owned(file_type)));
    return;
  }

  // Returns absolute path of the file if it were cached or to be cached.
  FilePath local_tmp_path = cache_->GetCacheFilePath(
      entry_proto->resource_id(),
      entry_proto->file_specific_info().file_md5(),
      DriveCache::CACHE_TYPE_TMP,
      DriveCache::CACHED_FILE_FROM_SERVER);
  cache_->GetFileOnUIThread(
      entry_proto->resource_id(),
      entry_proto->file_specific_info().file_md5(),
      base::Bind(
          &DriveFileSystem::OnGetFileFromCache,
          ui_weak_ptr_,
          GetFileFromCacheParams(
              file_path,
              local_tmp_path,
              GURL(entry_proto->content_url()),
              entry_proto->resource_id(),
              entry_proto->file_specific_info().file_md5(),
              entry_proto->file_specific_info().content_mime_type(),
              get_file_callback,
              get_content_callback)));
}

void DriveFileSystem::GetFileByResourceId(
    const std::string& resource_id,
    const GetFileCallback& get_file_callback,
    const google_apis::GetContentCallback& get_content_callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::GetFileByResourceIdOnUIThread,
                 ui_weak_ptr_,
                 resource_id,
                 google_apis::CreateRelayCallback(get_file_callback),
                 google_apis::CreateRelayCallback(get_content_callback)));
}

void DriveFileSystem::GetFileByResourceIdOnUIThread(
    const std::string& resource_id,
    const GetFileCallback& get_file_callback,
    const google_apis::GetContentCallback& get_content_callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!get_file_callback.is_null());

  resource_metadata_->GetEntryInfoByResourceId(
      resource_id,
      base::Bind(&DriveFileSystem::GetFileByResourceIdAfterGetEntry,
                 ui_weak_ptr_,
                 get_file_callback,
                 get_content_callback));
}

void DriveFileSystem::GetFileByResourceIdAfterGetEntry(
    const GetFileCallback& get_file_callback,
    const google_apis::GetContentCallback& get_content_callback,
    DriveFileError error,
    const FilePath& file_path,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!get_file_callback.is_null());

  if (error != DRIVE_FILE_OK) {
    get_file_callback.Run(DRIVE_FILE_ERROR_NOT_FOUND,
                          FilePath(),
                          std::string(),
                          REGULAR_FILE);
    return;
  }

  GetResolvedFileByPath(file_path,
                        get_file_callback,
                        get_content_callback,
                        entry_proto.Pass());
}

void DriveFileSystem::OnGetFileFromCache(
    const GetFileFromCacheParams& in_params,
    DriveFileError error,
    const FilePath& cache_file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!in_params.get_file_callback.is_null());

  // Have we found the file in cache? If so, return it back to the caller.
  if (error == DRIVE_FILE_OK) {
    in_params.get_file_callback.Run(error,
                                    cache_file_path,
                                    in_params.mime_type,
                                    REGULAR_FILE);
    return;
  }

  // If cache file is not found, try to download the file from the server
  // instead. This logic is rather complicated but here's how this works:
  //
  // Retrieve fresh file metadata from server. We will extract file size and
  // content url from there (we want to make sure used content url is not
  // stale).
  //
  // Check if we have enough space, based on the expected file size.
  // - if we don't have enough space, try to free up the disk space
  // - if we still don't have enough space, return "no space" error
  // - if we have enough space, start downloading the file from the server
  GetFileFromCacheParams params(in_params);
  params.cache_file_path = cache_file_path;
  drive_service_->GetDocumentEntry(
      params.resource_id,
      base::Bind(&DriveFileSystem::OnGetDocumentEntry,
                 ui_weak_ptr_,
                 params));
}

void DriveFileSystem::OnGetDocumentEntry(const GetFileFromCacheParams& params,
                                         google_apis::GDataErrorCode status,
                                         scoped_ptr<base::Value> data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!params.get_file_callback.is_null());

  const DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK) {
    params.get_file_callback.Run(error,
                                 params.cache_file_path,
                                 params.mime_type,
                                 REGULAR_FILE);
    return;
  }

  scoped_ptr<google_apis::DocumentEntry> doc_entry(
      google_apis::DocumentEntry::ExtractAndParse(*data));
  GURL content_url = doc_entry->content_url();
  int64 file_size = doc_entry->file_size();

  // The content URL can be empty for non-downloadable files (such as files
  // shared from others with "prevent downloading by viewers" flag set.)
  if (content_url.is_empty()) {
    params.get_file_callback.Run(DRIVE_FILE_ERROR_ACCESS_DENIED,
                                 params.cache_file_path,
                                 params.mime_type,
                                 REGULAR_FILE);
    return;
  }

  DCHECK_EQ(params.resource_id, doc_entry->resource_id());
  resource_metadata_->RefreshFile(
      doc_entry.Pass(),
      base::Bind(&DriveFileSystem::CheckForSpaceBeforeDownload,
                 ui_weak_ptr_,
                 params,
                 file_size,
                 content_url));
}

void DriveFileSystem::CheckForSpaceBeforeDownload(
    const GetFileFromCacheParams& params,
    int64 file_size,
    const GURL& content_url,
    DriveFileError error,
    const FilePath& /* drive_file_path */,
    scoped_ptr<DriveEntryProto> /* entry_proto */) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!params.get_file_callback.is_null());

  if (error != DRIVE_FILE_OK) {
    params.get_file_callback.Run(error,
                                 params.cache_file_path,
                                 params.mime_type,
                                 REGULAR_FILE);
    return;
  }

  bool* has_enough_space = new bool(false);
  google_apis::util::PostBlockingPoolSequencedTaskAndReply(
      FROM_HERE,
      blocking_task_runner_,
      base::Bind(&DriveCache::FreeDiskSpaceIfNeededFor,
                 base::Unretained(cache_),
                 file_size,
                 has_enough_space),
      base::Bind(&DriveFileSystem::StartDownloadFileIfEnoughSpace,
                 ui_weak_ptr_,
                 params,
                 content_url,
                 params.cache_file_path,
                 base::Owned(has_enough_space)));
}

void DriveFileSystem::StartDownloadFileIfEnoughSpace(
    const GetFileFromCacheParams& params,
    const GURL& content_url,
    const FilePath& cache_file_path,
    bool* has_enough_space) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!params.get_file_callback.is_null());

  if (!*has_enough_space) {
    // If no enough space, return PLATFORM_FILE_ERROR_NO_SPACE.
    params.get_file_callback.Run(DRIVE_FILE_ERROR_NO_SPACE,
                                 cache_file_path,
                                 params.mime_type,
                                 REGULAR_FILE);
    return;
  }

  // We have enough disk space. Start downloading the file.
  drive_service_->DownloadFile(
      params.virtual_file_path,
      params.local_tmp_path,
      content_url,
      base::Bind(&DriveFileSystem::OnFileDownloaded,
                 ui_weak_ptr_,
                 params),
      params.get_content_callback);
}

void DriveFileSystem::GetEntryInfoByPath(const FilePath& file_path,
                                         const GetEntryInfoCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::GetEntryInfoByPathOnUIThread,
                 ui_weak_ptr_,
                 file_path,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::GetEntryInfoByPathOnUIThread(
    const FilePath& file_path,
    const GetEntryInfoCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  LoadFeedIfNeeded(
      base::Bind(&DriveFileSystem::GetEntryInfoByPathOnUIThreadAfterLoad,
                 ui_weak_ptr_,
                 file_path,
                 callback));
}

void DriveFileSystem::GetEntryInfoByPathOnUIThreadAfterLoad(
    const FilePath& file_path,
    const GetEntryInfoCallback& callback,
    DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error, scoped_ptr<DriveEntryProto>());
    return;
  }

  resource_metadata_->GetEntryInfoByPath(
      file_path,
      base::Bind(&DriveFileSystem::GetEntryInfoByPathOnUIThreadAfterGetEntry,
                 ui_weak_ptr_,
                 callback));
}

void DriveFileSystem::GetEntryInfoByPathOnUIThreadAfterGetEntry(
    const GetEntryInfoCallback& callback,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error, scoped_ptr<DriveEntryProto>());
    return;
  }
  DCHECK(entry_proto.get());

  CheckLocalModificationAndRun(entry_proto.Pass(), callback);
}

void DriveFileSystem::ReadDirectoryByPath(
    const FilePath& directory_path,
    const ReadDirectoryWithSettingCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::ReadDirectoryByPathOnUIThread,
                 ui_weak_ptr_,
                 directory_path,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::ReadDirectoryByPathOnUIThread(
    const FilePath& directory_path,
    const ReadDirectoryWithSettingCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  LoadFeedIfNeeded(
      base::Bind(&DriveFileSystem::ReadDirectoryByPathOnUIThreadAfterLoad,
                 ui_weak_ptr_,
                 directory_path,
                 callback));
}

void DriveFileSystem::ReadDirectoryByPathOnUIThreadAfterLoad(
    const FilePath& directory_path,
    const ReadDirectoryWithSettingCallback& callback,
    DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error,
                 hide_hosted_docs_,
                 scoped_ptr<DriveEntryProtoVector>());
    return;
  }

  resource_metadata_->ReadDirectoryByPath(
      directory_path,
      base::Bind(&DriveFileSystem::ReadDirectoryByPathOnUIThreadAfterRead,
                 ui_weak_ptr_,
                 callback));
}

void DriveFileSystem::ReadDirectoryByPathOnUIThreadAfterRead(
    const ReadDirectoryWithSettingCallback& callback,
    DriveFileError error,
    scoped_ptr<DriveEntryProtoVector> entries) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error,
                 hide_hosted_docs_,
                 scoped_ptr<DriveEntryProtoVector>());
    return;
  }
  DCHECK(entries.get());  // This is valid for emptry directories too.

  callback.Run(DRIVE_FILE_OK, hide_hosted_docs_, entries.Pass());
}

void DriveFileSystem::RequestDirectoryRefresh(const FilePath& directory_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::RequestDirectoryRefreshOnUIThread,
                 ui_weak_ptr_,
                 directory_path));
}

void DriveFileSystem::RequestDirectoryRefreshOnUIThread(
    const FilePath& directory_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Make sure the destination directory exists.
  resource_metadata_->GetEntryInfoByPath(
      directory_path,
      base::Bind(
          &DriveFileSystem::RequestDirectoryRefreshOnUIThreadAfterGetEntryInfo,
          ui_weak_ptr_,
          directory_path));
}

void DriveFileSystem::RequestDirectoryRefreshOnUIThreadAfterGetEntryInfo(
    const FilePath& directory_path,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (error != DRIVE_FILE_OK ||
      !entry_proto->file_info().is_directory()) {
    LOG(ERROR) << "Directory entry not found: " << directory_path.value();
    return;
  }

  feed_loader_->LoadDirectoryFromServer(
      entry_proto->resource_id(),
      base::Bind(&DriveFileSystem::OnRequestDirectoryRefresh,
                 ui_weak_ptr_,
                 directory_path));
}

void DriveFileSystem::OnRequestDirectoryRefresh(
    const FilePath& directory_path,
    scoped_ptr<LoadFeedParams> params,
    DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(params.get());

  if (error != DRIVE_FILE_OK) {
    LOG(ERROR) << "Failed to refresh directory: " << directory_path.value()
               << ": " << error;
    return;
  }

  DriveFeedProcessor::DriveEntryProtoMap entry_proto_map;
  DriveFeedProcessor feed_processor(resource_metadata_.get());
  feed_processor.FeedToEntryProtoMap(
      params->feed_list,
      &entry_proto_map,
      NULL,
      NULL);

  resource_metadata_->RefreshDirectory(
      params->directory_resource_id,
      entry_proto_map,
      base::Bind(&DriveFileSystem::OnDirectoryChangeFileMoveCallback,
                 ui_weak_ptr_,
                 FileOperationCallback()));
}

void DriveFileSystem::UpdateFileByResourceId(
    const std::string& resource_id,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::UpdateFileByResourceIdOnUIThread,
                 ui_weak_ptr_,
                 resource_id,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::UpdateFileByResourceIdOnUIThread(
    const std::string& resource_id,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  // TODO(satorux): GetEntryInfoByResourceId() is called twice for
  // UpdateFileByResourceIdOnUIThread(). crbug.com/143873
  resource_metadata_->GetEntryInfoByResourceId(
      resource_id,
      base::Bind(&DriveFileSystem::UpdateFileByEntryInfo,
                 ui_weak_ptr_,
                 callback));
}

void DriveFileSystem::UpdateFileByEntryInfo(
    const FileOperationCallback& callback,
    DriveFileError error,
    const FilePath& drive_file_path,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error);
    return;
  }

  DCHECK(entry_proto.get());
  if (entry_proto->file_info().is_directory()) {
    callback.Run(DRIVE_FILE_ERROR_NOT_FOUND);
    return;
  }

  // Extract a pointer before we call Pass() so we can use it below.
  DriveEntryProto* entry_proto_ptr = entry_proto.get();
  cache_->GetFileOnUIThread(
      entry_proto_ptr->resource_id(),
      entry_proto_ptr->file_specific_info().file_md5(),
      base::Bind(&DriveFileSystem::OnGetFileCompleteForUpdateFile,
                 ui_weak_ptr_,
                 callback,
                 drive_file_path,
                 base::Passed(&entry_proto)));
}

void DriveFileSystem::OnGetFileCompleteForUpdateFile(
    const FileOperationCallback& callback,
    const FilePath& drive_file_path,
    scoped_ptr<DriveEntryProto> entry_proto,
    DriveFileError error,
    const FilePath& cache_file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error);
    return;
  }

  // Gets the size of the cache file. Since the file is locally modified, the
  // file size information stored in DriveEntry is not correct.
  DriveFileError* get_size_error = new DriveFileError(DRIVE_FILE_ERROR_FAILED);
  int64* file_size = new int64(-1);
  google_apis::util::PostBlockingPoolSequencedTaskAndReply(
      FROM_HERE,
      blocking_task_runner_,
      base::Bind(&GetLocalFileSizeOnBlockingPool,
                 cache_file_path,
                 get_size_error,
                 file_size),
      base::Bind(&DriveFileSystem::OnGetFileSizeCompleteForUpdateFile,
                 ui_weak_ptr_,
                 callback,
                 drive_file_path,
                 base::Passed(&entry_proto),
                 cache_file_path,
                 base::Owned(get_size_error),
                 base::Owned(file_size)));
}

void DriveFileSystem::OnGetFileSizeCompleteForUpdateFile(
    const FileOperationCallback& callback,
    const FilePath& drive_file_path,
    scoped_ptr<DriveEntryProto> entry_proto,
    const FilePath& cache_file_path,
    DriveFileError* error,
    int64* file_size) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  // |entry_proto| has been checked in UpdateFileByEntryInfo().
  DCHECK(entry_proto.get());
  DCHECK(!entry_proto->file_info().is_directory());

  if (*error != DRIVE_FILE_OK) {
    callback.Run(*error);
    return;
  }

  uploader_->UploadExistingFile(
      GURL(entry_proto->upload_url()),
      drive_file_path,
      cache_file_path,
      entry_proto->file_specific_info().content_mime_type(),
      *file_size,
      base::Bind(&DriveFileSystem::OnUpdatedFileUploaded,
                 ui_weak_ptr_,
                 callback),
      google_apis::UploaderReadyCallback());
}

void DriveFileSystem::OnUpdatedFileUploaded(
    const FileOperationCallback& callback,
    google_apis::DriveUploadError error,
    const FilePath& drive_path,
    const FilePath& file_path,
    scoped_ptr<google_apis::DocumentEntry> document_entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (error != google_apis::DRIVE_UPLOAD_OK) {
    if (!callback.is_null())
      callback.Run(DriveUploadErrorToDriveFileError(error));
    return;
  }

  AddUploadedFile(google_apis::UPLOAD_EXISTING_FILE,
                  drive_path.DirName(),
                  document_entry.Pass(),
                  file_path,
                  DriveCache::FILE_OPERATION_MOVE,
                  base::Bind(&OnAddUploadFileCompleted, callback,
                             DriveUploadErrorToDriveFileError(error)));
}

void DriveFileSystem::GetAvailableSpace(
    const GetAvailableSpaceCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::GetAvailableSpaceOnUIThread,
                 ui_weak_ptr_,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::GetAvailableSpaceOnUIThread(
    const GetAvailableSpaceCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  drive_service_->GetAccountMetadata(
      google_apis::util::IsDriveV2ApiEnabled() ?
      base::Bind(&DriveFileSystem::OnGetAboutResource,
                 ui_weak_ptr_,
                 callback) :
      base::Bind(&DriveFileSystem::OnGetAvailableSpace,
                 ui_weak_ptr_,
                 callback));
}

void DriveFileSystem::OnGetAvailableSpace(
    const GetAvailableSpaceCallback& callback,
    google_apis::GDataErrorCode status,
    scoped_ptr<base::Value> data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK) {
    callback.Run(error, -1, -1);
    return;
  }

  scoped_ptr<google_apis::AccountMetadataFeed> feed;
  if (data.get())
    feed = google_apis::AccountMetadataFeed::CreateFrom(*data);
  if (!feed.get()) {
    callback.Run(DRIVE_FILE_ERROR_FAILED, -1, -1);
    return;
  }

  callback.Run(DRIVE_FILE_OK,
               feed->quota_bytes_total(),
               feed->quota_bytes_used());
}

void DriveFileSystem::OnGetAboutResource(
    const GetAvailableSpaceCallback& callback,
    google_apis::GDataErrorCode status,
    scoped_ptr<base::Value> resource_json) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK) {
    callback.Run(error, -1, -1);
    return;
  }

  scoped_ptr<google_apis::AboutResource> about;
  if (resource_json.get())
    about = google_apis::AboutResource::CreateFrom(*resource_json);

  if (!about.get()) {
    callback.Run(DRIVE_FILE_ERROR_FAILED, -1, -1);
    return;
  }

  callback.Run(DRIVE_FILE_OK,
               about->quota_bytes_total(),
               about->quota_bytes_used());
}

void DriveFileSystem::AddNewDirectory(
    const CreateDirectoryParams& params,
    google_apis::GDataErrorCode status,
    scoped_ptr<base::Value> data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!params.callback.is_null());

  DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK) {
    params.callback.Run(error);
    return;
  }

  resource_metadata_->AddEntryToDirectory(
      params.created_directory_path.DirName(),
      scoped_ptr<google_apis::DocumentEntry>(
          google_apis::DocumentEntry::ExtractAndParse(*data)),
      base::Bind(&DriveFileSystem::ContinueCreateDirectory,
                 ui_weak_ptr_,
                 params));
}

void DriveFileSystem::ContinueCreateDirectory(
    const CreateDirectoryParams& params,
    DriveFileError error,
    const FilePath& moved_file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!params.callback.is_null());

  if (error != DRIVE_FILE_OK) {
    params.callback.Run(error);
    return;
  }

  OnDirectoryChanged(moved_file_path.DirName());

  // Not done yet with recursive directory creation?
  if (params.target_directory_path != params.created_directory_path &&
      params.is_recursive) {
    CreateDirectory(params.target_directory_path,
                    params.is_exclusive,
                    params.is_recursive,
                    params.callback);
  } else {
    // Finally done with the create request.
    params.callback.Run(DRIVE_FILE_OK);
  }
}

void DriveFileSystem::OnSearch(const SearchCallback& search_callback,
                               scoped_ptr<LoadFeedParams> params,
                               DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!search_callback.is_null());
  DCHECK(params.get());

  if (error != DRIVE_FILE_OK) {
    search_callback.Run(error,
                        GURL(),
                        scoped_ptr<std::vector<SearchResultInfo> >());
    return;
  }

  // The search results will be returned using virtual directory.
  // The directory is not really part of the file system, so it has no parent or
  // root.
  std::vector<SearchResultInfo>* results(new std::vector<SearchResultInfo>());
  scoped_ptr<std::vector<SearchResultInfo> > result_vec(results);

  DCHECK_EQ(1u, params->feed_list.size());
  google_apis::DocumentFeed* feed = params->feed_list[0];

  // TODO(tbarzic): Limit total number of returned results for the query.
  GURL next_feed;
  feed->GetNextFeedURL(&next_feed);

  const base::Closure callback = base::Bind(
      search_callback, DRIVE_FILE_OK, next_feed, base::Passed(&result_vec));

  std::vector<google_apis::DocumentEntry*> entries;
  feed->ReleaseEntries(&entries);
  if (entries.empty()) {
    callback.Run();
    return;
  }

  DVLOG(1) << "OnSearch number of entries=" << entries.size();
  // Go through all entries generated by the feed and add them to the search
  // result directory.
  for (size_t i = 0; i < entries.size(); ++i) {
    // Run the callback if this is the last iteration of the loop.
    const bool should_run_callback = (i+1 == entries.size());
    resource_metadata_->RefreshFile(
        scoped_ptr<google_apis::DocumentEntry>(entries[i]),
        base::Bind(&DriveFileSystem::AddToSearchResults,
                   ui_weak_ptr_,
                   results,
                   should_run_callback ? callback : base::Closure()));
  }
}

void DriveFileSystem::AddToSearchResults(
    std::vector<SearchResultInfo>* results,
    const base::Closure& callback,
    DriveFileError error,
    const FilePath& drive_file_path,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // If a result is not present in our local file system snapshot, call
  // CheckForUpdates to refresh the snapshot with a delta feed. This may happen
  // if the entry has recently been added to the drive (and we still haven't
  // received its delta feed).
  if (error == DRIVE_FILE_OK) {
    DCHECK(entry_proto.get());
    const bool is_directory = entry_proto->file_info().is_directory();
    results->push_back(SearchResultInfo(drive_file_path, is_directory));
    DVLOG(1) << "AddToSearchResults " << drive_file_path.value();
  } else if (error == DRIVE_FILE_ERROR_NOT_FOUND) {
    CheckForUpdates();
  } else {
    NOTREACHED();
  }

  if (!callback.is_null())
    callback.Run();
}

void DriveFileSystem::Search(const std::string& search_query,
                             const GURL& next_feed,
                             const SearchCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::SearchAsyncOnUIThread,
                 ui_weak_ptr_,
                 search_query,
                 next_feed,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::SearchAsyncOnUIThread(
    const std::string& search_query,
    const GURL& next_feed,
    const SearchCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  feed_loader_->SearchFromServer(
      search_query,
      next_feed,
      base::Bind(&DriveFileSystem::OnSearch, ui_weak_ptr_, callback));
}

void DriveFileSystem::OnDirectoryChangedByOperation(
    const FilePath& directory_path) {
  OnDirectoryChanged(directory_path);
}

void DriveFileSystem::OnDirectoryChanged(const FilePath& directory_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  FOR_EACH_OBSERVER(DriveFileSystemObserver, observers_,
                    OnDirectoryChanged(directory_path));
}

void DriveFileSystem::OnDocumentFeedFetched(int num_accumulated_entries) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  FOR_EACH_OBSERVER(DriveFileSystemObserver, observers_,
                    OnDocumentFeedFetched(num_accumulated_entries));
}

void DriveFileSystem::OnFeedFromServerLoaded() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  FOR_EACH_OBSERVER(DriveFileSystemObserver, observers_,
                    OnFeedFromServerLoaded());
}

void DriveFileSystem::LoadRootFeedFromCacheForTesting(
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  feed_loader_->LoadFromCache(callback);
}

void DriveFileSystem::OnFileDownloaded(
    const GetFileFromCacheParams& params,
    google_apis::GDataErrorCode status,
    const GURL& content_url,
    const FilePath& downloaded_file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!params.get_file_callback.is_null());

  // If user cancels download of a pinned-but-not-fetched file, mark file as
  // unpinned so that we do not sync the file again.
  if (status == google_apis::GDATA_CANCELLED) {
    cache_->GetCacheEntryOnUIThread(
        params.resource_id,
        params.md5,
        base::Bind(&DriveFileSystem::UnpinIfPinned,
                   ui_weak_ptr_,
                   params.resource_id,
                   params.md5));
  }

  // At this point, the disk can be full or nearly full for several reasons:
  // - The expected file size was incorrect and the file was larger
  // - There was an in-flight download operation and it used up space
  // - The disk became full for some user actions we cannot control
  //   (ex. the user might have downloaded a large file from a regular web site)
  //
  // If we don't have enough space, we return PLATFORM_FILE_ERROR_NO_SPACE,
  // and try to free up space, even if the file was downloaded successfully.
  bool* has_enough_space = new bool(false);
  google_apis::util::PostBlockingPoolSequencedTaskAndReply(
      FROM_HERE,
      blocking_task_runner_,
      base::Bind(&DriveCache::FreeDiskSpaceIfNeededFor,
                 base::Unretained(cache_),
                 0,
                 has_enough_space),
      base::Bind(&DriveFileSystem::OnFileDownloadedAndSpaceChecked,
                 ui_weak_ptr_,
                 params,
                 status,
                 content_url,
                 downloaded_file_path,
                 base::Owned(has_enough_space)));
}

void DriveFileSystem::UnpinIfPinned(
    const std::string& resource_id,
    const std::string& md5,
    bool success,
    const DriveCacheEntry& cache_entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // TODO(hshi): http://crbug.com/127138 notify when file properties change.
  // This allows file manager to clear the "Available offline" checkbox.
  if (success && cache_entry.is_pinned())
    cache_->UnpinOnUIThread(resource_id, md5, CacheOperationCallback());
}

void DriveFileSystem::OnFileDownloadedAndSpaceChecked(
    const GetFileFromCacheParams& params,
    google_apis::GDataErrorCode status,
    const GURL& content_url,
    const FilePath& downloaded_file_path,
    bool* has_enough_space) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!params.get_file_callback.is_null());

  DriveFileError error = util::GDataToDriveFileError(status);

  // Make sure that downloaded file is properly stored in cache. We don't have
  // to wait for this operation to finish since the user can already use the
  // downloaded file.
  if (error == DRIVE_FILE_OK) {
    if (*has_enough_space) {
      cache_->StoreOnUIThread(
          params.resource_id,
          params.md5,
          downloaded_file_path,
          DriveCache::FILE_OPERATION_MOVE,
          base::Bind(&DriveFileSystem::OnDownloadStoredToCache,
                     ui_weak_ptr_));
    } else {
      // If we don't have enough space, remove the downloaded file, and
      // report "no space" error.
      google_apis::util::PostBlockingPoolSequencedTask(
          FROM_HERE,
          blocking_task_runner_,
          base::Bind(base::IgnoreResult(&file_util::Delete),
                     downloaded_file_path,
                     false /* recursive*/));
      error = DRIVE_FILE_ERROR_NO_SPACE;
    }
  }

  params.get_file_callback.Run(error,
                               downloaded_file_path,
                               params.mime_type,
                               REGULAR_FILE);
}

void DriveFileSystem::OnDownloadStoredToCache(DriveFileError error,
                                              const std::string& resource_id,
                                              const std::string& md5) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Nothing much to do here for now.
}

void DriveFileSystem::OnDirectoryChangeFileMoveCallback(
    const FileOperationCallback& callback,
    DriveFileError error,
    const FilePath& directory_path) {
  if (error == DRIVE_FILE_OK)
    OnDirectoryChanged(directory_path);

  if (!callback.is_null())
    callback.Run(error);
}

void DriveFileSystem::NotifyFileSystemMounted() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DVLOG(1) << "File System is mounted";
  // Notify the observers that the file system is mounted.
  FOR_EACH_OBSERVER(DriveFileSystemObserver, observers_,
                    OnFileSystemMounted());
}

void DriveFileSystem::NotifyFileSystemToBeUnmounted() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DVLOG(1) << "File System is to be unmounted";
  // Notify the observers that the file system is being unmounted.
  FOR_EACH_OBSERVER(DriveFileSystemObserver, observers_,
                    OnFileSystemBeingUnmounted());
}

void DriveFileSystem::OnFeedCacheLoaded(const FileOperationCallback& callback,
                                        DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    // If cache cannot be loaded, try to load from server directly.
    feed_loader_->ReloadFromServerIfNeeded(
        base::Bind(&DriveFileSystem::NotifyInitialLoadFinishedAndRun,
                   ui_weak_ptr_,
                   callback));
    return;
  }

  // If successfully loaded from the server, notify the success, and check for
  // the latest feed from the server.
  DCHECK(resource_metadata_->origin() == INITIALIZED);
  NotifyInitialLoadFinishedAndRun(callback, DRIVE_FILE_OK);
  CheckForUpdates();
}

void DriveFileSystem::NotifyInitialLoadFinishedAndRun(
    const FileOperationCallback& callback,
    DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  // Notify the observers that root directory has been initialized.
  FOR_EACH_OBSERVER(DriveFileSystemObserver,
                    observers_,
                    OnInitialLoadFinished(error));

  callback.Run(error);
}

void DriveFileSystem::FindFirstMissingParentDirectory(
    const FilePath& directory_path,
    const FindFirstMissingParentDirectoryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  std::vector<FilePath::StringType> path_parts;
  directory_path.GetComponents(&path_parts);

  scoped_ptr<FindFirstMissingParentDirectoryParams> params(
      new FindFirstMissingParentDirectoryParams(path_parts, callback));

  // Have to post because FindFirstMissingParentDirectoryInternal calls
  // the callback directly.
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(&DriveFileSystem::FindFirstMissingParentDirectoryInternal,
                 ui_weak_ptr_,
                 base::Passed(&params)));
}

void DriveFileSystem::FindFirstMissingParentDirectoryInternal(
    scoped_ptr<FindFirstMissingParentDirectoryParams> params) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(params.get());

  // Terminate recursion if we're at the last element.
  if (params->index == params->path_parts.size()) {
    FindFirstMissingParentDirectoryResult result;
    result.Init(FIND_FIRST_DIRECTORY_ALREADY_PRESENT, FilePath(), GURL());
    params->callback.Run(result);
    return;
  }

  params->current_path = params->current_path.Append(
      params->path_parts[params->index]);
  // Need a reference to current_path before we call base::Passed because the
  // order of evaluation of arguments is indeterminate.
  const FilePath& current_path = params->current_path;
  resource_metadata_->GetEntryInfoByPath(
      current_path,
      base::Bind(&DriveFileSystem::ContinueFindFirstMissingParentDirectory,
                 ui_weak_ptr_,
                 base::Passed(&params)));
}

void DriveFileSystem::ContinueFindFirstMissingParentDirectory(
    scoped_ptr<FindFirstMissingParentDirectoryParams> params,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(params.get());

  FindFirstMissingParentDirectoryResult result;
  if (error == DRIVE_FILE_ERROR_NOT_FOUND) {
    // Found the missing parent.
    result.Init(FIND_FIRST_FOUND_MISSING,
                params->current_path,
                params->last_dir_content_url);
    params->callback.Run(result);
  } else if (error != DRIVE_FILE_OK ||
             !entry_proto->file_info().is_directory()) {
    // Unexpected error, or found a file when we were expecting a directory.
    result.Init(FIND_FIRST_FOUND_INVALID, FilePath(), GURL());
    params->callback.Run(result);
  } else {
    // This parent exists, so recursively look at the next element.
    params->last_dir_content_url = GURL(entry_proto->content_url());
    params->index++;
    FindFirstMissingParentDirectoryInternal(params.Pass());
  }
}

void DriveFileSystem::AddUploadedFile(
    google_apis::UploadMode upload_mode,
    const FilePath& directory_path,
    scoped_ptr<google_apis::DocumentEntry> entry,
    const FilePath& file_content_path,
    DriveCache::FileOperationType cache_operation,
    const base::Closure& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Post a task to the same thread, rather than calling it here, as
  // AddUploadedFile() is asynchronous.
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(&DriveFileSystem::AddUploadedFileOnUIThread,
                 ui_weak_ptr_,
                 upload_mode,
                 directory_path,
                 base::Passed(&entry),
                 file_content_path,
                 cache_operation,
                 callback));
}

void DriveFileSystem::AddUploadedFileOnUIThread(
    google_apis::UploadMode upload_mode,
    const FilePath& directory_path,
    scoped_ptr<google_apis::DocumentEntry> doc_entry,
    const FilePath& file_content_path,
    DriveCache::FileOperationType cache_operation,
    const base::Closure& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(doc_entry.get());

  const std::string& resource_id = doc_entry->resource_id();
  scoped_ptr<AddUploadedFileParams> params(
      new AddUploadedFileParams(upload_mode,
                                directory_path,
                                doc_entry.Pass(),
                                file_content_path,
                                cache_operation,
                                callback));

  const FileMoveCallback file_move_callback =
      base::Bind(&DriveFileSystem::ContinueAddUploadedFile,
                 ui_weak_ptr_, base::Passed(&params));

  if (upload_mode == google_apis::UPLOAD_EXISTING_FILE) {
    // Remove the existing entry.
    resource_metadata_->RemoveEntryFromParent(resource_id, file_move_callback);
  } else {
    file_move_callback.Run(DRIVE_FILE_OK, FilePath());
  }
}

void DriveFileSystem::ContinueAddUploadedFile(
    scoped_ptr<AddUploadedFileParams> params,
    DriveFileError error,
    const FilePath& /* file_path */) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_EQ(DRIVE_FILE_OK, error);
  DCHECK(params->doc_entry.get());

  params->resource_id = params->doc_entry->resource_id();
  params->md5 = params->doc_entry->file_md5();
  DCHECK(!params->resource_id.empty());
  DCHECK(!params->md5.empty());

  // Get parameters before base::Passed() invalidates |params|.
  const FilePath& directory_path = params->directory_path;
  scoped_ptr<google_apis::DocumentEntry> doc_entry(params->doc_entry.Pass());

  resource_metadata_->AddEntryToDirectory(
      directory_path,
      doc_entry.Pass(),
      base::Bind(&DriveFileSystem::AddUploadedFileToCache,
                 ui_weak_ptr_,
                 base::Passed(&params)));
}

void DriveFileSystem::AddUploadedFileToCache(
    scoped_ptr<AddUploadedFileParams> params,
    DriveFileError error,
    const FilePath& file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!params->resource_id.empty());
  DCHECK(!params->md5.empty());
  DCHECK(!params->resource_id.empty());
  DCHECK(!params->callback.is_null());

  if (error != DRIVE_FILE_OK) {
    params->callback.Run();
    return;
  }

  OnDirectoryChanged(file_path.DirName());

  if (params->upload_mode == google_apis::UPLOAD_NEW_FILE) {
    // Add the file to the cache if we have uploaded a new file.
    cache_->StoreOnUIThread(params->resource_id,
                            params->md5,
                            params->file_content_path,
                            params->cache_operation,
                            base::Bind(&OnCacheUpdatedForAddUploadedFile,
                                       params->callback));
  } else if (params->upload_mode == google_apis::UPLOAD_EXISTING_FILE) {
    // Clear the dirty bit if we have updated an existing file.
    cache_->ClearDirtyOnUIThread(params->resource_id,
                                 params->md5,
                                 base::Bind(&OnCacheUpdatedForAddUploadedFile,
                                            params->callback));
  } else {
    NOTREACHED() << "Unexpected upload mode: " << params->upload_mode;
    // Shouldn't reach here, so the line below should not make much sense, but
    // since calling |callback| exactly once is our obligation, we'd better call
    // it for not to clutter further more.
    params->callback.Run();
  }
}

void DriveFileSystem::UpdateEntryData(
    const std::string& resource_id,
    const std::string& md5,
    scoped_ptr<google_apis::DocumentEntry> entry,
    const FilePath& file_content_path,
    const base::Closure& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Post a task to the same thread, rather than calling it here, as
  // UpdateEntryData() is asynchronous.
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(&DriveFileSystem::UpdateEntryDataOnUIThread,
                 ui_weak_ptr_,
                 UpdateEntryParams(resource_id,
                                   md5,
                                   file_content_path,
                                   callback),
                 base::Passed(&entry)));
}

void DriveFileSystem::UpdateEntryDataOnUIThread(
    const UpdateEntryParams& params,
    scoped_ptr<google_apis::DocumentEntry> entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  resource_metadata_->RefreshFile(
      entry.Pass(),
      base::Bind(&DriveFileSystem::UpdateCacheEntryOnUIThread,
                 ui_weak_ptr_,
                 params));
}

void DriveFileSystem::UpdateCacheEntryOnUIThread(
    const UpdateEntryParams& params,
    DriveFileError error,
    const FilePath& /* drive_file_path */,
    scoped_ptr<DriveEntryProto> /* entry_proto */) {
  if (error != DRIVE_FILE_OK) {
    if (!params.callback.is_null())
      params.callback.Run();
    return;
  }

  // Add the file to the cache if we have uploaded a new file.
  cache_->StoreOnUIThread(params.resource_id,
                          params.md5,
                          params.file_content_path,
                          DriveCache::FILE_OPERATION_MOVE,
                          base::Bind(&OnCacheUpdatedForAddUploadedFile,
                                     params.callback));
}

DriveFileSystemMetadata DriveFileSystem::GetMetadata() const {
  DriveFileSystemMetadata metadata;
  metadata.largest_changestamp = resource_metadata_->largest_changestamp();
  metadata.origin = ContentOriginToString(resource_metadata_->origin());
  if (feed_loader_->refreshing())
    metadata.origin += " (refreshing)";
  return metadata;
}

void DriveFileSystem::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (type == chrome::NOTIFICATION_PREF_CHANGED) {
    PrefService* pref_service = profile_->GetPrefs();
    std::string* pref_name = content::Details<std::string>(details).ptr();
    if (*pref_name == prefs::kDisableDriveHostedFiles) {
      SetHideHostedDocuments(
          pref_service->GetBoolean(prefs::kDisableDriveHostedFiles));
    }
  } else {
    NOTREACHED();
  }
}

void DriveFileSystem::SetHideHostedDocuments(bool hide) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (hide == hide_hosted_docs_)
    return;

  hide_hosted_docs_ = hide;
  const FilePath root_path = resource_metadata_->root()->GetFilePath();

  // Kick off directory refresh when this setting changes.
  FOR_EACH_OBSERVER(DriveFileSystemObserver, observers_,
                    OnDirectoryChanged(root_path));
}

//============= DriveFileSystem: internal helper functions =====================

void DriveFileSystem::InitializePreferenceObserver() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  pref_registrar_.reset(new PrefChangeRegistrar());
  pref_registrar_->Init(profile_->GetPrefs());
  pref_registrar_->Add(prefs::kDisableDriveHostedFiles, this);
}

void DriveFileSystem::OpenFile(const FilePath& file_path,
                               const OpenFileCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::OpenFileOnUIThread,
                 ui_weak_ptr_,
                 file_path,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::OpenFileOnUIThread(const FilePath& file_path,
                                         const OpenFileCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // If the file is already opened, it cannot be opened again before closed.
  // This is for avoiding simultaneous modification to the file, and moreover
  // to avoid an inconsistent cache state (suppose an operation sequence like
  // Open->Open->modify->Close->modify->Close; the second modify may not be
  // synchronized to the server since it is already Closed on the cache).
  if (open_files_.find(file_path) != open_files_.end()) {
    base::MessageLoopProxy::current()->PostTask(
        FROM_HERE,
        base::Bind(callback, DRIVE_FILE_ERROR_IN_USE, FilePath()));
    return;
  }
  open_files_.insert(file_path);

  resource_metadata_->GetEntryInfoByPath(
      file_path,
      base::Bind(&DriveFileSystem::OnGetEntryInfoCompleteForOpenFile,
                 ui_weak_ptr_,
                 file_path,
                 base::Bind(&DriveFileSystem::OnOpenFileFinished,
                            ui_weak_ptr_,
                            file_path,
                            callback)));
}

void DriveFileSystem::OnGetEntryInfoCompleteForOpenFile(
    const FilePath& file_path,
    const OpenFileCallback& callback,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(entry_proto.get() || error != DRIVE_FILE_OK);

  if (entry_proto.get() && !entry_proto->has_file_specific_info())
    error = DRIVE_FILE_ERROR_NOT_FOUND;

  if (error == DRIVE_FILE_OK) {
    if (entry_proto->file_specific_info().file_md5().empty() ||
        entry_proto->file_specific_info().is_hosted_document()) {
      // No support for opening a directory or hosted document.
      error = DRIVE_FILE_ERROR_INVALID_OPERATION;
    }
  }

  if (error != DRIVE_FILE_OK) {
    if (!callback.is_null())
      callback.Run(error, FilePath());
    return;
  }

  DCHECK(!entry_proto->resource_id().empty());
  // Extract a pointer before we call Pass() so we can use it below.
  DriveEntryProto* entry_proto_ptr = entry_proto.get();
  GetResolvedFileByPath(
      file_path,
      base::Bind(&DriveFileSystem::OnGetFileCompleteForOpenFile,
                 ui_weak_ptr_,
                 callback,
                 GetFileCompleteForOpenParams(
                     entry_proto_ptr->resource_id(),
                     entry_proto_ptr->file_specific_info().file_md5())),
      google_apis::GetContentCallback(),
      entry_proto.Pass());
}

void DriveFileSystem::OnGetFileCompleteForOpenFile(
    const OpenFileCallback& callback,
    const GetFileCompleteForOpenParams& entry_proto,
    DriveFileError error,
    const FilePath& file_path,
    const std::string& mime_type,
    DriveFileType file_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (error != DRIVE_FILE_OK) {
    if (!callback.is_null())
      callback.Run(error, FilePath());
    return;
  }

  // OpenFileOnUIThread ensures that the file is a regular file.
  DCHECK_EQ(REGULAR_FILE, file_type);

  cache_->MarkDirtyOnUIThread(
      entry_proto.resource_id,
      entry_proto.md5,
      base::Bind(&DriveFileSystem::OnMarkDirtyInCacheCompleteForOpenFile,
                 ui_weak_ptr_,
                 callback));
}

void DriveFileSystem::OnMarkDirtyInCacheCompleteForOpenFile(
    const OpenFileCallback& callback,
    DriveFileError error,
    const FilePath& cache_file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!callback.is_null())
    callback.Run(error, cache_file_path);
}

void DriveFileSystem::OnOpenFileFinished(const FilePath& file_path,
                                         const OpenFileCallback& callback,
                                         DriveFileError result,
                                         const FilePath& cache_file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // All the invocation of |callback| from operations initiated from OpenFile
  // must go through here. Removes the |file_path| from the remembered set when
  // the file was not successfully opened.
  if (result != DRIVE_FILE_OK)
    open_files_.erase(file_path);

  if (!callback.is_null())
    callback.Run(result, cache_file_path);
}

void DriveFileSystem::CloseFile(const FilePath& file_path,
                                const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  google_apis::RunTaskOnUIThread(
      base::Bind(&DriveFileSystem::CloseFileOnUIThread,
                 ui_weak_ptr_,
                 file_path,
                 google_apis::CreateRelayCallback(callback)));
}

void DriveFileSystem::CloseFileOnUIThread(
    const FilePath& file_path,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (open_files_.find(file_path) == open_files_.end()) {
    // The file is not being opened.
    base::MessageLoopProxy::current()->PostTask(
        FROM_HERE,
        base::Bind(callback, DRIVE_FILE_ERROR_NOT_FOUND));
    return;
  }

  // Step 1 of CloseFile: Get resource_id and md5 for |file_path|.
  resource_metadata_->GetEntryInfoByPath(
      file_path,
      base::Bind(&DriveFileSystem::CloseFileOnUIThreadAfterGetEntryInfo,
                 ui_weak_ptr_,
                 file_path,
                 base::Bind(&DriveFileSystem::CloseFileOnUIThreadFinalize,
                            ui_weak_ptr_,
                            file_path,
                            callback)));
}

void DriveFileSystem::CloseFileOnUIThreadAfterGetEntryInfo(
    const FilePath& file_path,
    const FileOperationCallback& callback,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (entry_proto.get() && !entry_proto->has_file_specific_info())
    error = DRIVE_FILE_ERROR_NOT_FOUND;

  if (error != DRIVE_FILE_OK) {
    callback.Run(error);
    return;
  }

  // Step 2 of CloseFile: Commit the modification in cache. This will trigger
  // background upload.
  // TODO(benchan,kinaba): Call ClearDirtyInCache instead of CommitDirtyInCache
  // if the file has not been modified. Come up with a way to detect the
  // intactness effectively, or provide a method for user to declare it when
  // calling CloseFile().
  cache_->CommitDirtyOnUIThread(
      entry_proto->resource_id(),
      entry_proto->file_specific_info().file_md5(),
      base::Bind(&DriveFileSystem::CloseFileOnUIThreadAfterCommitDirtyInCache,
                 ui_weak_ptr_,
                 callback));
}

void DriveFileSystem::CloseFileOnUIThreadAfterCommitDirtyInCache(
    const FileOperationCallback& callback,
    DriveFileError error,
    const std::string& /* resource_id */,
    const std::string& /* md5 */) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  callback.Run(error);
}

void DriveFileSystem::CloseFileOnUIThreadFinalize(
    const FilePath& file_path,
    const FileOperationCallback& callback,
    DriveFileError result) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  // Step 3 of CloseFile.
  // All the invocation of |callback| from operations initiated from CloseFile
  // must go through here. Removes the |file_path| from the remembered set so
  // that subsequent operations can open the file again.
  open_files_.erase(file_path);

  // Then invokes the user-supplied callback function.
  callback.Run(result);
}

void DriveFileSystem::CheckLocalModificationAndRun(
    scoped_ptr<DriveEntryProto> entry_proto,
    const GetEntryInfoCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(entry_proto.get());
  DCHECK(!callback.is_null());

  // For entries that will never be cached, use the original entry info as is.
  if (!entry_proto->has_file_specific_info() ||
      entry_proto->file_specific_info().is_hosted_document()) {
    callback.Run(DRIVE_FILE_OK, entry_proto.Pass());
    return;
  }

  // Checks if the file is cached and modified locally.
  const std::string resource_id = entry_proto->resource_id();
  const std::string md5 = entry_proto->file_specific_info().file_md5();
  cache_->GetCacheEntryOnUIThread(
      resource_id,
      md5,
      base::Bind(
          &DriveFileSystem::CheckLocalModificationAndRunAfterGetCacheEntry,
          ui_weak_ptr_, base::Passed(&entry_proto), callback));
}

void DriveFileSystem::CheckLocalModificationAndRunAfterGetCacheEntry(
    scoped_ptr<DriveEntryProto> entry_proto,
    const GetEntryInfoCallback& callback,
    bool success,
    const DriveCacheEntry& cache_entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  // When no dirty cache is found, use the original entry info as is.
  if (!success || !cache_entry.is_dirty()) {
    callback.Run(DRIVE_FILE_OK, entry_proto.Pass());
    return;
  }

  // Gets the cache file path.
  const std::string& resource_id = entry_proto->resource_id();
  const std::string& md5 = entry_proto->file_specific_info().file_md5();
  cache_->GetFileOnUIThread(
      resource_id,
      md5,
      base::Bind(
          &DriveFileSystem::CheckLocalModificationAndRunAfterGetCacheFile,
          ui_weak_ptr_, base::Passed(&entry_proto), callback));
}

void DriveFileSystem::CheckLocalModificationAndRunAfterGetCacheFile(
    scoped_ptr<DriveEntryProto> entry_proto,
    const GetEntryInfoCallback& callback,
    DriveFileError error,
    const FilePath& local_cache_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  // When no dirty cache is found, use the original entry info as is.
  if (error != DRIVE_FILE_OK) {
    callback.Run(DRIVE_FILE_OK, entry_proto.Pass());
    return;
  }

  // If the cache is dirty, obtain the file info from the cache file itself.
  base::PlatformFileInfo* file_info = new base::PlatformFileInfo;
  bool* get_file_info_result = new bool(false);
  google_apis::util::PostBlockingPoolSequencedTaskAndReply(
      FROM_HERE,
      blocking_task_runner_,
      base::Bind(&GetFileInfoOnBlockingPool,
                 local_cache_path,
                 base::Unretained(file_info),
                 base::Unretained(get_file_info_result)),
      base::Bind(&DriveFileSystem::CheckLocalModificationAndRunAfterGetFileInfo,
                 ui_weak_ptr_,
                 base::Passed(&entry_proto),
                 callback,
                 base::Owned(file_info),
                 base::Owned(get_file_info_result)));
}

void DriveFileSystem::CheckLocalModificationAndRunAfterGetFileInfo(
    scoped_ptr<DriveEntryProto> entry_proto,
    const GetEntryInfoCallback& callback,
    base::PlatformFileInfo* file_info,
    bool* get_file_info_result) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (!*get_file_info_result) {
    callback.Run(DRIVE_FILE_ERROR_NOT_FOUND, scoped_ptr<DriveEntryProto>());
    return;
  }

  PlatformFileInfoProto entry_file_info;
  DriveEntry::ConvertPlatformFileInfoToProto(*file_info, &entry_file_info);
  *entry_proto->mutable_file_info() = entry_file_info;
  callback.Run(DRIVE_FILE_OK, entry_proto.Pass());
}

}  // namespace drive
