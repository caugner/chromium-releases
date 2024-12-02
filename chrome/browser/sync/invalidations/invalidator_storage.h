// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Wraps PrefService in an InvalidationStateTracker to allow SyncNotifiers
// to use PrefService as persistence for invalidation state. It is not thread
// safe, and lives on the UI thread.

#ifndef CHROME_BROWSER_SYNC_INVALIDATIONS_INVALIDATOR_STORAGE_H_
#define CHROME_BROWSER_SYNC_INVALIDATIONS_INVALIDATOR_STORAGE_H_

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "sync/internal_api/public/syncable/model_type.h"
#include "sync/notifier/invalidation_state_tracker.h"

class PrefService;

namespace base {
  class DictionaryValue;
}

namespace browser_sync {

// TODO(tim): Bug 124137. We may want to move this outside of sync/ into a
// browser/invalidations directory, or re-organize to have a browser
// subdirectory that contains signin/ sync/ invalidations/ and other cloud
// services.  For now this is still tied to sync while we refactor, so minimize
// churn and keep it here.
class InvalidatorStorage : public base::SupportsWeakPtr<InvalidatorStorage>,
                           public sync_notifier::InvalidationStateTracker {
 public:
  // |pref_service| may be NULL (for unit tests), but in that case no setter
  // methods should be called. Does not own |pref_service|.
  explicit InvalidatorStorage(PrefService* pref_service);
  virtual ~InvalidatorStorage();

  // Erases invalidation versions and state stored on disk.
  void Clear();

  // InvalidationStateTracker implementation.
  virtual sync_notifier::InvalidationVersionMap GetAllMaxVersions() const
      OVERRIDE;
  virtual void SetMaxVersion(syncable::ModelType model_type,
                             int64 max_version) OVERRIDE;
  // TODO(tim): These are not yet used. Bug 124140.
  virtual void SetInvalidationState(const std::string& state) OVERRIDE;
  virtual std::string GetInvalidationState() const OVERRIDE;

 private:
  FRIEND_TEST_ALL_PREFIXES(InvalidatorStorageTest, SerializeEmptyMap);
  FRIEND_TEST_ALL_PREFIXES(InvalidatorStorageTest, DeserializeOutOfRange);
  FRIEND_TEST_ALL_PREFIXES(InvalidatorStorageTest, DeserializeInvalidFormat);
  FRIEND_TEST_ALL_PREFIXES(InvalidatorStorageTest, DeserializeEmptyDictionary);
  FRIEND_TEST_ALL_PREFIXES(InvalidatorStorageTest, DeserializeBasic);

  base::NonThreadSafe non_thread_safe_;

  // Helpers to convert between InvalidationVersionMap <--> DictionaryValue.
  static void DeserializeMap(const base::DictionaryValue* max_versions_dict,
                             sync_notifier::InvalidationVersionMap* map);
  static void SerializeMap(const sync_notifier::InvalidationVersionMap& map,
                           base::DictionaryValue* to_dict);

  // May be NULL.
  PrefService* const pref_service_;

  DISALLOW_COPY_AND_ASSIGN(InvalidatorStorage);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_INVALIDATIONS_INVALIDATOR_STORAGE_H_
