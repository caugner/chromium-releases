// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/chromeos/network/tray_network.h"

#include "ash/shell.h"
#include "ash/system/chromeos/network/network_list_detailed_view_base.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_item_more.h"
#include "ash/system/tray/tray_item_view.h"
#include "ash/system/tray/tray_notification_view.h"
#include "grit/ash_resources.h"
#include "grit/ash_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/link_listener.h"
#include "ui/views/layout/box_layout.h"

namespace {

using ash::internal::TrayNetwork;

int GetMessageIcon(TrayNetwork::MessageType message_type) {
  switch(message_type) {
    case TrayNetwork::ERROR_CONNECT_FAILED:
      return IDR_AURA_UBER_TRAY_NETWORK_FAILED;
    case TrayNetwork::MESSAGE_DATA_LOW:
      return IDR_AURA_UBER_TRAY_NETWORK_DATA_LOW;
    case TrayNetwork::MESSAGE_DATA_NONE:
      return IDR_AURA_UBER_TRAY_NETWORK_DATA_NONE;
    case TrayNetwork::MESSAGE_DATA_PROMO:
      return IDR_AURA_UBER_TRAY_NOTIFICATION_3G;
  }
  NOTREACHED();
  return 0;
}

}  // namespace

namespace ash {
namespace internal {

namespace tray {

enum ColorTheme {
  LIGHT,
  DARK,
};

class NetworkMessages {
 public:
  struct Message {
    Message() : delegate(NULL) {}
    Message(NetworkTrayDelegate* in_delegate,
            const string16& in_title,
            const string16& in_message,
            const std::vector<string16>& in_links) :
        delegate(in_delegate),
        title(in_title),
        message(in_message),
        links(in_links) {}
    NetworkTrayDelegate* delegate;
    string16 title;
    string16 message;
    std::vector<string16> links;
  };
  typedef std::map<TrayNetwork::MessageType, Message> MessageMap;

  MessageMap& messages() { return messages_; }
  const MessageMap& messages() const { return messages_; }

 private:
  MessageMap messages_;
};

class NetworkTrayView : public TrayItemView {
 public:
  NetworkTrayView(ColorTheme size, bool tray_icon)
      : color_theme_(size), tray_icon_(tray_icon) {
    SetLayoutManager(
        new views::BoxLayout(views::BoxLayout::kHorizontal, 0, 0, 0));

    image_view_ = color_theme_ == DARK ?
        new FixedSizedImageView(0, kTrayPopupItemHeight) :
        new views::ImageView;
    AddChildView(image_view_);

    NetworkIconInfo info;
    Shell::GetInstance()->tray_delegate()->
        GetMostRelevantNetworkIcon(&info, false);
    Update(info);
  }

  virtual ~NetworkTrayView() {}

  void Update(const NetworkIconInfo& info) {
    image_view_->SetImage(info.image);
    if (tray_icon_)
      SetVisible(info.tray_icon_visible);
    SchedulePaint();
  }

 private:
  views::ImageView* image_view_;
  ColorTheme color_theme_;
  bool tray_icon_;

  DISALLOW_COPY_AND_ASSIGN(NetworkTrayView);
};

class NetworkDefaultView : public TrayItemMore {
 public:
  explicit NetworkDefaultView(SystemTrayItem* owner)
      : TrayItemMore(owner) {
    Update();
  }

  virtual ~NetworkDefaultView() {}

  void Update() {
    NetworkIconInfo info;
    Shell::GetInstance()->tray_delegate()->
        GetMostRelevantNetworkIcon(&info, true);
    SetImage(&info.image);
    SetLabel(info.description);
    SetAccessibleName(info.description);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkDefaultView);
};

class NetworkListDetailedView : public NetworkListDetailedViewBase {
 public:
  NetworkListDetailedView(user::LoginStatus login, int header_string_id)
      : NetworkListDetailedViewBase(login, header_string_id),
        airplane_(NULL),
        button_wifi_(NULL),
        button_mobile_(NULL),
        view_mobile_account_(NULL),
        setup_mobile_account_(NULL),
        other_wifi_(NULL),
        turn_on_wifi_(NULL),
        other_mobile_(NULL) {
  }

  virtual ~NetworkListDetailedView() {
  }

  // Overridden from NetworkListDetailedViewBase:

  virtual void AppendHeaderButtons() OVERRIDE {
    button_wifi_ = new TrayPopupHeaderButton(this,
        IDR_AURA_UBER_TRAY_WIFI_ENABLED,
        IDR_AURA_UBER_TRAY_WIFI_DISABLED,
        IDR_AURA_UBER_TRAY_WIFI_ENABLED_HOVER,
        IDR_AURA_UBER_TRAY_WIFI_DISABLED_HOVER,
        IDS_ASH_STATUS_TRAY_WIFI);
    button_wifi_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_DISABLE_WIFI));
    button_wifi_->SetToggledTooltipText(
        l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_ENABLE_WIFI));
    footer()->AddButton(button_wifi_);

    button_mobile_ = new TrayPopupHeaderButton(this,
        IDR_AURA_UBER_TRAY_CELLULAR_ENABLED,
        IDR_AURA_UBER_TRAY_CELLULAR_DISABLED,
        IDR_AURA_UBER_TRAY_CELLULAR_ENABLED_HOVER,
        IDR_AURA_UBER_TRAY_CELLULAR_DISABLED_HOVER,
        IDS_ASH_STATUS_TRAY_CELLULAR);
    button_mobile_->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_DISABLE_MOBILE));
    button_mobile_->SetToggledTooltipText(
        l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_ENABLE_MOBILE));
    footer()->AddButton(button_mobile_);

    AppendInfoButtonToHeader();
  }

  virtual void UpdateHeaderButtons() OVERRIDE {
    SystemTrayDelegate* delegate = Shell::GetInstance()->tray_delegate();
    button_wifi_->SetToggled(!delegate->GetWifiEnabled());
    button_mobile_->SetToggled(!delegate->GetMobileEnabled());
    button_mobile_->SetVisible(delegate->GetMobileAvailable());
    UpdateSettingButton();
  }

  virtual void AppendNetworkEntries() OVERRIDE {
    CreateScrollableList();

    HoverHighlightView* container = new HoverHighlightView(this);
    container->set_fixed_height(kTrayPopupItemHeight);
    container->AddLabel(ui::ResourceBundle::GetSharedInstance().
        GetLocalizedString(IDS_ASH_STATUS_TRAY_MOBILE_VIEW_ACCOUNT),
        gfx::Font::NORMAL);
    AddChildView(container);
    view_mobile_account_ = container;

    container = new HoverHighlightView(this);
    container->set_fixed_height(kTrayPopupItemHeight);
    container->AddLabel(ui::ResourceBundle::GetSharedInstance().
        GetLocalizedString(IDS_ASH_STATUS_TRAY_SETUP_MOBILE),
        gfx::Font::NORMAL);
    AddChildView(container);
    setup_mobile_account_ = container;
  }

  virtual void GetAvailableNetworkList(
      std::vector<NetworkIconInfo>* list) OVERRIDE {
    Shell::GetInstance()->tray_delegate()->GetAvailableNetworks(list);
  }

  virtual void RefreshNetworkScrollWithEmptyNetworkList() OVERRIDE {
    ClearNetworkScrollWithEmptyNetworkList();
    HoverHighlightView* container = new HoverHighlightView(this);
    container->set_fixed_height(kTrayPopupItemHeight);

    if (Shell::GetInstance()->tray_delegate()->GetWifiEnabled()) {
      NetworkIconInfo info;
      Shell::GetInstance()->tray_delegate()->
          GetMostRelevantNetworkIcon(&info, true);
      container->AddIconAndLabel(info.image,
          info.description,
          gfx::Font::NORMAL);
      container->set_border(views::Border::CreateEmptyBorder(0,
          kTrayPopupPaddingHorizontal, 0, 0));

    } else {
      container->AddLabel(ui::ResourceBundle::GetSharedInstance().
          GetLocalizedString(IDS_ASH_STATUS_TRAY_NETWORK_WIFI_DISABLED),
          gfx::Font::NORMAL);
    }

    scroll_content()->AddChildViewAt(container, 0);
    scroll_content()->SizeToPreferredSize();
    static_cast<views::View*>(scroller())->Layout();
  }

  virtual void UpdateNetworkEntries() OVERRIDE {
    RefreshNetworkScrollWithUpdatedNetworkData();

    view_mobile_account_->SetVisible(false);
    setup_mobile_account_->SetVisible(false);

    if (login() == user::LOGGED_IN_NONE)
      return;

    std::string carrier_id, topup_url, setup_url;
    if (Shell::GetInstance()->tray_delegate()->
            GetCellularCarrierInfo(&carrier_id,
                                   &topup_url,
                                   &setup_url)) {
      if (carrier_id != carrier_id_) {
        carrier_id_ = carrier_id;
        if (!topup_url.empty())
          topup_url_ = topup_url;
      }

      if (!setup_url.empty())
        setup_url_ = setup_url;

      if (!topup_url_.empty())
        view_mobile_account_->SetVisible(true);
      if (!setup_url_.empty())
        setup_mobile_account_->SetVisible(true);
    }
  }

  virtual void AppendCustomButtonsToBottomRow(
      TrayPopupTextButtonContainer* bottom_row) OVERRIDE {
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    other_wifi_ = new TrayPopupTextButton(this,
        rb.GetLocalizedString(IDS_ASH_STATUS_TRAY_OTHER_WIFI));
    bottom_row->AddTextButton(other_wifi_);

    turn_on_wifi_ = new TrayPopupTextButton(this,
        rb.GetLocalizedString(IDS_ASH_STATUS_TRAY_TURN_ON_WIFI));
    bottom_row->AddTextButton(turn_on_wifi_);

    other_mobile_ = new TrayPopupTextButton(this,
        rb.GetLocalizedString(IDS_ASH_STATUS_TRAY_OTHER_MOBILE));
    bottom_row->AddTextButton(other_mobile_);
  }

  virtual void UpdateNetworkExtra() OVERRIDE {
    if (login() == user::LOGGED_IN_LOCKED)
      return;

    SystemTrayDelegate* delegate = Shell::GetInstance()->tray_delegate();
    if (IsNetworkListEmpty() && !delegate->GetWifiEnabled()) {
      turn_on_wifi_->SetVisible(true);
      other_wifi_->SetVisible(false);
    } else {
      turn_on_wifi_->SetVisible(false);
      other_wifi_->SetVisible(true);
      other_wifi_->SetEnabled(delegate->GetWifiEnabled());
    }
    other_mobile_->SetVisible(delegate->GetMobileAvailable() &&
                              delegate->GetMobileScanSupported());
    if (other_mobile_->visible())
      other_mobile_->SetEnabled(delegate->GetMobileEnabled());

    turn_on_wifi_->parent()->Layout();
  }

  virtual void CustomButtonPressed(views::Button* sender,
      const ui::Event& event) OVERRIDE {
    ash::SystemTrayDelegate* delegate =
        ash::Shell::GetInstance()->tray_delegate();
    if (sender == button_wifi_)
      delegate->ToggleWifi();
    else if (sender == button_mobile_)
      delegate->ToggleMobile();
    else if (sender == other_mobile_)
      delegate->ShowOtherCellular();
    else if (sender == other_wifi_)
      delegate->ShowOtherWifi();
    else if (sender == turn_on_wifi_)
      delegate->ToggleWifi();
    else
      NOTREACHED();
  }

  virtual bool CustomLinkClickedOn(views::View* sender) OVERRIDE {
    ash::SystemTrayDelegate* delegate =
        ash::Shell::GetInstance()->tray_delegate();
    if (sender == view_mobile_account_) {
      delegate->ShowCellularURL(topup_url_);
      return true;
    } else if (sender == setup_mobile_account_) {
      delegate->ShowCellularURL(setup_url_);
      return true;
    } else if (sender == airplane_) {
      delegate->ToggleAirplaneMode();
      return true;
    } else {
      return false;
    }
  }

 private:
  std::string carrier_id_;
  std::string topup_url_;
  std::string setup_url_;

  views::View* airplane_;
  TrayPopupHeaderButton* button_wifi_;
  TrayPopupHeaderButton* button_mobile_;
  views::View* view_mobile_account_;
  views::View* setup_mobile_account_;
  TrayPopupTextButton* other_wifi_;
  TrayPopupTextButton* turn_on_wifi_;
  TrayPopupTextButton* other_mobile_;

  DISALLOW_COPY_AND_ASSIGN(NetworkListDetailedView);
};

class NetworkWifiDetailedView : public NetworkDetailedView {
 public:
  explicit NetworkWifiDetailedView(bool wifi_enabled) {
    SetLayoutManager(new views::BoxLayout(views::BoxLayout::kHorizontal,
                                          kTrayPopupPaddingHorizontal,
                                          10,
                                          kTrayPopupPaddingBetweenItems));
    ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
    views::ImageView* image = new views::ImageView;
    const int image_id = wifi_enabled ?
        IDR_AURA_UBER_TRAY_WIFI_ENABLED : IDR_AURA_UBER_TRAY_WIFI_DISABLED;
    image->SetImage(bundle.GetImageNamed(image_id).ToImageSkia());
    AddChildView(image);

    const int string_id = wifi_enabled ?
        IDS_ASH_STATUS_TRAY_NETWORK_WIFI_ENABLED:
        IDS_ASH_STATUS_TRAY_NETWORK_WIFI_DISABLED;
    views::Label* label =
        new views::Label(bundle.GetLocalizedString(string_id));
    label->SetMultiLine(true);
    label->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
    AddChildView(label);
  }

  virtual ~NetworkWifiDetailedView() {}

  // Overridden from NetworkDetailedView:

  virtual void Init() OVERRIDE {
  }

  virtual NetworkDetailedView::DetailedViewType GetViewType() const OVERRIDE {
    return NetworkDetailedView::WIFI_VIEW;
  }

  virtual void Update() OVERRIDE {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkWifiDetailedView);
};

class NetworkMessageView : public views::View,
                           public views::LinkListener {
 public:
  NetworkMessageView(TrayNetwork* tray,
                     TrayNetwork::MessageType message_type,
                     const NetworkMessages::Message& network_msg)
      : tray_(tray),
        message_type_(message_type) {
    SetLayoutManager(
        new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, 1));

    if (!network_msg.title.empty()) {
      views::Label* title = new views::Label(network_msg.title);
      title->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
      title->SetFont(title->font().DeriveFont(0, gfx::Font::BOLD));
      AddChildView(title);
    }

    if (!network_msg.message.empty()) {
      views::Label* message = new views::Label(network_msg.message);
      message->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
      message->SetMultiLine(true);
      message->SizeToFit(kTrayNotificationContentsWidth);
      AddChildView(message);
    }

    if (!network_msg.links.empty()) {
      for (size_t i = 0; i < network_msg.links.size(); ++i) {
        views::Link* link = new views::Link(network_msg.links[i]);
        link->set_id(i);
        link->set_listener(this);
        link->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
        link->SetMultiLine(true);
        link->SizeToFit(kTrayNotificationContentsWidth);
        AddChildView(link);
      }
    }
  }

  virtual ~NetworkMessageView() {
  }

  // Overridden from views::LinkListener.
  virtual void LinkClicked(views::Link* source, int event_flags) OVERRIDE {
    tray_->LinkClicked(message_type_, source->id());
  }

  TrayNetwork::MessageType message_type() const { return message_type_; }

 private:
  TrayNetwork* tray_;
  TrayNetwork::MessageType message_type_;

  DISALLOW_COPY_AND_ASSIGN(NetworkMessageView);
};

class NetworkNotificationView : public TrayNotificationView {
 public:
  explicit NetworkNotificationView(TrayNetwork* tray)
      : TrayNotificationView(tray, 0) {
    CreateMessageView();
    InitView(network_message_view_);
    SetIconImage(*ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
        GetMessageIcon(network_message_view_->message_type())));
  }

  // Overridden from TrayNotificationView.
  virtual void OnClose() OVERRIDE {
    tray_network()->ClearNetworkMessage(network_message_view_->message_type());
  }

  virtual void OnClickAction() OVERRIDE {
    if (network_message_view_->message_type() !=
        TrayNetwork::MESSAGE_DATA_PROMO)
      tray()->PopupDetailedView(0, true);
  }

  void Update() {
    CreateMessageView();
    UpdateViewAndImage(network_message_view_,
        *ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
            GetMessageIcon(network_message_view_->message_type())));
  }

 private:
  TrayNetwork* tray_network() {
    return static_cast<TrayNetwork*>(tray());
  }

  void CreateMessageView() {
    // Display the first (highest priority) message.
    CHECK(!tray_network()->messages()->messages().empty());
    NetworkMessages::MessageMap::const_iterator iter =
        tray_network()->messages()->messages().begin();
    network_message_view_ =
        new NetworkMessageView(tray_network(), iter->first, iter->second);
  }

  tray::NetworkMessageView* network_message_view_;

  DISALLOW_COPY_AND_ASSIGN(NetworkNotificationView);
};

}  // namespace tray

TrayNetwork::TrayNetwork()
    : tray_(NULL),
      default_(NULL),
      detailed_(NULL),
      notification_(NULL),
      messages_(new tray::NetworkMessages()),
      request_wifi_view_(false) {
}

TrayNetwork::~TrayNetwork() {
}

views::View* TrayNetwork::CreateTrayView(user::LoginStatus status) {
  CHECK(tray_ == NULL);
  tray_ = new tray::NetworkTrayView(tray::LIGHT, true /*tray_icon*/);
  return tray_;
}

views::View* TrayNetwork::CreateDefaultView(user::LoginStatus status) {
  CHECK(default_ == NULL);
  default_ = new tray::NetworkDefaultView(this);
  return default_;
}

views::View* TrayNetwork::CreateDetailedView(user::LoginStatus status) {
  CHECK(detailed_ == NULL);
  // Clear any notifications when showing the detailed view.
  messages_->messages().clear();
  HideNotificationView();
  if (request_wifi_view_) {
    SystemTrayDelegate* delegate = Shell::GetInstance()->tray_delegate();
    // The Wi-Fi state is not toggled yet at this point.
    detailed_ = new tray::NetworkWifiDetailedView(!delegate->GetWifiEnabled());
    request_wifi_view_ = false;
  } else {
    detailed_ = new tray::NetworkListDetailedView(
        status, IDS_ASH_STATUS_TRAY_NETWORK);
    detailed_->Init();
  }
  return detailed_;
}

views::View* TrayNetwork::CreateNotificationView(user::LoginStatus status) {
  CHECK(notification_ == NULL);
  if (messages_->messages().empty())
    return NULL;  // Message has already been cleared.
  notification_ = new tray::NetworkNotificationView(this);
  return notification_;
}

void TrayNetwork::DestroyTrayView() {
  tray_ = NULL;
}

void TrayNetwork::DestroyDefaultView() {
  default_ = NULL;
}

void TrayNetwork::DestroyDetailedView() {
  detailed_ = NULL;
}

void TrayNetwork::DestroyNotificationView() {
  notification_ = NULL;
}

void TrayNetwork::UpdateAfterLoginStatusChange(user::LoginStatus status) {
}

void TrayNetwork::UpdateAfterShelfAlignmentChange(ShelfAlignment alignment) {
  SetTrayImageItemBorder(tray_, alignment);
}

void TrayNetwork::OnNetworkRefresh(const NetworkIconInfo& info) {
  if (tray_)
    tray_->Update(info);
  if (default_)
    default_->Update();
  if (detailed_)
    detailed_->Update();
}

void TrayNetwork::SetNetworkMessage(NetworkTrayDelegate* delegate,
                                   MessageType message_type,
                                   const string16& title,
                                   const string16& message,
                                   const std::vector<string16>& links) {
  messages_->messages()[message_type] =
      tray::NetworkMessages::Message(delegate, title, message, links);
  if (notification_)
    notification_->Update();
  else
    ShowNotificationView();
}

void TrayNetwork::ClearNetworkMessage(MessageType message_type) {
  messages_->messages().erase(message_type);
  if (messages_->messages().empty()) {
    HideNotificationView();
    return;
  }
  if (notification_)
    notification_->Update();
  else
    ShowNotificationView();
}

void TrayNetwork::OnWillToggleWifi() {
  if (!detailed_ ||
      detailed_->GetViewType() == tray::NetworkDetailedView::WIFI_VIEW) {
    request_wifi_view_ = true;
    PopupDetailedView(kTrayPopupAutoCloseDelayForTextInSeconds, false);
  }
}

void TrayNetwork::LinkClicked(MessageType message_type, int link_id) {
  tray::NetworkMessages::MessageMap::const_iterator iter =
      messages()->messages().find(message_type);
  if (iter != messages()->messages().end() && iter->second.delegate)
    iter->second.delegate->NotificationLinkClicked(link_id);
}

}  // namespace internal
}  // namespace ash
