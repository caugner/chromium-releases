// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/autofill/autofill_external_delegate_views.h"

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/autofill/autofill_manager.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/views/autofill/autofill_popup_view_views.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/rect.h"
#include "ui/views/widget/widget.h"

namespace {

class MockAutofillExternalDelegateViews : public AutofillExternalDelegateViews {
 public:
  explicit MockAutofillExternalDelegateViews(content::WebContents* web_contents)
      : AutofillExternalDelegateViews(
            web_contents,
            AutofillManager::FromWebContents(web_contents)),
        popup_hidden_(false) {}
  ~MockAutofillExternalDelegateViews() {}

  void HideAutofillPopupInternal() OVERRIDE {
    popup_hidden_ = true;
    AutofillExternalDelegateViews::HideAutofillPopupInternal();
  }

  AutofillPopupViewViews* popup_view() {
    return AutofillExternalDelegateViews::popup_view();
  }

  bool popup_hidden_;
};

}  // namespace

class AutofillExternalDelegateViewsBrowserTest : public InProcessBrowserTest {
 public:
  AutofillExternalDelegateViewsBrowserTest() {}
  virtual ~AutofillExternalDelegateViewsBrowserTest() {}

  virtual void SetUpOnMainThread() OVERRIDE {
    web_contents_ = chrome::GetActiveWebContents(browser());
    ASSERT_TRUE(web_contents_ != NULL);

    autofill_external_delegate_.reset(
        new MockAutofillExternalDelegateViews(web_contents_));
  }

  void GeneratePopup() {
    int query_id = 1;
    FormData form;
    FormFieldData field;
    field.is_focusable = true;
    field.should_autocomplete = true;
    gfx::Rect bounds(100, 100);

    // Ensure that we can populate the popup through the delegate without any
    // and then close it.
    autofill_external_delegate_->OnQuery(query_id, form, field, bounds, false);

    std::vector<string16> autofill_item;
    autofill_item.push_back(string16());
    std::vector<int> autofill_id;
    autofill_id.push_back(0);
    autofill_external_delegate_->OnSuggestionsReturned(
        query_id, autofill_item, autofill_item, autofill_item, autofill_id);
  }

 protected:
  content::WebContents* web_contents_;
  scoped_ptr<MockAutofillExternalDelegateViews> autofill_external_delegate_;
};

IN_PROC_BROWSER_TEST_F(AutofillExternalDelegateViewsBrowserTest,
                       OpenAndClosePopup) {
  GeneratePopup();

  autofill_external_delegate_->HideAutofillPopup();
  EXPECT_TRUE(autofill_external_delegate_->popup_hidden_);
}

IN_PROC_BROWSER_TEST_F(AutofillExternalDelegateViewsBrowserTest,
                       CloseWidgetAndNoLeaking) {
  GeneratePopup();

  // Delete the widget to ensure that the external delegate can handle the
  // popup getting deleted elsewhere and the .
  views::Widget* popup_widget =
      autofill_external_delegate_->popup_view()->GetWidget();
  popup_widget->CloseNow();

  EXPECT_TRUE(autofill_external_delegate_->popup_hidden_);
}
