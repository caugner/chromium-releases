// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/display/display_preferences.h"

#include "ash/display/display_controller.h"
#include "ash/display/multi_display_manager.h"
#include "ash/shell.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/common/pref_names.h"
#include "googleurl/src/url_canon.h"
#include "googleurl/src/url_util.h"
#include "ui/aura/display_manager.h"
#include "ui/aura/env.h"
#include "ui/gfx/display.h"
#include "ui/gfx/insets.h"

namespace chromeos {
namespace {

// Replaces dot "." by "%2E" since it's the path separater of base::Value.  Also
// replaces "%" by "%25" for unescaping.
void EscapeDisplayName(const std::string& name, std::string* escaped) {
  DCHECK(escaped);
  std::string middle;
  ReplaceChars(name, "%", "%25", &middle);
  ReplaceChars(middle, ".", "%2E", escaped);
}

// Unescape %-encoded characters.
std::string UnescapeDisplayName(const std::string& name) {
  url_canon::RawCanonOutputT<char16> decoded;
  url_util::DecodeURLEscapeSequences(name.data(), name.size(), &decoded);
  // Display names are ASCII-only.
  return UTF16ToASCII(string16(decoded.data(), decoded.length()));
}

// This kind of boilerplates should be done by base::JSONValueConverter but it
// doesn't support classes like gfx::Insets for now.
// TODO(mukai): fix base::JSONValueConverter and use it here.
bool ValueToInsets(const base::DictionaryValue& value, gfx::Insets* insets) {
  DCHECK(insets);
  int top = 0;
  int left = 0;
  int bottom = 0;
  int right = 0;
  if (value.GetInteger("top", &top) &&
      value.GetInteger("left", &left) &&
      value.GetInteger("bottom", &bottom) &&
      value.GetInteger("right", &right)) {
    insets->Set(top, left, bottom, right);
    return true;
  }
  return false;
}

void InsetsToValue(const gfx::Insets& insets, base::DictionaryValue* value) {
  DCHECK(value);
  value->SetInteger("top", insets.top());
  value->SetInteger("left", insets.left());
  value->SetInteger("bottom", insets.bottom());
  value->SetInteger("right", insets.right());
}

ash::internal::MultiDisplayManager* GetDisplayManager() {
  return static_cast<ash::internal::MultiDisplayManager*>(
      aura::Env::GetInstance()->display_manager());
}

// Returns true id the current user can write display preferences to
// Local State.
bool IsValidUser() {
  UserManager* user_manager = UserManager::Get();
  return (user_manager->IsUserLoggedIn() &&
          !user_manager->IsLoggedInAsDemoUser() &&
          !user_manager->IsLoggedInAsGuest() &&
          !user_manager->IsLoggedInAsStub());
}

void NotifyDisplayLayoutChanged(PrefService* pref_service) {
  ash::DisplayController* display_controller =
      ash::Shell::GetInstance()->display_controller();

  ash::DisplayLayout default_layout(
      static_cast<ash::DisplayLayout::Position>(pref_service->GetInteger(
          prefs::kSecondaryDisplayLayout)),
      pref_service->GetInteger(prefs::kSecondaryDisplayOffset));
  display_controller->SetDefaultDisplayLayout(default_layout);

  const base::DictionaryValue* layouts = pref_service->GetDictionary(
      prefs::kSecondaryDisplays);
  for (base::DictionaryValue::key_iterator it = layouts->begin_keys();
       it != layouts->end_keys(); ++it) {
    const base::Value* value = NULL;
    if (!layouts->Get(*it, &value) || value == NULL) {
      LOG(WARNING) << "Can't find dictionary value for " << *it;
      continue;
    }

    ash::DisplayLayout layout;
    if (!ash::DisplayLayout::ConvertFromValue(*value, &layout)) {
      LOG(WARNING) << "Invalid preference value for " << *it;
      continue;
    }

    display_controller->SetLayoutForDisplayName(
        UnescapeDisplayName(*it), layout);
  }
}

void NotifyDisplayOverscans() {
  PrefService* local_state = g_browser_process->local_state();
  ash::internal::MultiDisplayManager* display_manager = GetDisplayManager();

  const base::DictionaryValue* overscans = local_state->GetDictionary(
      prefs::kDisplayOverscans);
  for (base::DictionaryValue::key_iterator it = overscans->begin_keys();
       it != overscans->end_keys(); ++it) {
    int64 display_id = gfx::Display::kInvalidDisplayID;
    if (!base::StringToInt64(*it, &display_id)) {
      LOG(WARNING) << "Invalid key, cannot convert to display ID: " << *it;
      continue;
    }

    const base::DictionaryValue* value = NULL;
    if (!overscans->GetDictionary(*it, &value) || value == NULL) {
      LOG(WARNING) << "Can't find dictionary value for " << *it;
      continue;
    }

    gfx::Insets insets;
    if (!ValueToInsets(*value, &insets)) {
      LOG(WARNING) << "Can't convert the data into insets for " << *it;
      continue;
    }

    display_manager->SetOverscanInsets(display_id, insets);
  }
}

}  // namespace

void RegisterDisplayPrefs(PrefService* pref_service) {
  // The default secondary display layout.
  pref_service->RegisterIntegerPref(prefs::kSecondaryDisplayLayout,
                                    static_cast<int>(ash::DisplayLayout::RIGHT),
                                    PrefService::UNSYNCABLE_PREF);
  // The default offset of the secondary display position from the primary
  // display.
  pref_service->RegisterIntegerPref(prefs::kSecondaryDisplayOffset,
                                    0,
                                    PrefService::UNSYNCABLE_PREF);
  // Per-display preference.
  pref_service->RegisterDictionaryPref(prefs::kSecondaryDisplays,
                                       PrefService::UNSYNCABLE_PREF);
}

void RegisterDisplayLocalStatePrefs(PrefService* local_state) {
  // Primary output name.
  local_state->RegisterInt64Pref(prefs::kPrimaryDisplayID,
                                 gfx::Display::kInvalidDisplayID,
                                 PrefService::UNSYNCABLE_PREF);

  // Display overscan preference.
  local_state->RegisterDictionaryPref(prefs::kDisplayOverscans,
                                      PrefService::UNSYNCABLE_PREF);
}

void SetDisplayLayoutPref(PrefService* pref_service,
                          const gfx::Display& display,
                          int layout,
                          int offset) {
  {
    DictionaryPrefUpdate update(pref_service, prefs::kSecondaryDisplays);
    ash::DisplayLayout display_layout(
        static_cast<ash::DisplayLayout::Position>(layout), offset);

    aura::DisplayManager* display_manager =
        aura::Env::GetInstance()->display_manager();
    std::string name;
    EscapeDisplayName(display_manager->GetDisplayNameFor(display),
                      &name);
    DCHECK(!name.empty());

    base::DictionaryValue* pref_data = update.Get();
    scoped_ptr<base::Value>layout_value(new base::DictionaryValue());
    if (pref_data->HasKey(name)) {
      base::Value* value = NULL;
      if (pref_data->Get(name, &value) && value != NULL)
        layout_value.reset(value->DeepCopy());
    }
    if (ash::DisplayLayout::ConvertToValue(display_layout, layout_value.get()))
      pref_data->Set(name, layout_value.release());
  }

  pref_service->SetInteger(prefs::kSecondaryDisplayLayout, layout);
  pref_service->SetInteger(prefs::kSecondaryDisplayOffset, offset);

  NotifyDisplayLayoutChanged(pref_service);
}

void StorePrimaryDisplayIDPref(int64 display_id) {
  if (!IsValidUser())
    return;

  PrefService* local_state = g_browser_process->local_state();
  if (GetDisplayManager()->IsInternalDisplayId(display_id))
    local_state->ClearPref(prefs::kPrimaryDisplayID);
  else
    local_state->SetInt64(prefs::kPrimaryDisplayID, display_id);
}

void SetDisplayOverscan(const gfx::Display& display,
                        const gfx::Insets& insets) {
  if (!IsValidUser())
    return;

  {
    DictionaryPrefUpdate update(
        g_browser_process->local_state(), prefs::kDisplayOverscans);
    const std::string id = base::Int64ToString(display.id());

    base::DictionaryValue* pref_data = update.Get();
    base::DictionaryValue* insets_value = new base::DictionaryValue();
    InsetsToValue(insets, insets_value);
    pref_data->Set(id, insets_value);
  }

  NotifyDisplayOverscans();
}

void SetPrimaryDisplayIDPref(int64 display_id) {
  StorePrimaryDisplayIDPref(display_id);
  ash::Shell::GetInstance()->display_controller()->SetPrimaryDisplayId(
      display_id);
}

void NotifyDisplayPrefChanged(PrefService* pref_service) {
  NotifyDisplayLayoutChanged(pref_service);
}

void NotifyDisplayLocalStatePrefChanged() {
  PrefService* local_state = g_browser_process->local_state();
  ash::Shell::GetInstance()->display_controller()->SetPrimaryDisplayId(
      local_state->GetInt64(prefs::kPrimaryDisplayID));
  NotifyDisplayOverscans();
}

}  // namespace chromeos
