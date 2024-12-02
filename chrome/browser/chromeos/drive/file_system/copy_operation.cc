// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_system/copy_operation.h"

#include "base/file_util.h"
#include "base/json/json_file_value_serializer.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/drive_cache.h"
#include "chrome/browser/chromeos/drive/drive_file_system.h"
#include "chrome/browser/chromeos/drive/drive_file_system_util.h"
#include "chrome/browser/chromeos/drive/drive_files.h"
#include "chrome/browser/google_apis/drive_service_interface.h"
#include "chrome/browser/google_apis/drive_upload_error.h"
#include "chrome/browser/google_apis/gdata_util.h"
#include "content/public/browser/browser_thread.h"
#include "net/base/mime_util.h"

using content::BrowserThread;

namespace drive {
namespace file_system {

namespace {

const char kMimeTypeOctetStream[] = "application/octet-stream";

// Copies a file from |src_file_path| to |dest_file_path| on the local
// file system using file_util::CopyFile. |error| is set to
// DRIVE_FILE_OK on success or DRIVE_FILE_ERROR_FAILED
// otherwise.
void CopyLocalFileOnBlockingPool(const FilePath& src_file_path,
                                 const FilePath& dest_file_path,
                                 DriveFileError* error) {
  DCHECK(error);

  *error = file_util::CopyFile(src_file_path, dest_file_path) ?
      DRIVE_FILE_OK : DRIVE_FILE_ERROR_FAILED;
}

void RunFileOperationCallbackHelper(
    const FileOperationCallback& callback,
    DriveFileError* error) {
  DCHECK(error);

  if (!callback.is_null())
    callback.Run(*error);
}

// Gets the file size and the content type of |local_file|.
void GetLocalFileInfoOnBlockingPool(const FilePath& local_file,
                                    DriveFileError* error,
                                    int64* file_size,
                                    std::string* content_type) {
  DCHECK(error);
  DCHECK(file_size);
  DCHECK(content_type);

  if (!net::GetMimeTypeFromExtension(local_file.Extension(), content_type))
    *content_type = kMimeTypeOctetStream;

  *file_size = 0;
  *error = file_util::GetFileSize(local_file, file_size) ?
      DRIVE_FILE_OK :
      DRIVE_FILE_ERROR_NOT_FOUND;
}

// Helper function called upon completion of AddUploadFile invoked by
// OnTransferCompleted.
// TODO(mtomasz): The same method is in drive_file_system.cc. Share it.
void OnAddUploadFileCompleted(const FileOperationCallback& callback,
                              DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!callback.is_null())
    callback.Run(error);
}

// Checks if a local file at |local_file_path| is a JSON file referencing a
// hosted document on blocking pool, and if so, gets the resource ID of the
// document.
void GetDocumentResourceIdOnBlockingPool(
    const FilePath& local_file_path,
    std::string* resource_id) {
  DCHECK(resource_id);

  if (DocumentEntry::HasHostedDocumentExtension(local_file_path)) {
    std::string error;
    DictionaryValue* dict_value = NULL;
    JSONFileValueSerializer serializer(local_file_path);
    scoped_ptr<Value> value(serializer.Deserialize(NULL, &error));
    if (value.get() && value->GetAsDictionary(&dict_value))
      dict_value->GetString("resource_id", resource_id);
  }
}

}  // namespace

// CopyOperation::StartFileUploadParams implementation.
struct CopyOperation::StartFileUploadParams {
  StartFileUploadParams(const FilePath& in_local_file_path,
                        const FilePath& in_remote_file_path,
                        const FileOperationCallback& in_callback)
      : local_file_path(in_local_file_path),
        remote_file_path(in_remote_file_path),
        callback(in_callback) {}

  const FilePath local_file_path;
  const FilePath remote_file_path;
  const FileOperationCallback callback;
};

CopyOperation::CopyOperation(
    google_apis::DriveServiceInterface* drive_service,
    DriveFileSystemInterface* drive_file_system,
    DriveResourceMetadata* metadata,
    google_apis::DriveUploaderInterface* uploader,
    scoped_refptr<base::SequencedTaskRunner> blocking_task_runner,
    OperationObserver* observer)
  : drive_service_(drive_service),
    drive_file_system_(drive_file_system),
    metadata_(metadata),
    uploader_(uploader),
    blocking_task_runner_(blocking_task_runner),
    observer_(observer),
    weak_ptr_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
}

CopyOperation::~CopyOperation() {
}

void CopyOperation::Copy(const FilePath& src_file_path,
                         const FilePath& dest_file_path,
                         const FileOperationCallback& callback) {
  BrowserThread::CurrentlyOn(BrowserThread::UI);
  DCHECK(!callback.is_null());

  metadata_->GetEntryInfoPairByPaths(
      src_file_path,
      dest_file_path.DirName(),
      base::Bind(&CopyOperation::CopyAfterGetEntryInfoPair,
                 weak_ptr_factory_.GetWeakPtr(),
                 dest_file_path,
                 callback));
}

void CopyOperation::TransferFileFromRemoteToLocal(
    const FilePath& remote_src_file_path,
    const FilePath& local_dest_file_path,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  drive_file_system_->GetFileByPath(
      remote_src_file_path,
      base::Bind(&CopyOperation::OnGetFileCompleteForTransferFile,
                 weak_ptr_factory_.GetWeakPtr(),
                 local_dest_file_path,
                 callback),
      google_apis::GetContentCallback());
}

void CopyOperation::OnGetFileCompleteForTransferFile(
    const FilePath& local_dest_file_path,
    const FileOperationCallback& callback,
    DriveFileError error,
    const FilePath& local_file_path,
    const std::string& unused_mime_type,
    DriveFileType file_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error);
    return;
  }

  // GetFileByPath downloads the file from Drive to a local cache, which is then
  // copied to the actual destination path on the local file system using
  // CopyLocalFileOnBlockingPool.
  DriveFileError* copy_file_error =
      new DriveFileError(DRIVE_FILE_OK);
  google_apis::util::PostBlockingPoolSequencedTaskAndReply(
      FROM_HERE,
      blocking_task_runner_,
      base::Bind(&CopyLocalFileOnBlockingPool,
                 local_file_path,
                 local_dest_file_path,
                 copy_file_error),
      base::Bind(&RunFileOperationCallbackHelper,
                 callback,
                 base::Owned(copy_file_error)));
}


void CopyOperation::TransferFileFromLocalToRemote(
    const FilePath& local_src_file_path,
    const FilePath& remote_dest_file_path,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  // Make sure the destination directory exists.
  metadata_->GetEntryInfoByPath(
      remote_dest_file_path.DirName(),
      base::Bind(
          &CopyOperation::TransferFileFromLocalToRemoteAfterGetEntryInfo,
          weak_ptr_factory_.GetWeakPtr(),
          local_src_file_path,
          remote_dest_file_path,
          callback));
}

void CopyOperation::TransferRegularFile(
    const FilePath& local_file_path,
    const FilePath& remote_dest_file_path,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DriveFileError* error =
      new DriveFileError(DRIVE_FILE_OK);
  int64* file_size = new int64;
  std::string* content_type = new std::string;
  google_apis::util::PostBlockingPoolSequencedTaskAndReply(
      FROM_HERE,
      blocking_task_runner_,
      base::Bind(&GetLocalFileInfoOnBlockingPool,
                 local_file_path,
                 error,
                 file_size,
                 content_type),
      base::Bind(&CopyOperation::StartFileUpload,
                 weak_ptr_factory_.GetWeakPtr(),
                 StartFileUploadParams(local_file_path,
                                       remote_dest_file_path,
                                       callback),
                 base::Owned(error),
                 base::Owned(file_size),
                 base::Owned(content_type)));
}

void CopyOperation::CopyDocumentToDirectory(
    const FilePath& dir_path,
    const std::string& resource_id,
    const FilePath::StringType& new_name,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  drive_service_->CopyDocument(
      resource_id,
      new_name,
      base::Bind(&CopyOperation::OnCopyDocumentCompleted,
                 weak_ptr_factory_.GetWeakPtr(),
                 dir_path,
                 callback));
}

void CopyOperation::OnCopyDocumentCompleted(
    const FilePath& dir_path,
    const FileOperationCallback& callback,
    GDataErrorCode status,
    scoped_ptr<base::Value> data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK) {
    callback.Run(error);
    return;
  }

  // |entry| was added in the root directory on the server, so we should
  // first add it to |root_| to mirror the state and then move it to the
  // destination directory by MoveEntryFromRootDirectory().
  metadata_->AddEntryToDirectory(
      metadata_->root()->GetFilePath(),
      scoped_ptr<DocumentEntry>(DocumentEntry::ExtractAndParse(*data)),
      base::Bind(&CopyOperation::MoveEntryFromRootDirectory,
                 weak_ptr_factory_.GetWeakPtr(),
                 dir_path,
                 callback));
}

// TODO(mtomasz): Share with the file_system::MoveOperation class.
void CopyOperation::MoveEntryFromRootDirectory(
    const FilePath& directory_path,
    const FileOperationCallback& callback,
    DriveFileError error,
    const FilePath& file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK_EQ(kDriveRootDirectory, file_path.DirName().value());

  // Return if there is an error or |dir_path| is the root directory.
  if (error != DRIVE_FILE_OK ||
      directory_path == FilePath(kDriveRootDirectory)) {
    callback.Run(error);
    return;
  }

  metadata_->GetEntryInfoPairByPaths(
      file_path,
      directory_path,
      base::Bind(
          &CopyOperation::MoveEntryFromRootDirectoryAfterGetEntryInfoPair,
          weak_ptr_factory_.GetWeakPtr(),
          callback));
}

// TODO(mtomasz): Share with the file_system::MoveOperation class.
void CopyOperation::MoveEntryFromRootDirectoryAfterGetEntryInfoPair(
    const FileOperationCallback& callback,
    scoped_ptr<EntryInfoPairResult> result) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK(result.get());

  if (result->first.error != DRIVE_FILE_OK) {
    callback.Run(result->first.error);
    return;
  } else if (result->second.error != DRIVE_FILE_OK) {
    callback.Run(result->second.error);
    return;
  }

  scoped_ptr<DriveEntryProto> src_proto = result->first.proto.Pass();
  scoped_ptr<DriveEntryProto> dir_proto = result->second.proto.Pass();

  if (!dir_proto->file_info().is_directory()) {
    callback.Run(DRIVE_FILE_ERROR_NOT_A_DIRECTORY);
    return;
  }

  const FilePath& file_path = result->first.path;
  const FilePath& dir_path = result->second.path;
  drive_service_->AddResourceToDirectory(
      GURL(dir_proto->content_url()),
      GURL(src_proto->edit_url()),
      base::Bind(&CopyOperation::MoveEntryToDirectory,
                 weak_ptr_factory_.GetWeakPtr(),
                 file_path,
                 dir_path,
                 base::Bind(&CopyOperation::NotifyAndRunFileOperationCallback,
                            weak_ptr_factory_.GetWeakPtr(),
                            callback)));
}
void CopyOperation::MoveEntryToDirectory(
    const FilePath& file_path,
    const FilePath& directory_path,
    const FileMoveCallback& callback,
    GDataErrorCode status,
    const GURL& /* document_url */) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  const DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK) {
    callback.Run(error, FilePath());
    return;
  }

  metadata_->MoveEntryToDirectory(file_path, directory_path, callback);
}

void CopyOperation::NotifyAndRunFileOperationCallback(
    const FileOperationCallback& callback,
    DriveFileError error,
    const FilePath& moved_file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error == DRIVE_FILE_OK)
    observer_->OnDirectoryChangedByOperation(moved_file_path.DirName());

  callback.Run(error);
}

void CopyOperation::CopyAfterGetEntryInfoPair(
    const FilePath& dest_file_path,
    const FileOperationCallback& callback,
    scoped_ptr<EntryInfoPairResult> result) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK(result.get());

  if (result->first.error != DRIVE_FILE_OK) {
    callback.Run(result->first.error);
    return;
  } else if (result->second.error != DRIVE_FILE_OK) {
    callback.Run(result->second.error);
    return;
  }

  scoped_ptr<DriveEntryProto> src_file_proto = result->first.proto.Pass();
  scoped_ptr<DriveEntryProto> dest_parent_proto = result->second.proto.Pass();

  if (!dest_parent_proto->file_info().is_directory()) {
    callback.Run(DRIVE_FILE_ERROR_NOT_A_DIRECTORY);
    return;
  } else if (src_file_proto->file_info().is_directory()) {
    // TODO(kochi): Implement copy for directories. In the interim,
    // we handle recursive directory copy in the file manager.
    // crbug.com/141596
    callback.Run(DRIVE_FILE_ERROR_INVALID_OPERATION);
    return;
  }

  if (src_file_proto->file_specific_info().is_hosted_document()) {
    CopyDocumentToDirectory(dest_file_path.DirName(),
                            src_file_proto->resource_id(),
                            // Drop the document extension, which should not be
                            // in the document title.
                            dest_file_path.BaseName().RemoveExtension().value(),
                            callback);
    return;
  }

  // TODO(kochi): Reimplement this once the server API supports
  // copying of regular files directly on the server side. crbug.com/138273
  const FilePath& src_file_path = result->first.path;
  drive_file_system_->GetFileByPath(
      src_file_path,
      base::Bind(&CopyOperation::OnGetFileCompleteForCopy,
                 weak_ptr_factory_.GetWeakPtr(),
                 dest_file_path,
                 callback),
      google_apis::GetContentCallback());
}

void CopyOperation::OnGetFileCompleteForCopy(
    const FilePath& remote_dest_file_path,
    const FileOperationCallback& callback,
    DriveFileError error,
    const FilePath& local_file_path,
    const std::string& unused_mime_type,
    DriveFileType file_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error);
    return;
  }

  // This callback is only triggered for a regular file via Copy().
  DCHECK_EQ(REGULAR_FILE, file_type);
  TransferRegularFile(local_file_path, remote_dest_file_path, callback);
}

void CopyOperation::StartFileUpload(
    const StartFileUploadParams& params,
    DriveFileError* error,
    int64* file_size,
    std::string* content_type) {
  // This method needs to run on the UI thread as required by
  // DriveUploader::UploadNewFile().
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(error);
  DCHECK(file_size);
  DCHECK(content_type);

  if (*error != DRIVE_FILE_OK) {
    if (!params.callback.is_null())
      params.callback.Run(*error);

    return;
  }

  // Make sure the destination directory exists.
  metadata_->GetEntryInfoByPath(
      params.remote_file_path.DirName(),
      base::Bind(
          &CopyOperation::StartFileUploadAfterGetEntryInfo,
          weak_ptr_factory_.GetWeakPtr(),
          params,
          *file_size,
          *content_type));
}

void CopyOperation::StartFileUploadAfterGetEntryInfo(
    const StartFileUploadParams& params,
    int64 file_size,
    std::string content_type,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (entry_proto.get() && !entry_proto->file_info().is_directory())
    error = DRIVE_FILE_ERROR_NOT_A_DIRECTORY;

  if (error != DRIVE_FILE_OK) {
    if (!params.callback.is_null())
      params.callback.Run(error);
    return;
  }
  DCHECK(entry_proto.get());

  uploader_->UploadNewFile(GURL(entry_proto->upload_url()),
                           params.remote_file_path,
                           params.local_file_path,
                           params.remote_file_path.BaseName().value(),
                           content_type,
                           file_size,
                           file_size,
                           base::Bind(&CopyOperation::OnTransferCompleted,
                                      weak_ptr_factory_.GetWeakPtr(),
                                      params.callback),
                           google_apis::UploaderReadyCallback());
}

void CopyOperation::OnTransferCompleted(
    const FileOperationCallback& callback,
    google_apis::DriveUploadError error,
    const FilePath& drive_path,
    const FilePath& file_path,
    scoped_ptr<DocumentEntry> document_entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (error == google_apis::DRIVE_UPLOAD_OK && document_entry.get()) {
    drive_file_system_->AddUploadedFile(
        google_apis::UPLOAD_NEW_FILE,
        drive_path.DirName(),
        document_entry.Pass(),
        file_path,
        DriveCache::FILE_OPERATION_COPY,
        base::Bind(&OnAddUploadFileCompleted, callback, DRIVE_FILE_OK));
  } else if (!callback.is_null()) {
    callback.Run(DriveUploadErrorToDriveFileError(error));
  }
}

void CopyOperation::TransferFileFromLocalToRemoteAfterGetEntryInfo(
    const FilePath& local_src_file_path,
    const FilePath& remote_dest_file_path,
    const FileOperationCallback& callback,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error);
    return;
  }

  DCHECK(entry_proto.get());
  if (!entry_proto->file_info().is_directory()) {
    // The parent of |remote_dest_file_path| is not a directory.
    callback.Run(DRIVE_FILE_ERROR_NOT_A_DIRECTORY);
    return;
  }

  std::string* resource_id = new std::string;
  google_apis::util::PostBlockingPoolSequencedTaskAndReply(
      FROM_HERE,
      blocking_task_runner_,
      base::Bind(&GetDocumentResourceIdOnBlockingPool,
                 local_src_file_path,
                 resource_id),
      base::Bind(&CopyOperation::TransferFileForResourceId,
                 weak_ptr_factory_.GetWeakPtr(),
                 local_src_file_path,
                 remote_dest_file_path,
                 callback,
                 base::Owned(resource_id)));
}

void CopyOperation::TransferFileForResourceId(
    const FilePath& local_file_path,
    const FilePath& remote_dest_file_path,
    const FileOperationCallback& callback,
    std::string* resource_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(resource_id);
  DCHECK(!callback.is_null());

  if (resource_id->empty()) {
    // If |resource_id| is empty, upload the local file as a regular file.
    TransferRegularFile(local_file_path, remote_dest_file_path, callback);
    return;
  }

  // Otherwise, copy the document on the server side and add the new copy
  // to the destination directory (collection).
  CopyDocumentToDirectory(
      remote_dest_file_path.DirName(),
      *resource_id,
      // Drop the document extension, which should not be
      // in the document title.
      remote_dest_file_path.BaseName().RemoveExtension().value(),
      callback);
}

}  // namespace file_system
}  // namespace drive
