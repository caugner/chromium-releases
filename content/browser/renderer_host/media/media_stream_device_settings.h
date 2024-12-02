// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// MediaStreamDeviceSettings is used to decide which of the available capture
// device to use as well as getting user permission to use the capture device.
// There will be one instance of MediaStreamDeviceSettings handling all
// requests.

// This version always accepts the first device in the list(s), but this will
// soon be changed to ask the user and/or Chrome settings.

// Expected call flow:
// 1. RequestCaptureDeviceUsage() to request usage of capture device.
// 2. SettingsRequester::GetDevices() is called to get a list of available
//    devices.
// 3. AvailableDevices() is called with a list of currently available devices.
// 4. TODO(mflodman) Pick device and get user confirmation.
// Temporary 4. Choose first device of each requested media type.
// 5. Confirm by calling SettingsRequester::DevicesAccepted().
// Repeat step 1 - 5 for new device requests.

// Note that this is still in a development phase and the class will be modified
// to include real UI interaction.

#ifndef CONTENT_BROWSER_RENDERER_HOST_MEDIA_MEDIA_STREAM_DEVICE_SETTINGS_H_
#define CONTENT_BROWSER_RENDERER_HOST_MEDIA_MEDIA_STREAM_DEVICE_SETTINGS_H_

#include <map>
#include <string>

#include "base/basictypes.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/renderer_host/media/media_stream_provider.h"

namespace media_stream {

class MediaStreamDeviceSettingsRequest;
class SettingsRequester;

// MediaStreamDeviceSettings is responsible for getting user permission to use
// a media capture device as well as selecting what device to use.
class CONTENT_EXPORT MediaStreamDeviceSettings
    : public base::SupportsWeakPtr<MediaStreamDeviceSettings> {
 public:
  explicit MediaStreamDeviceSettings(SettingsRequester* requester);
  virtual ~MediaStreamDeviceSettings();

  // Called when a new request of capture device usage is made.
  void RequestCaptureDeviceUsage(const std::string& label,
                                 int render_process_id,
                                 int render_view_id,
                                 const StreamOptions& stream_components,
                                 const std::string& security_origin);

  // Called to pass in an array of available devices for a request represented
  // by |label|. There could be multiple calls for a request.
  void AvailableDevices(const std::string& label, MediaStreamType stream_type,
                        const StreamDeviceInfoArray& devices);

  // Called by the InfoBar when the user grants/denies access to some devices
  // to the webpage. This is placed here, so the request can be cleared from the
  // list of pending requests, instead of letting the InfoBar itself respond to
  // the requester. An empty list of devices means that access has been denied.
  // This method must be called on the IO thread.
  void PostResponse(const std::string& label,
                    const content::MediaStreamDevices& devices);

  // Used for testing only. This function is called to use faked UI, which is
  // needed for server based tests. The first non-opened device(s) will be
  // picked.
  void UseFakeUI();

 private:
  typedef std::map< std::string, MediaStreamDeviceSettingsRequest* >
      SettingsRequests;

  SettingsRequester* requester_;
  SettingsRequests requests_;

  // See comment above for method UseFakeUI. Used for automated testing.
  bool use_fake_ui_;

  DISALLOW_COPY_AND_ASSIGN(MediaStreamDeviceSettings);
};

}  // namespace media_stream

#endif  // CONTENT_BROWSER_RENDERER_HOST_MEDIA_MEDIA_STREAM_DEVICE_SETTINGS_H_
