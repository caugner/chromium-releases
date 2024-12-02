// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_SYSTEM_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_SYSTEM_SERVICE_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/chromeos/drive/drive_file_error.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "sync/notifier/invalidation_handler.h"

class FilePath;

namespace google_apis {
class DriveServiceInterface;
class DriveUploader;
}

namespace drive {

class DriveCache;
class DriveDownloadObserver;
class DriveFileSystemInterface;
class DriveWebAppsRegistry;
class FileWriteHelper;
class DriveSyncClient;
class DrivePrefetcher;
class StaleCacheFilesRemover;

// DriveSystemService runs the Drive system, including the Drive file system
// implementation for the file manager, and some other sub systems.
//
// The class is essentially a container that manages lifetime of the objects
// that are used to run the Drive system. The DriveSystemService object is
// created per-profile.
class DriveSystemService : public ProfileKeyedService,
                           public syncer::InvalidationHandler {
 public:
  google_apis::DriveServiceInterface* drive_service() {
    return drive_service_.get();
  }

  DriveCache* cache() { return cache_; }
  DriveFileSystemInterface* file_system() { return file_system_.get(); }
  FileWriteHelper* file_write_helper() { return file_write_helper_.get(); }
  google_apis::DriveUploader* uploader() { return uploader_.get(); }
  DriveWebAppsRegistry* webapps_registry() { return webapps_registry_.get(); }

  // Clears all the local cache files and in-memory data, and remounts the file
  // system.
  void ClearCacheAndRemountFileSystem(
      const base::Callback<void(bool)>& callback);

  // Reloads and remounts the file system.
  void ReloadAndRemountFileSystem();

  // ProfileKeyedService override:
  virtual void Shutdown() OVERRIDE;

  // syncer::InvalidationHandler implementation.
  virtual void OnInvalidatorStateChange(
      syncer::InvalidatorState state) OVERRIDE;
  virtual void OnIncomingInvalidation(
      const syncer::ObjectIdInvalidationMap& invalidation_map,
      syncer::IncomingInvalidationSource source) OVERRIDE;

 private:
  explicit DriveSystemService(Profile* profile);
  virtual ~DriveSystemService();

  // Returns true if Drive is enabled.
  // Must be called on UI thread.
  bool IsDriveEnabled();

  // Initializes the object. This function should be called before any
  // other functions.
  void Initialize(google_apis::DriveServiceInterface* drive_service,
                  const FilePath& cache_root);

  // Registers remote file system proxy for drive mount point.
  void AddDriveMountPoint();
  // Unregisters drive mount point from File API.
  void RemoveDriveMountPoint();

  // Adds back the drive mount point. Used to implement ClearCache().
  void AddBackDriveMountPoint(const base::Callback<void(bool)>& callback,
                              DriveFileError error,
                              const FilePath& file_path);

  // Called when cache initialization is done. Continues initialization if
  // the cache initialization is successful.
  void OnCacheInitialized(bool success);

  // Disables Drive. Used to disable Drive when needed (ex. initialization of
  // the Drive cache failed).
  // Must be called on UI thread.
  void DisableDrive();

  friend class DriveSystemServiceFactory;

  Profile* profile_;
  // True if Drive is disabled due to initialization errors.
  bool drive_disabled_;

  // True once this is registered to listen to the Drive updates.
  bool push_notification_registered_;

  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;
  DriveCache* cache_;
  scoped_ptr<google_apis::DriveServiceInterface> drive_service_;
  scoped_ptr<google_apis::DriveUploader> uploader_;
  scoped_ptr<DriveWebAppsRegistry> webapps_registry_;
  scoped_ptr<DriveFileSystemInterface> file_system_;
  scoped_ptr<FileWriteHelper> file_write_helper_;
  scoped_ptr<DriveDownloadObserver> download_observer_;
  scoped_ptr<DriveSyncClient> sync_client_;
  scoped_ptr<DrivePrefetcher> prefetcher_;
  scoped_ptr<StaleCacheFilesRemover> stale_cache_files_remover_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<DriveSystemService> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(DriveSystemService);
};

// Singleton that owns all DriveSystemServices and associates them with
// Profiles.
class DriveSystemServiceFactory : public ProfileKeyedServiceFactory {
 public:
  // Returns the DriveSystemService for |profile|, creating it if it is not
  // yet created.
  //
  // This function starts returning NULL if Drive is disabled, even if this
  // function previously returns a non-NULL object. In other words, clients
  // can assume that Drive is enabled if this function returns a non-NULL
  // object.
  static DriveSystemService* GetForProfile(Profile* profile);

  // Returns the DriveSystemService that is already associated with |profile|,
  // if it is not yet created it will return NULL.
  //
  // This function starts returning NULL if Drive is disabled. See also the
  // comment at GetForProfile().
  static DriveSystemService* FindForProfile(Profile* profile);

  // Returns the DriveSystemServiceFactory instance.
  static DriveSystemServiceFactory* GetInstance();

  // Sets drive service that should be used to initialize file system in test.
  // Should be called before the service is created.
  // Please, make sure |drive_service| gets deleted if no system service is
  // created (e.g. by calling this method with NULL).
  static void set_drive_service_for_test(
      google_apis::DriveServiceInterface* drive_service);

  // Sets root path for the cache used in test. Should be called before the
  // service is created.
  // If |cache_root| is not empty, new string object will be created. Please,
  // make sure it gets deleted if no system service is created (e.g. by calling
  // this method with empty string).
  static void set_cache_root_for_test(const std::string& cache_root);

 private:
  friend struct DefaultSingletonTraits<DriveSystemServiceFactory>;

  DriveSystemServiceFactory();
  virtual ~DriveSystemServiceFactory();

  // ProfileKeyedServiceFactory:
  virtual ProfileKeyedService* BuildServiceInstanceFor(
      Profile* profile) const OVERRIDE;
};

}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_SYSTEM_SERVICE_H_
