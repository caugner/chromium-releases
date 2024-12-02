// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_FILEAPI_SYNCABLE_CANNED_SYNCABLE_FILE_SYSTEM_H_
#define WEBKIT_FILEAPI_SYNCABLE_CANNED_SYNCABLE_FILE_SYSTEM_H_

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/message_loop.h"
#include "base/platform_file.h"
#include "base/scoped_temp_dir.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/fileapi/local_file_system_test_helper.h"
#include "webkit/fileapi/syncable/sync_status_code.h"
#include "webkit/quota/quota_types.h"

namespace base {
class MessageLoopProxy;
class SingleThreadTaskRunner;
class Thread;
}

namespace net {
class URLRequestContext;
}

namespace quota {
class QuotaManager;
}

namespace fileapi {

class FileSystemContext;
class FileSystemOperation;
class LocalFileSyncContext;

// A canned syncable filesystem for testing.
// This internally creates its own QuotaManager and FileSystemContext
// (as we do so for each isolated application).
class CannedSyncableFileSystem {
 public:
  typedef base::Callback<void(base::PlatformFileError)> StatusCallback;
  typedef base::Callback<void(int64)> WriteCallback;

  CannedSyncableFileSystem(const GURL& origin,
                           const std::string& service,
                           base::SingleThreadTaskRunner* io_task_runner,
                           base::SingleThreadTaskRunner* file_task_runner);
  ~CannedSyncableFileSystem();

  // SetUp must be called before using this instance.
  void SetUp();

  // TearDown must be called before destructing this instance.
  void TearDown();

  // Creates a FileSystemURL for the given (utf8) path string.
  FileSystemURL URL(const std::string& path) const;

  // Initialize this with given |sync_context| if it hasn't
  // been initialized.
  SyncStatusCode MaybeInitializeFileSystemContext(
      LocalFileSyncContext* sync_context);

  // Opens a new syncable file system.
  base::PlatformFileError OpenFileSystem();

  // Accessors.
  FileSystemContext* file_system_context() {
    return file_system_context_.get();
  }
  quota::QuotaManager* quota_manager() { return quota_manager_.get(); }
  GURL origin() const { return test_helper_.origin(); }
  FileSystemType type() const { return test_helper_.type(); }
  quota::StorageType storage_type() const {
    return test_helper_.storage_type();
  }

  // Helper routines to perform file system operations.
  // OpenFileSystem() must have been called before calling any of them.
  // They create an operation and run it on IO task runner, and the operation
  // posts a task on file runner.
  base::PlatformFileError CreateDirectory(const FileSystemURL& url);
  base::PlatformFileError CreateFile(const FileSystemURL& url);
  base::PlatformFileError Copy(const FileSystemURL& src_url,
                               const FileSystemURL& dest_url);
  base::PlatformFileError Move(const FileSystemURL& src_url,
                               const FileSystemURL& dest_url);
  base::PlatformFileError TruncateFile(const FileSystemURL& url, int64 size);
  base::PlatformFileError Remove(const FileSystemURL& url, bool recursive);
  base::PlatformFileError FileExists(const FileSystemURL& url);
  base::PlatformFileError DirectoryExists(const FileSystemURL& url);

  // Returns the # of bytes written (>=0) or an error code (<0).
  int64 Write(net::URLRequestContext* url_request_context,
              const FileSystemURL& url, const GURL& blob_url);
  int64 WriteString(const FileSystemURL& url, const std::string& data);

  // Purges the file system local storage.
  base::PlatformFileError DeleteFileSystem();

  // Retrieves the quota and usage.
  quota::QuotaStatusCode GetUsageAndQuota(int64* usage, int64* quota);

  // ChangeTracker related methods. They run on file task runner.
  void GetChangedURLsInTracker(std::vector<FileSystemURL>* urls);
  void FinalizeSyncForURLInTracker(const FileSystemURL& url);

  // Returns new FileSystemOperation.
  FileSystemOperation* NewOperation();

 private:
  // Operation methods body.
  void DoCreateDirectory(const FileSystemURL& url,
                         const StatusCallback& callback);
  void DoCreateFile(const FileSystemURL& url,
                    const StatusCallback& callback);
  void DoCopy(const FileSystemURL& src_url,
              const FileSystemURL& dest_url,
              const StatusCallback& callback);
  void DoMove(const FileSystemURL& src_url,
              const FileSystemURL& dest_url,
              const StatusCallback& callback);
  void DoTruncateFile(const FileSystemURL& url,
                      int64 size,
                      const StatusCallback& callback);
  void DoRemove(const FileSystemURL& url,
                bool recursive,
                const StatusCallback& callback);
  void DoFileExists(const FileSystemURL& url,
                    const StatusCallback& callback);
  void DoDirectoryExists(const FileSystemURL& url,
                         const StatusCallback& callback);
  void DoWrite(net::URLRequestContext* url_request_context,
               const FileSystemURL& url,
               const GURL& blob_url,
               const WriteCallback& callback);
  void DoWriteString(const FileSystemURL& url,
                     const std::string& data,
                     const WriteCallback& callback);
  void DoGetUsageAndQuota(int64* usage,
                          int64* quota,
                          const quota::StatusCallback& callback);

  // Callbacks.
  void DidOpenFileSystem(base::PlatformFileError result,
                         const std::string& name,
                         const GURL& root);
  void DidInitializeFileSystemContext(SyncStatusCode status);

  ScopedTempDir data_dir_;
  const std::string service_name_;

  scoped_refptr<quota::QuotaManager> quota_manager_;
  scoped_refptr<FileSystemContext> file_system_context_;
  LocalFileSystemTestOriginHelper test_helper_;
  GURL root_url_;
  base::PlatformFileError result_;
  SyncStatusCode sync_status_;

  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> file_task_runner_;

  // Boolean flags mainly for helping debug.
  bool is_filesystem_set_up_;
  bool is_filesystem_opened_;

  DISALLOW_COPY_AND_ASSIGN(CannedSyncableFileSystem);
};

}  // namespace fileapi

#endif  // WEBKIT_FILEAPI_SYNCABLE_CANNED_SYNCABLE_FILE_SYSTEM_H_
