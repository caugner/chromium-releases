// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_host/pepper/device_id_fetcher.h"

#include "base/file_util.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#if defined(OS_CHROMEOS)
#include "chromeos/cryptohome/cryptohome_library.h"
#endif
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_ppapi_host.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_process_host.h"
#include "crypto/encryptor.h"
#include "crypto/random.h"
#include "crypto/sha2.h"
#if defined(ENABLE_RLZ)
#include "rlz/lib/machine_id.h"
#endif

using content::BrowserPpapiHost;
using content::BrowserThread;
using content::RenderProcessHost;

namespace chrome {

namespace {

const char kDRMIdentifierFile[] = "Pepper DRM ID.0";

const uint32_t kSaltLength = 32;

void GetMachineIDAsync(const DeviceIDFetcher::IDCallback& callback) {
  std::string result;
#if defined(OS_WIN) && defined(ENABLE_RLZ)
  rlz_lib::GetMachineId(&result);
#elif defined(OS_CHROMEOS)
  result = chromeos::CryptohomeLibrary::Get()->GetSystemSalt();
  if (result.empty()) {
    // cryptohome must not be running; re-request after a delay.
    const int64 kRequestSystemSaltDelayMs = 500;
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&GetMachineIDAsync, callback),
        base::TimeDelta::FromMilliseconds(kRequestSystemSaltDelayMs));
    return;
  }
#else
  // Not implemented for other platforms.
  NOTREACHED();
#endif
  callback.Run(result);
}

}  // namespace

DeviceIDFetcher::DeviceIDFetcher(int render_process_id)
    : in_progress_(false),
      render_process_id_(render_process_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
}

DeviceIDFetcher::~DeviceIDFetcher() {
}

bool DeviceIDFetcher::Start(const IDCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  if (in_progress_)
    return false;

  in_progress_ = true;
  callback_ = callback;

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&DeviceIDFetcher::CheckPrefsOnUIThread, this));
  return true;
}

// static
void DeviceIDFetcher::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* prefs) {
  prefs->RegisterBooleanPref(prefs::kEnableDRM,
                             true,
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(
      prefs::kDRMSalt,
      "",
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

// static
base::FilePath DeviceIDFetcher::GetLegacyDeviceIDPath(
    const base::FilePath& profile_path) {
  return profile_path.AppendASCII(kDRMIdentifierFile);
}

void DeviceIDFetcher::CheckPrefsOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  Profile* profile = NULL;
  RenderProcessHost* render_process_host =
      RenderProcessHost::FromID(render_process_id_);
  if (render_process_host && render_process_host->GetBrowserContext()) {
    profile = Profile::FromBrowserContext(
        render_process_host->GetBrowserContext());
  }

  if (!profile ||
      profile->IsOffTheRecord() ||
      !profile->GetPrefs()->GetBoolean(prefs::kEnableDRM)) {
    RunCallbackOnIOThread(std::string());
    return;
  }

  // Check if the salt pref is set. If it isn't, set it.
  std::string salt = profile->GetPrefs()->GetString(prefs::kDRMSalt);
  if (salt.empty()) {
    uint8_t salt_bytes[kSaltLength];
    crypto::RandBytes(salt_bytes, arraysize(salt_bytes));
    // Since it will be stored in a string pref, convert it to hex.
    salt = base::HexEncode(salt_bytes, arraysize(salt_bytes));
    profile->GetPrefs()->SetString(prefs::kDRMSalt, salt);
  }

#if defined(OS_CHROMEOS)
  // Try the legacy path first for ChromeOS. We pass the new salt in as well
  // in case the legacy id doesn't exist.
  BrowserThread::PostBlockingPoolTask(
      FROM_HERE,
      base::Bind(&DeviceIDFetcher::LegacyComputeOnBlockingPool,
                 this,
                 profile->GetPath(), salt));
#else
  // Get the machine ID and call ComputeOnUIThread with salt + machine_id.
  GetMachineIDAsync(base::Bind(&DeviceIDFetcher::ComputeOnUIThread,
                               this, salt));
#endif
}

void DeviceIDFetcher::ComputeOnUIThread(const std::string& salt,
                                        const std::string& machine_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (machine_id.empty()) {
    LOG(ERROR) << "Empty machine id";
    RunCallbackOnIOThread(std::string());
    return;
  }

  // Build the identifier as follows:
  // SHA256(machine-id||service||SHA256(machine-id||service||salt))
  std::vector<uint8> salt_bytes;
  if (!base::HexStringToBytes(salt, &salt_bytes))
    salt_bytes.clear();
  if (salt_bytes.size() != kSaltLength) {
    LOG(ERROR) << "Unexpected salt bytes length: " << salt_bytes.size();
    RunCallbackOnIOThread(std::string());
    return;
  }

  char id_buf[256 / 8];  // 256-bits for SHA256
  std::string input = machine_id;
  input.append(kDRMIdentifierFile);
  input.append(salt_bytes.begin(), salt_bytes.end());
  crypto::SHA256HashString(input, &id_buf, sizeof(id_buf));
  std::string id = StringToLowerASCII(
      base::HexEncode(reinterpret_cast<const void*>(id_buf), sizeof(id_buf)));
  input = machine_id;
  input.append(kDRMIdentifierFile);
  input.append(id);
  crypto::SHA256HashString(input, &id_buf, sizeof(id_buf));
  id = StringToLowerASCII(base::HexEncode(
        reinterpret_cast<const void*>(id_buf),
        sizeof(id_buf)));

  RunCallbackOnIOThread(id);
}

// TODO(raymes): This is temporary code to migrate ChromeOS devices to the new
// scheme for generating device IDs. Delete this once we are sure most ChromeOS
// devices have been migrated.
void DeviceIDFetcher::LegacyComputeOnBlockingPool(
    const base::FilePath& profile_path,
    const std::string& salt) {
  std::string id;
  // First check if the legacy device ID file exists on ChromeOS. If it does, we
  // should just return that.
  base::FilePath id_path = GetLegacyDeviceIDPath(profile_path);
  if (base::PathExists(id_path)) {
    if (base::ReadFileToString(id_path, &id) && !id.empty()) {
      RunCallbackOnIOThread(id);
      return;
    }
  }
  // If we didn't find an ID, get the machine ID and call the new code path to
  // generate an ID.
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&GetMachineIDAsync,
                 base::Bind(&DeviceIDFetcher::ComputeOnUIThread,
                            this, salt)));
}

void DeviceIDFetcher::RunCallbackOnIOThread(const std::string& id) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&DeviceIDFetcher::RunCallbackOnIOThread, this, id));
    return;
  }
  in_progress_ = false;
  callback_.Run(id);
}

}  // namespace chrome
