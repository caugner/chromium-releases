// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/app/app_api.h"

#include "base/json/json_writer.h"
#include "base/values.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/app_notification_manager.h"
#include "chrome/browser/extensions/extension_event_router.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "content/public/browser/notification_service.h"
#include "googleurl/src/gurl.h"

namespace {

const char kBodyTextKey[] = "bodyText";
const char kExtensionIdKey[] = "extensionId";
const char kLinkTextKey[] = "linkText";
const char kLinkUrlKey[] = "linkUrl";
const char kTitleKey[] = "title";

const char kInvalidExtensionIdError[] =
    "Invalid extension id";
const char kMissingLinkTextError[] =
    "You must specify linkText if you use linkUrl";
const char kOnLaunchedEvent[] = "experimental.app.onLaunched";

}  // anonymous namespace

namespace extensions {

bool AppNotifyFunction::RunImpl() {
  if (!include_incognito() && profile_->IsOffTheRecord()) {
    error_ = extension_misc::kAppNotificationsIncognitoError;
    return false;
  }

  DictionaryValue* details;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(0, &details));
  EXTENSION_FUNCTION_VALIDATE(details != NULL);

  // TODO(asargent) remove this before the API leaves experimental.
  std::string id = extension_id();
  if (details->HasKey(kExtensionIdKey)) {
    EXTENSION_FUNCTION_VALIDATE(details->GetString(kExtensionIdKey, &id));
    if (!profile()->GetExtensionService()->GetExtensionById(id, true)) {
      error_ = kInvalidExtensionIdError;
      return false;
    }
  }

  std::string title;
  if (details->HasKey(kTitleKey))
    EXTENSION_FUNCTION_VALIDATE(details->GetString(kTitleKey, &title));

  std::string body;
  if (details->HasKey(kBodyTextKey))
    EXTENSION_FUNCTION_VALIDATE(details->GetString(kBodyTextKey, &body));

  scoped_ptr<AppNotification> item(new AppNotification(
      true, base::Time::Now(), "", id, title, body));

  if (details->HasKey(kLinkUrlKey)) {
    std::string link_url;
    EXTENSION_FUNCTION_VALIDATE(details->GetString(kLinkUrlKey, &link_url));
    item->set_link_url(GURL(link_url));
    if (!item->link_url().is_valid()) {
      error_ = "Invalid url: " + link_url;
      return false;
    }
    if (!details->HasKey(kLinkTextKey)) {
      error_ = kMissingLinkTextError;
      return false;
    }
    std::string link_text;
    EXTENSION_FUNCTION_VALIDATE(details->GetString(kLinkTextKey,
                                                   &link_text));
    item->set_link_text(link_text);
  }

  AppNotificationManager* manager =
      profile()->GetExtensionService()->app_notification_manager();

  // TODO(beaudoin) We should probably report an error if Add returns false.
  manager->Add(item.release());

  return true;
}

bool AppClearAllNotificationsFunction::RunImpl() {
  if (!include_incognito() && profile_->IsOffTheRecord()) {
    error_ = extension_misc::kAppNotificationsIncognitoError;
    return false;
  }

  std::string id = extension_id();
  DictionaryValue* details = NULL;
  if (args_->GetDictionary(0, &details) && details->HasKey(kExtensionIdKey)) {
    EXTENSION_FUNCTION_VALIDATE(details->GetString(kExtensionIdKey, &id));
    if (!profile()->GetExtensionService()->GetExtensionById(id, true)) {
      error_ = kInvalidExtensionIdError;
      return false;
    }
  }

  AppNotificationManager* manager =
      profile()->GetExtensionService()->app_notification_manager();
  manager->ClearAll(id);
  return true;
}

// static.
void AppEventRouter::DispatchOnLaunchedEvent(
    Profile* profile, const Extension* extension) {
  profile->GetExtensionEventRouter()->DispatchEventToExtension(
      extension->id(), kOnLaunchedEvent, "[]", NULL, GURL());
}

// static.
void AppEventRouter::DispatchOnLaunchedEventWithFileEntry(
    Profile* profile, const Extension* extension, const string16& action,
    const std::string& file_system_id, const FilePath& base_name) {
  ListValue args;
  DictionaryValue* launch_data = new DictionaryValue();
  DictionaryValue* intent = new DictionaryValue();
  intent->SetString("action", UTF16ToUTF8(action));
  launch_data->Set("intent", intent);
  intent->SetString("type", "chrome-extension://fileentry");
  args.Append(launch_data);
  args.Append(Value::CreateStringValue(file_system_id));
  args.Append(Value::CreateStringValue(base_name.AsUTF8Unsafe()));
  std::string json_args;
  base::JSONWriter::Write(&args, &json_args);
  profile->GetExtensionEventRouter()->DispatchEventToExtension(
      extension->id(), kOnLaunchedEvent, json_args, NULL, GURL());
}

}  // namespace extensions
