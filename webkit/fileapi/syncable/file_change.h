// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_FILEAPI_SYNCABLE_FILE_CHANGE_H_
#define WEBKIT_FILEAPI_SYNCABLE_FILE_CHANGE_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "webkit/fileapi/syncable/sync_file_type.h"
#include "webkit/storage/webkit_storage_export.h"

namespace fileapi {

class WEBKIT_STORAGE_EXPORT FileChange {
 public:
  enum ChangeType {
    FILE_CHANGE_ADD_OR_UPDATE,
    FILE_CHANGE_DELETE,
  };

  FileChange(ChangeType change, SyncFileType file_type);

  bool IsAddOrUpdate() const { return change_ == FILE_CHANGE_ADD_OR_UPDATE; }
  bool IsDelete() const { return change_ == FILE_CHANGE_DELETE; }

  bool IsFile() const { return file_type_ == SYNC_FILE_TYPE_FILE; }
  bool IsDirectory() const { return file_type_ == SYNC_FILE_TYPE_DIRECTORY; }
  bool IsTypeUnknown() const { return !IsFile() && !IsDirectory(); }

  ChangeType change() const { return change_; }
  SyncFileType file_type() const { return file_type_; }

  std::string DebugString() const;

  bool operator==(const FileChange& that) const {
    return change() == that.change() &&
        file_type() == that.file_type();
  }

 private:
  ChangeType change_;
  SyncFileType file_type_;
};

class WEBKIT_STORAGE_EXPORT FileChangeList {
 public:
  FileChangeList();
  ~FileChangeList();

  // Updates the list with the |new_change|.
  void Update(const FileChange& new_change);

  size_t size() const { return list_.size(); }
  bool empty() const { return list_.empty(); }
  void clear() { list_.clear(); }
  const std::vector<FileChange>& list() const { return list_; }

  std::string DebugString() const;

 private:
  std::vector<FileChange> list_;
};

}  // namespace fileapi

#endif  // WEBKIT_FILEAPI_SYNCABLE_FILE_CHANGE_H_
