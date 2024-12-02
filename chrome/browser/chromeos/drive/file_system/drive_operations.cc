// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_system/drive_operations.h"

#include "chrome/browser/chromeos/drive/file_system/copy_operation.h"
#include "chrome/browser/chromeos/drive/file_system/move_operation.h"
#include "chrome/browser/chromeos/drive/file_system/remove_operation.h"

namespace drive {
namespace file_system {

DriveOperations::DriveOperations() {
}

DriveOperations::~DriveOperations() {
}

void DriveOperations::Init(
    google_apis::DriveServiceInterface* drive_service,
    DriveFileSystemInterface* drive_file_system,
    DriveCache* cache,
    DriveResourceMetadata* metadata,
    google_apis::DriveUploaderInterface* uploader,
    scoped_refptr<base::SequencedTaskRunner> blocking_task_runner,
    OperationObserver* observer) {
  copy_operation_.reset(new file_system::CopyOperation(drive_service,
                                                       drive_file_system,
                                                       metadata,
                                                       uploader,
                                                       blocking_task_runner,
                                                       observer));
  move_operation_.reset(new file_system::MoveOperation(drive_service,
                                                       metadata,
                                                       observer));
  remove_operation_.reset(new file_system::RemoveOperation(drive_service,
                                                           cache,
                                                           metadata,
                                                           observer));
}

void DriveOperations::InitForTesting(CopyOperation* copy_operation,
                                     MoveOperation* move_operation,
                                     RemoveOperation* remove_operation) {
  copy_operation_.reset(copy_operation);
  move_operation_.reset(move_operation);
  remove_operation_.reset(remove_operation);
}

void DriveOperations::Copy(const FilePath& src_file_path,
                           const FilePath& dest_file_path,
                           const FileOperationCallback& callback) {
  copy_operation_->Copy(src_file_path, dest_file_path, callback);
}

void DriveOperations::TransferFileFromRemoteToLocal(
    const FilePath& remote_src_file_path,
    const FilePath& local_dest_file_path,
    const FileOperationCallback& callback) {
  copy_operation_->TransferFileFromRemoteToLocal(remote_src_file_path,
                                                 local_dest_file_path,
                                                 callback);
}

void DriveOperations::TransferFileFromLocalToRemote(
    const FilePath& local_src_file_path,
    const FilePath& remote_dest_file_path,
    const FileOperationCallback& callback) {
  copy_operation_->TransferFileFromLocalToRemote(local_src_file_path,
                                                 remote_dest_file_path,
                                                 callback);
}

void DriveOperations::TransferRegularFile(
    const FilePath& local_src_file_path,
    const FilePath& remote_dest_file_path,
    const FileOperationCallback& callback) {
  copy_operation_->TransferRegularFile(local_src_file_path,
                                       remote_dest_file_path,
                                       callback);
}

void DriveOperations::Move(const FilePath& src_file_path,
                           const FilePath& dest_file_path,
                           const FileOperationCallback& callback) {
  move_operation_->Move(src_file_path, dest_file_path, callback);
}

void DriveOperations::Remove(const FilePath& file_path,
                             bool is_recursive,
                             const FileOperationCallback& callback) {
  remove_operation_->Remove(file_path, is_recursive, callback);
}
}  // namespace file_system
}  // namespace drive
