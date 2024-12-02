// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/cocoa/web_intent_picker_cocoa.h"
#import "chrome/browser/ui/cocoa/web_intent_sheet_controller.h"
#include "chrome/browser/ui/intents/web_intent_picker_delegate.h"
#include "chrome/browser/ui/intents/web_intent_picker_delegate_mock.h"
#include "chrome/browser/ui/intents/web_intent_picker_model.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "webkit/glue/web_intent_service_data.h"

class WebIntentSheetControllerBrowserTest : public InProcessBrowserTest {
 public:
  void CreateBubble(content::WebContents* web_contents);
  void CreatePicker();

  WebIntentPickerSheetController* controller_;  // Weak, owns self.
  NSWindow* window_;  // Weak, owned by controller.
  scoped_ptr<WebIntentPickerCocoa> picker_;
  WebIntentPickerDelegateMock delegate_;
  WebIntentPickerModel model_;  // The model used by the picker
};

void WebIntentSheetControllerBrowserTest::CreateBubble(
    content::WebContents* web_contents) {
  picker_.reset(new WebIntentPickerCocoa(web_contents, &delegate_, &model_));

  controller_ =
     [[WebIntentPickerSheetController alloc] initWithPicker:picker_.get()];
  window_ = [controller_ window];
  [controller_ showWindow:nil];
}

void WebIntentSheetControllerBrowserTest::CreatePicker() {
  picker_.reset(new WebIntentPickerCocoa());
  picker_->delegate_ = &delegate_;
  picker_->model_ = &model_;
  window_ = nil;
  controller_ = nil;
}

IN_PROC_BROWSER_TEST_F(WebIntentSheetControllerBrowserTest, CloseWillClose) {
  CreateBubble(chrome::GetActiveWebContents(browser()));

  EXPECT_CALL(delegate_, OnUserCancelledPickerDialog()).Times(0);
  EXPECT_CALL(delegate_, OnClosing());
  picker_->Close();

  ignore_result(picker_.release());  // Closing |picker_| will destruct it.
}

IN_PROC_BROWSER_TEST_F(WebIntentSheetControllerBrowserTest,
    DontCancelAfterServiceInvokation) {
  CreateBubble(chrome::GetActiveWebContents(browser()));

  GURL url;
  model_.AddInstalledService(string16(), url,
      webkit_glue::WebIntentServiceData::DISPOSITION_WINDOW);

  EXPECT_CALL(delegate_, OnServiceChosen(
      url, webkit_glue::WebIntentServiceData::DISPOSITION_WINDOW,
      WebIntentPickerDelegate::kEnableDefaults));
  EXPECT_CALL(delegate_, OnUserCancelledPickerDialog()).Times(0);
  EXPECT_CALL(delegate_, OnClosing());

  picker_->OnServiceChosen(0);
  picker_->Close();

  ignore_result(picker_.release());  // Closing |picker_| will destruct it.
}

IN_PROC_BROWSER_TEST_F(WebIntentSheetControllerBrowserTest,
    OnCancelledWillSignalClose) {
  CreatePicker();
  EXPECT_CALL(delegate_, OnUserCancelledPickerDialog());
  EXPECT_CALL(delegate_, OnClosing());
  picker_->OnCancelled();

  ignore_result(picker_.release());  // Closing |picker_| will destruct it.
}
