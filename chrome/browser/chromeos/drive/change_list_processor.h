// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_CHANGE_LIST_PROCESSOR_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_CHANGE_LIST_PROCESSOR_H_

#include <map>
#include <set>
#include <string>

#include "base/files/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "chrome/browser/chromeos/drive/file_errors.h"
#include "url/gurl.h"

namespace google_apis {
class AboutResource;
class ResourceList;
}  // google_apis

namespace drive {

class ResourceEntry;

namespace internal {

class ResourceMetadata;

// Holds information needed to fetch contents of a directory.
// This object is copyable.
class DirectoryFetchInfo {
 public:
  DirectoryFetchInfo() : changestamp_(0) {}
  DirectoryFetchInfo(const std::string& resource_id,
                     int64 changestamp)
      : resource_id_(resource_id),
        changestamp_(changestamp) {
  }

  // Returns true if the object is empty.
  bool empty() const { return resource_id_.empty(); }

  // Resource ID of the directory.
  const std::string& resource_id() const { return resource_id_; }

  // Changestamp of the directory. The changestamp is used to determine if
  // the directory contents should be fetched.
  int64 changestamp() const { return changestamp_; }

  // Returns a string representation of this object.
  std::string ToString() const;

 private:
  const std::string resource_id_;
  const int64 changestamp_;
};

// Class to represent a change list.
class ChangeList {
 public:
  explicit ChangeList(const google_apis::ResourceList& resource_list);
  ~ChangeList();

  const std::vector<ResourceEntry>& entries() const { return entries_; }
  std::vector<ResourceEntry>* mutable_entries() { return &entries_; }
  const GURL& next_url() const { return next_url_; }
  int64 largest_changestamp() const { return largest_changestamp_; }

 private:
  std::vector<ResourceEntry> entries_;
  GURL next_url_;
  int64 largest_changestamp_;

  DISALLOW_COPY_AND_ASSIGN(ChangeList);
};

// ChangeListProcessor is used to process change lists, or full resource
// lists from WAPI (codename for Documents List API) or Google Drive API, and
// updates the resource metadata stored locally.
class ChangeListProcessor {
 public:
  typedef std::map<std::string /* resource_id */, ResourceEntry>
      ResourceEntryMap;

  // Class used to record UMA stats with ConvertToMap().
  class ChangeListToEntryMapUMAStats;

  explicit ChangeListProcessor(ResourceMetadata* resource_metadata);
  ~ChangeListProcessor();

  // Applies change lists or full resource lists to |resource_metadata_|.
  //
  // |is_delta_update| determines the type of input data to process, whether
  // it is full resource lists (false) or change lists (true).
  //
  // Must be run on the same task runner as |resource_metadata_| uses.
  //
  // TODO(hashimoto): Report error on failures.
  void Apply(scoped_ptr<google_apis::AboutResource> about_resource,
             ScopedVector<ChangeList> change_lists,
             bool is_delta_update);

  // Converts change lists into a ResourceEntryMap.
  // |uma_stats| may be NULL.
  static void ConvertToMap(ScopedVector<ChangeList> change_lists,
                           ResourceEntryMap* entry_map,
                           ChangeListToEntryMapUMAStats* uma_stats);

  // The set of changed directories as a result of change list processing.
  const std::set<base::FilePath>& changed_dirs() const { return changed_dirs_; }

  // Updates the changestamp of a directory according to |directory_fetch_info|
  // and adds or refreshes the child entries from |entry_map|.
  static FileError RefreshDirectory(
      ResourceMetadata* resource_metadata,
      const DirectoryFetchInfo& directory_fetch_info,
      const ResourceEntryMap& entry_map,
      base::FilePath* out_file_path);

 private:
  // Applies the pre-processed metadata from entry_map_ onto the resource
  // metadata. If this is not delta update (i.e. |is_delta_update| is false),
  // |about_resource| must not be null.
  void ApplyEntryMap(bool is_delta_update,
                     int64 changestamp,
                     scoped_ptr<google_apis::AboutResource> about_resource);

  // Apply the next item from entry_map_ to the file system. The async
  // version posts to the message loop to avoid recursive stack-overflow.
  void ApplyNextEntry();

  // Apply |entry| to resource_metadata_.
  void ApplyEntry(const ResourceEntry& entry);

  // Helper function to add |entry| to its parent. Updates changed_dirs_
  // as a side effect.
  void AddEntry(const ResourceEntry& entry);

  // Removes entry pointed to by |resource_id| from its parent. Updates
  // changed_dirs_ as a side effect.
  void RemoveEntry(const ResourceEntry& entry);

  // Refreshes ResourceMetadata entry that has the same resource_id as
  // |entry| with |entry|. Updates changed_dirs_ as a side effect.
  void RefreshEntry(const ResourceEntry& entry);

  // Updates the root directory entry. changestamp will be updated.
  void UpdateRootEntry(int64 largest_changestamp);

  // Adds the directories changed by the update on |entry| to |changed_dirs_|.
  void UpdateChangedDirs(const ResourceEntry& entry);

  ResourceMetadata* resource_metadata_;  // Not owned.

  ResourceEntryMap entry_map_;
  std::set<base::FilePath> changed_dirs_;

  DISALLOW_COPY_AND_ASSIGN(ChangeListProcessor);
};

}  // namespace internal
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_CHANGE_LIST_PROCESSOR_H_
