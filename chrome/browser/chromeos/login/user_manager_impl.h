// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_USER_MANAGER_IMPL_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_USER_MANAGER_IMPL_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/observer_list.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/api/sync/profile_sync_service_observer.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_image_manager_impl.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/wallpaper_manager.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class FilePath;
class PrefService;
class ProfileSyncService;

namespace chromeos {

class RemoveUserDelegate;

// Implementation of the UserManager.
class UserManagerImpl : public UserManager,
                        public ProfileSyncServiceObserver,
                        public content::NotificationObserver {
 public:
  virtual ~UserManagerImpl();

  // UserManager implementation:
  virtual UserImageManager* GetUserImageManager() OVERRIDE;
  virtual const UserList& GetUsers() const OVERRIDE;
  virtual void UserLoggedIn(const std::string& email,
                            bool browser_restart) OVERRIDE;
  virtual void DemoUserLoggedIn() OVERRIDE;
  virtual void GuestUserLoggedIn() OVERRIDE;
  virtual void EphemeralUserLoggedIn(const std::string& email) OVERRIDE;
  virtual void SessionStarted() OVERRIDE;
  virtual void RemoveUser(const std::string& email,
                          RemoveUserDelegate* delegate) OVERRIDE;
  virtual void RemoveUserFromList(const std::string& email) OVERRIDE;
  virtual bool IsKnownUser(const std::string& email) const OVERRIDE;
  virtual const User* FindUser(const std::string& email) const OVERRIDE;
  virtual const User* GetLoggedInUser() const OVERRIDE;
  virtual User* GetLoggedInUser() OVERRIDE;
  virtual void SaveUserOAuthStatus(
      const std::string& username,
      User::OAuthTokenStatus oauth_token_status) OVERRIDE;
  virtual void SaveUserDisplayName(const std::string& username,
                                   const string16& display_name) OVERRIDE;
  virtual string16 GetUserDisplayName(
      const std::string& username) const OVERRIDE;
  virtual void SaveUserDisplayEmail(const std::string& username,
                                    const std::string& display_email) OVERRIDE;
  virtual std::string GetUserDisplayEmail(
      const std::string& username) const OVERRIDE;
  virtual void SaveLoggedInUserWallpaperProperties(User::WallpaperType type,
                                                   int index) OVERRIDE;
  virtual void SetLoggedInUserCustomWallpaperLayout(
      ash::WallpaperLayout layout) OVERRIDE;
  virtual bool IsCurrentUserOwner() const OVERRIDE;
  virtual bool IsCurrentUserNew() const OVERRIDE;
  virtual bool IsCurrentUserEphemeral() const OVERRIDE;
  virtual bool IsUserLoggedIn() const OVERRIDE;
  virtual bool IsLoggedInAsDemoUser() const OVERRIDE;
  virtual bool IsLoggedInAsGuest() const OVERRIDE;
  virtual bool IsLoggedInAsStub() const OVERRIDE;
  virtual bool IsSessionStarted() const OVERRIDE;
  virtual bool IsEphemeralUser(const std::string& email) const OVERRIDE;
  virtual void AddObserver(UserManager::Observer* obs) OVERRIDE;
  virtual void RemoveObserver(UserManager::Observer* obs) OVERRIDE;
  virtual void NotifyLocalStateChanged() OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // ProfileSyncServiceObserver implementation.
  virtual void OnStateChanged() OVERRIDE;

 private:
  friend class UserManagerImplWrapper;
  friend class WallpaperManager;
  friend class UserManagerTest;

  UserManagerImpl();

  // Loads |users_| from Local State if the list has not been loaded yet.
  // Subsequent calls have no effect. Must be called on the UI thread.
  void EnsureUsersLoaded();

  // Retrieves trusted device policies and removes users from the persistent
  // list if ephemeral users are enabled. Schedules a callback to itself if
  // trusted device policies are not yet available.
  void RetrieveTrustedDevicePolicies();

  // Returns true if trusted device policies have successfully been retrieved
  // and ephemeral users are enabled.
  bool AreEphemeralUsersEnabled() const;

  // Returns the user with the given email address if found in the persistent
  // list. Returns |NULL| otherwise.
  const User* FindUserInList(const std::string& email) const;

  // Notifies on new user session.
  void NotifyOnLogin();

  // Reads user's oauth token status from local state preferences.
  User::OAuthTokenStatus LoadUserOAuthStatus(const std::string& username) const;

  void SetCurrentUserIsOwner(bool is_current_user_owner);

  // Stores layout and type preference in local state. Runs on UI thread.
  void SaveWallpaperToLocalState(const std::string& username,
                                 const std::string& wallpaper_path,
                                 ash::WallpaperLayout layout,
                                 User::WallpaperType type);

  // Updates current user ownership on UI thread.
  void UpdateOwnership(DeviceSettingsService::OwnershipStatus status,
                       bool is_owner);

  // Triggers an asynchronous ownership check.
  void CheckOwnership();

  // Creates a new User instance.
  User* CreateUser(const std::string& email, bool is_ephemeral) const;

  // Removes the user from the persistent list only. Also removes the user's
  // picture.
  void RemoveUserFromListInternal(const std::string& email);

  // List of all known users. User instances are owned by |this| and deleted
  // when users are removed by |RemoveUserFromListInternal|.
  mutable UserList users_;

  // The logged-in user. NULL until a user has logged in, then points to one
  // of the User instances in |users_|, the |guest_user_| instance or an
  // ephemeral user instance.
  User* logged_in_user_;

  // True if SessionStarted() has been called.
  bool session_started_;

  // Cached flag of whether currently logged-in user is owner or not.
  // May be accessed on different threads, requires locking.
  bool is_current_user_owner_;
  mutable base::Lock is_current_user_owner_lock_;

  // Cached flag of whether the currently logged-in user existed before this
  // login.
  bool is_current_user_new_;

  // Cached flag of whether the currently logged-in user is ephemeral. Storage
  // of persistent information is avoided for such users by not adding them to
  // the user list in local state, not downloading their custom user images and
  // mounting their cryptohomes using tmpfs.
  bool is_current_user_ephemeral_;

  // Cached flag indicating whether ephemeral users are enabled. Defaults to
  // |false| if the value has not been read from trusted device policy yet.
  bool ephemeral_users_enabled_;

  // True if user pod row is showed at login screen.
  bool show_users_;

  // Cached name of device owner. Defaults to empty string if the value has not
  // been read from trusted device policy yet.
  std::string owner_email_;

  content::NotificationRegistrar registrar_;

  // Profile sync service which is observed to take actions after sync
  // errors appear. NOTE: there is no guarantee that it is the current sync
  // service, so do NOT use it outside |OnStateChanged| method.
  ProfileSyncService* observed_sync_service_;

  ObserverList<UserManager::Observer> observer_list_;

  scoped_ptr<UserImageManagerImpl> user_image_manager_;

  DISALLOW_COPY_AND_ASSIGN(UserManagerImpl);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_USER_MANAGER_IMPL_H_
