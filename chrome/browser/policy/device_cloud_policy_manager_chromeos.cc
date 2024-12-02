// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/device_cloud_policy_manager_chromeos.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "chrome/browser/chromeos/system/statistics_provider.h"
#include "chrome/browser/policy/cloud_policy_client.h"
#include "chrome/browser/policy/device_cloud_policy_store_chromeos.h"
#include "chrome/browser/policy/device_management_service.h"
#include "chrome/browser/policy/enrollment_handler_chromeos.h"
#include "chrome/browser/policy/enterprise_install_attributes.h"
#include "chrome/common/pref_names.h"

namespace policy {

namespace {

// MachineInfo key names.
const char kMachineInfoSystemHwqual[] = "hardware_class";

// These are the machine serial number keys that we check in order until we
// find a non-empty serial number. The VPD spec says the serial number should be
// in the "serial_number" key for v2+ VPDs. However, legacy devices used a
// different keys to report their serial number, which we fall back to if
// "serial_number" is not present.
//
// Product_S/N is still special-cased due to inconsistencies with serial
// numbers on Lumpy devices: On these devices, serial_number is identical to
// Product_S/N with an appended checksum. Unfortunately, the sticker on the
// packaging doesn't include that checksum either (the sticker on the device
// does though!). The former sticker is the source of the serial number used by
// device management service, so we prefer Product_S/N over serial number to
// match the server.
//
// TODO(mnissler): Move serial_number back to the top once the server side uses
// the correct serial number.
const char* kMachineInfoSerialNumberKeys[] = {
  "Product_S/N",    // Lumpy/Alex devices
  "serial_number",  // VPD v2+ devices
  "Product_SN",     // Mario
  "sn",             // old ZGB devices (more recent ones use serial_number)
};

}  // namespace

DeviceCloudPolicyManagerChromeOS::DeviceCloudPolicyManagerChromeOS(
    scoped_ptr<DeviceCloudPolicyStoreChromeOS> store,
    EnterpriseInstallAttributes* install_attributes)
    : CloudPolicyManager(make_scoped_ptr<CloudPolicyStore>(store.get())),
      device_store_(store.release()),  // Hack: retain |store| till here.
      install_attributes_(install_attributes),
      device_management_service_(NULL),
      local_state_(NULL) {}

DeviceCloudPolicyManagerChromeOS::~DeviceCloudPolicyManagerChromeOS() {}

void DeviceCloudPolicyManagerChromeOS::Connect(
    PrefService* local_state,
    DeviceManagementService* device_management_service) {
  CHECK(!device_management_service_);
  CHECK(device_management_service);
  CHECK(local_state);

  local_state_ = local_state;
  device_management_service_ = device_management_service;

  StartIfManaged();
}

void DeviceCloudPolicyManagerChromeOS::StartEnrollment(
    const std::string& auth_token,
    const AllowedDeviceModes& allowed_device_modes,
    const EnrollmentCallback& callback) {
  CHECK(device_management_service_);
  ShutdownService();

  enrollment_handler_.reset(
      new EnrollmentHandlerChromeOS(
          device_store_, install_attributes_, CreateClient(), auth_token,
          allowed_device_modes,
          base::Bind(&DeviceCloudPolicyManagerChromeOS::EnrollmentCompleted,
                     base::Unretained(this), callback)));
  enrollment_handler_->StartEnrollment();
}

void DeviceCloudPolicyManagerChromeOS::CancelEnrollment() {
  if (enrollment_handler_.get()) {
    enrollment_handler_.reset();
    StartIfManaged();
  }
}

void DeviceCloudPolicyManagerChromeOS::OnStoreLoaded(CloudPolicyStore* store) {
  CloudPolicyManager::OnStoreLoaded(store);

  if (!enrollment_handler_.get())
    StartIfManaged();
}

// static
std::string DeviceCloudPolicyManagerChromeOS::GetMachineID() {
  std::string machine_id;
  chromeos::system::StatisticsProvider* provider =
      chromeos::system::StatisticsProvider::GetInstance();
  for (size_t i = 0; i < arraysize(kMachineInfoSerialNumberKeys); i++) {
    if (provider->GetMachineStatistic(kMachineInfoSerialNumberKeys[i],
                                      &machine_id) &&
        !machine_id.empty()) {
      break;
    }
  }

  if (machine_id.empty())
    LOG(WARNING) << "Failed to get machine id.";

  return machine_id;
}

// static
std::string DeviceCloudPolicyManagerChromeOS::GetMachineModel() {
  std::string machine_model;
  chromeos::system::StatisticsProvider* provider =
      chromeos::system::StatisticsProvider::GetInstance();
  if (!provider->GetMachineStatistic(kMachineInfoSystemHwqual, &machine_model))
    LOG(WARNING) << "Failed to get machine model.";

  return machine_model;
}

scoped_ptr<CloudPolicyClient> DeviceCloudPolicyManagerChromeOS::CreateClient() {
  return make_scoped_ptr(
      new CloudPolicyClient(GetMachineID(), GetMachineModel(),
                            USER_AFFILIATION_NONE, POLICY_SCOPE_MACHINE, NULL,
                            device_management_service_));
}

void DeviceCloudPolicyManagerChromeOS::EnrollmentCompleted(
    const EnrollmentCallback& callback,
    EnrollmentStatus status) {
  if (status.status() == EnrollmentStatus::STATUS_SUCCESS) {
    InitializeService(enrollment_handler_->ReleaseClient());
    StartRefreshScheduler(local_state_, prefs::kDevicePolicyRefreshRate);
  } else {
    StartIfManaged();
  }

  enrollment_handler_.reset();
  if (!callback.is_null())
    callback.Run(status);
}

void DeviceCloudPolicyManagerChromeOS::StartIfManaged() {
  if (device_management_service_ &&
      local_state_ &&
      cloud_policy_store()->is_initialized() &&
      cloud_policy_store()->is_managed() &&
      !cloud_policy_service()) {
    InitializeService(CreateClient());
    StartRefreshScheduler(local_state_, prefs::kDevicePolicyRefreshRate);
  }
}

}  // namespace policy
