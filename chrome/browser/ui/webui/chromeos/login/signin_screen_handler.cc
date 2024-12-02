// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/chromeos/login/signin_screen_handler.h"

#include "base/command_line.h"
#include "base/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/power_library.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/webui_login_display.h"
#include "chrome/browser/chromeos/user_cros_settings_provider.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

const char kDefaultDomain[] = "@gmail.com";

// Account picker screen id.
const char kAccountPickerScreen[] = "account-picker";
// Sign in screen id.
const char kSigninScreen[] = "signin";
// Sign in screen id for GAIA extension hosted content.
const char kGaiaSigninScreen[] = "gaia-signin";
// Start page of GAIA authentication extension.
const char kGaiaExtStartPage[] =
    "chrome-extension://mfffpogegjflfpflabcdkioaeobkgjik/main.html";

// User dictionary keys.
const char kKeyName[] = "name";
const char kKeyEmailAddress[] = "emailAddress";
const char kKeyCanRemove[] = "canRemove";
const char kKeyImageUrl[] = "imageUrl";
const char kKeyOauthTokenStatus[] = "oauthTokenStatus";

// Max number of users to show.
const int kMaxUsers = 5;

// Sanitize emails. Currently, it only ensures all emails have a domain.
std::string SanitizeEmail(const std::string& email) {
  std::string sanitized(email);

  // Apply a default domain if necessary.
  if (sanitized.find('@') == std::string::npos)
    sanitized += kDefaultDomain;

  return sanitized;
}

}  // namespace

namespace chromeos {

SigninScreenHandler::SigninScreenHandler()
    : delegate_(WebUILoginDisplay::GetInstance()),
      show_on_init_(false),
      oobe_ui_(false),
      extension_driven_(
          CommandLine::ForCurrentProcess()->HasSwitch(
              switches::kWebUILogin)) {
  delegate_->SetWebUIHandler(this);
}

SigninScreenHandler::~SigninScreenHandler() {
}

void SigninScreenHandler::GetLocalizedStrings(
    DictionaryValue* localized_strings) {
  localized_strings->SetString("signinScreenTitle",
      l10n_util::GetStringUTF16(IDS_OOBE_SIGNIN));
  localized_strings->SetString("emailHint",
      l10n_util::GetStringUTF16(IDS_LOGIN_USERNAME));
  localized_strings->SetString("passwordHint",
      l10n_util::GetStringUTF16(IDS_LOGIN_PASSWORD));
  localized_strings->SetString("signinButton",
      l10n_util::GetStringUTF16(IDS_LOGIN_BUTTON));
  localized_strings->SetString("enterGuestButton",
      l10n_util::GetStringUTF16(IDS_ENTER_GUEST_SESSION_BUTTON));
  localized_strings->SetString("shutDown",
      l10n_util::GetStringUTF16(IDS_SHUTDOWN_BUTTON));
  localized_strings->SetString("addUser",
      l10n_util::GetStringUTF16(IDS_ADD_USER_BUTTON));
  localized_strings->SetString("cancel",
      l10n_util::GetStringUTF16(IDS_CANCEL));
  localized_strings->SetString("addUserOfflineMessage",
      l10n_util::GetStringUTF16(IDS_LOGIN_ERROR_ADD_USER_OFFLINE));
  localized_strings->SetString("offlineMessageTitle",
      l10n_util::GetStringUTF16(IDS_LOGIN_OFFLINE_TITLE));
  localized_strings->SetString("offlineMessageBody",
      l10n_util::GetStringUTF16(IDS_LOGIN_OFFLINE_MESSAGE));
  localized_strings->SetString("createAccount",
      l10n_util::GetStringUTF16(IDS_CREATE_ACCOUNT_BUTTON));
  localized_strings->SetString("guestSignin",
      l10n_util::GetStringUTF16(IDS_BROWSE_WITHOUT_SIGNING_IN_BUTTON));
  localized_strings->SetString("removeUser",
      l10n_util::GetStringUTF16(IDS_LOGIN_REMOVE));

  if (extension_driven_)
    localized_strings->SetString("authType", "ext");
  else
    localized_strings->SetString("authType", "webui");
}

void SigninScreenHandler::Show(bool oobe_ui) {
  oobe_ui_ = oobe_ui;
  if (!page_is_ready()) {
    show_on_init_ = true;
    return;
  }

  if (oobe_ui) {
    // Shows new user sign-in for OOBE.
    HandleShowAddUser(NULL);
  } else {
    // Populates account picker. Animation is turned off for now until we
    // figure out how to make it fast enough.
    SendUserList(false);

    // Show sign-in UI if there is no visible users.
    if (WebUILoginDisplay::GetInstance()->users().empty())
      HandleShowAddUser(NULL);
    else
      ShowScreen(kAccountPickerScreen, NULL);
  }
}

// SigninScreenHandler, private: -----------------------------------------------

void SigninScreenHandler::Initialize() {
  if (show_on_init_) {
    show_on_init_ = false;
    Show(oobe_ui_);
  }
}

void SigninScreenHandler::RegisterMessages() {
  web_ui_->RegisterMessageCallback("authenticateUser",
      NewCallback(this, &SigninScreenHandler::HandleAuthenticateUser));
  web_ui_->RegisterMessageCallback("completeLogin",
      NewCallback(this, &SigninScreenHandler::HandleCompleteLogin));
  web_ui_->RegisterMessageCallback("getUsers",
      NewCallback(this, &SigninScreenHandler::HandleGetUsers));
  web_ui_->RegisterMessageCallback("launchIncognito",
      NewCallback(this, &SigninScreenHandler::HandleLaunchIncognito));
  web_ui_->RegisterMessageCallback("showAddUser",
      NewCallback(this, &SigninScreenHandler::HandleShowAddUser));
  web_ui_->RegisterMessageCallback("shutdownSystem",
      NewCallback(this, &SigninScreenHandler::HandleShutdownSystem));
  web_ui_->RegisterMessageCallback("removeUser",
      NewCallback(this, &SigninScreenHandler::HandleRemoveUser));
  web_ui_->RegisterMessageCallback("toggleEnrollmentScreen",
      NewCallback(this, &SigninScreenHandler::HandleToggleEnrollmentScreen));
  web_ui_->RegisterMessageCallback("launchHelpApp",
      NewCallback(this, &SigninScreenHandler::HandleLaunchHelpApp));
  web_ui_->RegisterMessageCallback("createAccount",
      NewCallback(this, &SigninScreenHandler::HandleCreateAccount));
}

void SigninScreenHandler::HandleGetUsers(const base::ListValue* args) {
  SendUserList(false);
}

void SigninScreenHandler::ClearAndEnablePassword() {
  web_ui_->CallJavascriptFunction("cr.ui.Oobe.resetSigninUI");
}

void SigninScreenHandler::OnLoginSuccess(const std::string& username) {
  base::StringValue username_value(username);
  web_ui_->CallJavascriptFunction("cr.ui.Oobe.onLoginSuccess", username_value);
}

void SigninScreenHandler::OnUserRemoved(const std::string& username) {
  SendUserList(false);
}

void SigninScreenHandler::ShowError(int login_attempts,
                                    const std::string& error_text,
                                    const std::string& help_link_text,
                                    HelpAppLauncher::HelpTopic help_topic_id) {
  base::FundamentalValue login_attempts_value(login_attempts);
  base::StringValue error_message(error_text);
  base::StringValue help_link(help_link_text);
  base::FundamentalValue help_id(static_cast<int>(help_topic_id));
  web_ui_->CallJavascriptFunction("cr.ui.Oobe.showSignInError",
                                  login_attempts_value,
                                  error_message,
                                  help_link,
                                  help_id);
}

void SigninScreenHandler::HandleCompleteLogin(const base::ListValue* args) {
  std::string username;
  std::string password;
  if (!args->GetString(0, &username) ||
      !args->GetString(1, &password)) {
    NOTREACHED();
    return;
  }

  username = SanitizeEmail(username);
  delegate_->CompleteLogin(username, password);
}

void SigninScreenHandler::HandleAuthenticateUser(const base::ListValue* args) {
  std::string username;
  std::string password;
  if (!args->GetString(0, &username) ||
      !args->GetString(1, &password)) {
    NOTREACHED();
    return;
  }

  username = SanitizeEmail(username);
  delegate_->Login(username, password);
}

void SigninScreenHandler::HandleLaunchIncognito(const base::ListValue* args) {
  delegate_->LoginAsGuest();
}

void SigninScreenHandler::HandleShutdownSystem(const base::ListValue* args) {
  DCHECK(CrosLibrary::Get()->EnsureLoaded());
  CrosLibrary::Get()->GetPowerLibrary()->RequestShutdown();
}

void SigninScreenHandler::HandleRemoveUser(const base::ListValue* args) {
  std::string email;
  if (!args->GetString(0, &email)) {
    NOTREACHED();
    return;
  }

  delegate_->RemoveUser(email);
}

void SigninScreenHandler::HandleShowAddUser(const base::ListValue* args) {
  if (extension_driven_) {
    DictionaryValue params;
    params.SetString("startUrl", kGaiaExtStartPage);

    std::string email;
    // |args| can be null if it's OOBE.
    if (args && args->GetString(0, &email))
      params.SetString("email", email);

    params.SetBoolean("createAccount",
        UserCrosSettingsProvider::cached_allow_new_user());
    params.SetBoolean("guestSignin",
        UserCrosSettingsProvider::cached_allow_guest());

    ShowScreen(kGaiaSigninScreen, &params);
  } else {
    ShowScreen(kSigninScreen, NULL);
  }
}

void SigninScreenHandler::HandleToggleEnrollmentScreen(
    const base::ListValue* args) {
  delegate_->ShowEnterpriseEnrollmentScreen();
}

void SigninScreenHandler::HandleLaunchHelpApp(const base::ListValue* args) {
  double help_topic_id;  // Javascript number is passed back as double.
  if (!args->GetDouble(0, &help_topic_id)) {
    NOTREACHED();
    return;
  }

  if (!help_app_.get())
    help_app_ = new HelpAppLauncher(
        WebUILoginDisplay::GetLoginWindow()->GetNativeWindow());
  help_app_->ShowHelpTopic(
      static_cast<HelpAppLauncher::HelpTopic>(help_topic_id));
}

void SigninScreenHandler::SendUserList(bool animated) {
  bool show_guest = WebUILoginDisplay::GetInstance()->show_guest();

  size_t max_non_owner_users = show_guest ? kMaxUsers - 2 : kMaxUsers - 1;
  size_t non_owner_count = 0;

  ListValue users_list;
  UserVector users = WebUILoginDisplay::GetInstance()->users();

  bool single_user = users.size() == 1;
  for (UserVector::const_iterator it = users.begin();
       it != users.end(); ++it) {
    const std::string& email = it->email();
    bool is_owner = email == UserCrosSettingsProvider::cached_owner();

    if (non_owner_count < max_non_owner_users || is_owner) {
      DictionaryValue* user_dict = new DictionaryValue();
      user_dict->SetString(kKeyName, it->GetDisplayName());
      user_dict->SetString(kKeyEmailAddress, email);
      user_dict->SetInteger(kKeyOauthTokenStatus, it->oauth_token_status());

      // Single user check here is necessary because owner info might not be
      // available when running into login screen on first boot.
      // See http://crosbug.com/12723
      user_dict->SetBoolean(kKeyCanRemove,
                            !single_user &&
                            !email.empty() &&
                            !is_owner);

      if (!email.empty()) {
        long long timestamp = base::TimeTicks::Now().ToInternalValue();
        std::string image_url(
            StringPrintf("%s%s?id=%lld",
                         chrome::kChromeUIUserImageURL,
                         email.c_str(),
                         timestamp));
        user_dict->SetString(kKeyImageUrl, image_url);
      } else {
        std::string image_url(std::string(chrome::kChromeUIScheme) + "://" +
            std::string(chrome::kChromeUIThemePath) +
            "/IDR_LOGIN_DEFAULT_USER");
        user_dict->SetString(kKeyImageUrl, image_url);
      }

      users_list.Append(user_dict);
      if (!is_owner)
        ++non_owner_count;
    }
  }

  if (show_guest) {
    // Add the Guest to the user list.
    DictionaryValue* guest_dict = new DictionaryValue();
    guest_dict->SetString(kKeyName, l10n_util::GetStringUTF16(IDS_GUEST));
    guest_dict->SetString(kKeyEmailAddress, "");
    guest_dict->SetBoolean(kKeyCanRemove, false);
    guest_dict->SetInteger(kKeyOauthTokenStatus,
                           UserManager::OAUTH_TOKEN_STATUS_UNKNOWN);
    std::string image_url(std::string(chrome::kChromeUIScheme) + "://" +
        std::string(chrome::kChromeUIThemePath) + "/IDR_LOGIN_GUEST");
    guest_dict->SetString(kKeyImageUrl, image_url);
    users_list.Append(guest_dict);
  }

  // Call the Javascript callback
  base::FundamentalValue animated_value(animated);
  web_ui_->CallJavascriptFunction("login.AccountPickerScreen.loadUsers",
                                  users_list, animated_value);
}

void SigninScreenHandler::HandleCreateAccount(const base::ListValue* args) {
  delegate_->CreateAccount();
}

}  // namespace chromeos
