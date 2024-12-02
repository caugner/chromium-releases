// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/network_profile_bubble.h"

#include <wtsapi32.h>
// Make sure we link the wtsapi lib file in.
#pragma comment(lib, "wtsapi32.lib")

#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/metrics/histogram.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/network_profile_bubble_prefs.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"

namespace {

// The duration of the silent period before we start nagging the user again.
const int kSilenceDurationDays = 100;

// Bubble layout constants.
const int kAnchorVerticalInset = 5;
const int kInset = 2;
const int kNotificationBubbleWidth = 250;

// The name of the UMA histogram collecting our stats.
const char kMetricNetworkedProfileCheck[] = "NetworkedProfile.Check";

enum MetricNetworkedProfileCheck {
  // Check was suppressed by command line flag.
  METRIC_CHECK_SUPPRESSED,
  // WTSQuerySessionInformation call failed.
  METRIC_CHECK_FAILED,
  // File access in profile dir failed.
  METRIC_CHECK_IO_FAILED,

  // Profile on a network share detected.
  METRIC_PROFILE_ON_NETWORK,
  // Profile not on a network share detected.
  METRIC_PROFILE_NOT_ON_NETWORK,

  // Check was suppressed because of remote session.
  METRIC_REMOTE_SESSION,

  // User has clicked learn more on the notification bubble.
  METRIC_LEARN_MORE_CLICKED,
  // User has clicked OK on the notification bubble.
  METRIC_ACKNOWLEDGED,

  METRIC_NETWORKED_PROFILE_CHECK_SIZE  // Must be the last.
};

// Helper function wrapping the UMA_HISTOGRAM_ENUMERATION macro.
void RecordUmaEvent(MetricNetworkedProfileCheck event) {
  UMA_HISTOGRAM_ENUMERATION(kMetricNetworkedProfileCheck,
                            event,
                            METRIC_NETWORKED_PROFILE_CHECK_SIZE);
}

// Implementation of BrowserList::Observer used to wait for a browser window.
class BrowserListObserver : public BrowserList::Observer {
 private:
  virtual ~BrowserListObserver();

  // Overridden from BrowserList::Observer:
  virtual void OnBrowserAdded(Browser* browser) OVERRIDE;
  virtual void OnBrowserRemoved(Browser* browser) OVERRIDE;
  virtual void OnBrowserSetLastActive(Browser* browser) OVERRIDE;
};

BrowserListObserver::~BrowserListObserver() {
}

void BrowserListObserver::OnBrowserAdded(Browser* browser) {
}

void BrowserListObserver::OnBrowserRemoved(Browser* browser) {
}

void BrowserListObserver::OnBrowserSetLastActive(Browser* browser) {
  NetworkProfileBubble::ShowNotification(browser);
  // No need to observe anymore.
  BrowserList::RemoveObserver(this);
  delete this;
}

}  // namespace

// static
bool NetworkProfileBubble::notification_shown_ = false;

////////////////////////////////////////////////////////////////////////////////
// NetworkProfileBubble, public:

// static
void NetworkProfileBubble::CheckNetworkProfile(const FilePath& profile_path) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  // On Windows notify the users if their profiles are located on a network
  // share as we don't officially support this setup yet.
  // However we don't want to bother users on Cytrix setups as those have no
  // real choice and their admins must be well aware of the risks associated.
  // Also the command line flag --no-network-profile-warning can stop this
  // warning from popping up. In this case we can skip the check to make the
  // start faster.
  // Collect a lot of stats along the way to see which cases do occur in the
  // wild often enough.
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kNoNetworkProfileWarning)) {
    RecordUmaEvent(METRIC_CHECK_SUPPRESSED);
    return;
  }

  LPWSTR buffer = NULL;
  DWORD buffer_length = 0;
  // Checking for RDP is cheaper than checking for a network drive so do this
  // one first.
  if (!WTSQuerySessionInformation(WTS_CURRENT_SERVER, WTS_CURRENT_SESSION,
                                  WTSClientProtocolType,
                                  &buffer, &buffer_length)) {
    RecordUmaEvent(METRIC_CHECK_FAILED);
    return;
  }

  unsigned short* type = reinterpret_cast<unsigned short*>(buffer);
  // Zero means local session and we should warn the users if they have
  // their profile on a network share.
  if (*type == 0) {
    bool profile_on_network = false;
    if (!profile_path.empty()) {
      FilePath temp_file;
      // Try to create some non-empty temp file in the profile dir and use
      // it to check if there is a reparse-point free path to it.
      if (file_util::CreateTemporaryFileInDir(profile_path, &temp_file) &&
          file_util::WriteFile(temp_file, ".", 1)) {
        FilePath normalized_temp_file;
        if (!file_util::NormalizeFilePath(temp_file, &normalized_temp_file))
          profile_on_network = true;
      } else {
        RecordUmaEvent(METRIC_CHECK_IO_FAILED);
      }
      file_util::Delete(temp_file, false);
    }
    if (profile_on_network) {
      RecordUmaEvent(METRIC_PROFILE_ON_NETWORK);
      content::BrowserThread::PostTask(
          content::BrowserThread::UI, FROM_HERE,
          base::Bind(&NetworkProfileBubble::NotifyNetworkProfileDetected));
    } else {
      RecordUmaEvent(METRIC_PROFILE_NOT_ON_NETWORK);
    }
  } else {
    RecordUmaEvent(METRIC_REMOTE_SESSION);
  }

  WTSFreeMemory(buffer);
}

// static
bool NetworkProfileBubble::ShouldCheckNetworkProfile(PrefService* prefs) {
  if (prefs->GetInteger(prefs::kNetworkProfileWarningsLeft))
    return !notification_shown_;
  int64 last_check = prefs->GetInt64(prefs::kNetworkProfileLastWarningTime);
  base::TimeDelta time_since_last_check =
      base::Time::Now() - base::Time::FromTimeT(last_check);
  if (time_since_last_check.InDays() > kSilenceDurationDays) {
    prefs->SetInteger(prefs::kNetworkProfileWarningsLeft,
                      browser::kMaxWarnings);
    return !notification_shown_;
  }
  return false;
}

// static
void NetworkProfileBubble::ShowNotification(Browser* browser) {
  views::View* anchor = NULL;
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (browser_view && browser_view->GetToolbarView())
    anchor = browser_view->GetToolbarView()->app_menu();
  NetworkProfileBubble* bubble =
      new NetworkProfileBubble(anchor, browser, browser->profile());
  views::BubbleDelegateView::CreateBubble(bubble);
  bubble->Show();
  notification_shown_ = true;

  // Mark the time of the last bubble and reduce the number of warnings left
  // before the next silence period starts.
  PrefService* prefs = browser->profile()->GetPrefs();
  prefs->SetInt64(prefs::kNetworkProfileLastWarningTime,
                  base::Time::Now().ToTimeT());
  int left_warnings = prefs->GetInteger(prefs::kNetworkProfileWarningsLeft);
  if (left_warnings > 0)
    prefs->SetInteger(prefs::kNetworkProfileWarningsLeft, --left_warnings);
}

////////////////////////////////////////////////////////////////////////////////
// NetworkProfileBubble, private:

NetworkProfileBubble::NetworkProfileBubble(views::View* anchor,
                                           content::PageNavigator* navigator,
                                           Profile* profile)
    : BubbleDelegateView(anchor, views::BubbleBorder::TOP_RIGHT),
      navigator_(navigator),
      profile_(profile) {
}

NetworkProfileBubble::~NetworkProfileBubble() {
}

void NetworkProfileBubble::Init() {
  views::GridLayout* layout = views::GridLayout::CreatePanel(this);
  layout->SetInsets(0, kInset, kInset, kInset);
  SetLayoutManager(layout);

  views::ColumnSet* columns = layout->AddColumnSet(0);
  columns->AddColumn(views::GridLayout::LEADING, views::GridLayout::LEADING, 0,
                     views::GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0, 0);

  views::Label* title = new views::Label(
      l10n_util::GetStringFUTF16(IDS_PROFILE_ON_NETWORK_WARNING,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  title->SetMultiLine(true);
  title->SizeToFit(kNotificationBubbleWidth);
  title->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
  layout->AddView(title);

  views::ColumnSet* bottom_columns = layout->AddColumnSet(1);
  bottom_columns->AddColumn(views::GridLayout::CENTER,
      views::GridLayout::CENTER, 0, views::GridLayout::USE_PREF, 0, 0);
  bottom_columns->AddPaddingColumn(1, 0);
  bottom_columns->AddColumn(views::GridLayout::CENTER,
      views::GridLayout::CENTER, 0, views::GridLayout::USE_PREF, 0, 0);
  layout->StartRowWithPadding(0, 1, 0,
                              views::kRelatedControlSmallVerticalSpacing);

  views::Link* learn_more =
      new views::Link(l10n_util::GetStringUTF16(IDS_LEARN_MORE));
  learn_more->set_listener(this);
  layout->AddView(learn_more);

  views::NativeTextButton* ok_button = new views::NativeTextButton(
      this, UTF16ToWide(l10n_util::GetStringUTF16(IDS_OK)));
  ok_button->SetIsDefault(true);
  layout->AddView(ok_button);
}

gfx::Rect NetworkProfileBubble::GetAnchorRect() {
  // Compensate for padding in anchor.
  gfx::Rect rect(BubbleDelegateView::GetAnchorRect());
  rect.Inset(0, anchor_view() ? kAnchorVerticalInset : 0);
  return rect;
}

void NetworkProfileBubble::LinkClicked(views::Link* source, int event_flags) {
  RecordUmaEvent(METRIC_LEARN_MORE_CLICKED);
  WindowOpenDisposition disposition =
      browser::DispositionFromEventFlags(event_flags);
  content::OpenURLParams params(
      GURL("https://sites.google.com/a/chromium.org/dev/administrators/"
            "common-problems-and-solutions#network_profile"),
      content::Referrer(),
      disposition == CURRENT_TAB ? NEW_FOREGROUND_TAB : disposition,
      content::PAGE_TRANSITION_LINK, false);
  navigator_->OpenURL(params);

  // If the user interacted with the bubble we don't reduce the number of
  // warnings left.
  PrefService* prefs = profile_->GetPrefs();
  int left_warnings = prefs->GetInteger(prefs::kNetworkProfileWarningsLeft);
  prefs->SetInteger(prefs::kNetworkProfileWarningsLeft, ++left_warnings);
  GetWidget()->Close();
}

void NetworkProfileBubble::ButtonPressed(views::Button* sender,
                                         const views::Event& event) {
  RecordUmaEvent(METRIC_ACKNOWLEDGED);

  GetWidget()->Close();
}

// static
void NetworkProfileBubble::NotifyNetworkProfileDetected() {
  if (BrowserList::GetLastActive() != NULL)
    ShowNotification(BrowserList::GetLastActive());
  else
    BrowserList::AddObserver(new BrowserListObserver());
}
