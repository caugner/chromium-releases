// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_item_model.h"

#include <vector>

#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "content/public/test/mock_download_item.h"
#include "grit/generated_resources.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/gfx/font.h"

using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRefOfCopy;
using ::testing::SetArgPointee;
using ::testing::_;

namespace {

// Create a char array that has as many elements as there are download
// interrupt reasons. We can then use that in a COMPILE_ASSERT to make sure
// that all the interrupt reason codes are accounted for. The reason codes are
// unfortunately sparse, making this necessary.
char kInterruptReasonCounter[] = {
  0,                                // content::DOWNLOAD_INTERRUPT_REASON_NONE
#define INTERRUPT_REASON(name,value) 0,
#include "content/public/browser/download_interrupt_reason_values.h"
#undef INTERRUPT_REASON
};
const size_t kInterruptReasonCount = ARRAYSIZE_UNSAFE(kInterruptReasonCounter);

// DownloadItemModel with mocks several methods.
class TestDownloadItemModel : public DownloadItemModel {
 public:
  explicit TestDownloadItemModel(content::DownloadItem* download)
      : DownloadItemModel(download) {
  }

  MOCK_CONST_METHOD0(IsDriveDownload, bool());
  MOCK_CONST_METHOD0(GetTotalBytes, int64());
  MOCK_CONST_METHOD0(GetCompletedBytes, int64());
};

class DownloadItemModelTest : public testing::Test {
 public:
  DownloadItemModelTest() {}

  virtual ~DownloadItemModelTest() {
  }

 protected:
  // Sets up defaults for the download item and sets |model_| to a new
  // DownloadItemModel that uses the mock download item.
  void SetupDownloadItemDefaults() {
    ON_CALL(item_, GetReceivedBytes()).WillByDefault(Return(1));
    ON_CALL(item_, GetTotalBytes()).WillByDefault(Return(2));
    ON_CALL(item_, IsInProgress()).WillByDefault(Return(true));
    ON_CALL(item_, TimeRemaining(_)).WillByDefault(Return(false));
    ON_CALL(item_, GetMimeType()).WillByDefault(Return("text/html"));
    ON_CALL(item_, AllDataSaved()).WillByDefault(Return(false));
    ON_CALL(item_, GetOpenWhenComplete()).WillByDefault(Return(false));
    ON_CALL(item_, GetFileExternallyRemoved()).WillByDefault(Return(false));
    ON_CALL(item_, GetState())
        .WillByDefault(Return(content::DownloadItem::IN_PROGRESS));
    ON_CALL(item_, GetURL())
        .WillByDefault(ReturnRefOfCopy(GURL("http://example.com/foo.bar")));
    ON_CALL(item_, GetFileNameToReportUser())
        .WillByDefault(Return(FilePath(FILE_PATH_LITERAL("foo.bar"))));
    ON_CALL(item_, GetTargetDisposition())
        .WillByDefault(
            Return(content::DownloadItem::TARGET_DISPOSITION_OVERWRITE));
    ON_CALL(item_, IsPaused()).WillByDefault(Return(false));

    // Setup the model:
    model_.reset(new NiceMock<TestDownloadItemModel>(&item_));
    ON_CALL(*model_.get(), IsDriveDownload())
        .WillByDefault(Return(false));
    ON_CALL(*model_.get(), GetTotalBytes())
        .WillByDefault(Return(2));
    ON_CALL(*model_.get(), GetCompletedBytes())
        .WillByDefault(Return(1));
  }

  void SetupInterruptedDownloadItem(content::DownloadInterruptReason reason) {
    EXPECT_CALL(item_, GetLastReason()).WillRepeatedly(Return(reason));
    EXPECT_CALL(item_, GetState())
        .WillRepeatedly(Return(
            (reason == content::DOWNLOAD_INTERRUPT_REASON_NONE) ?
                content::DownloadItem::IN_PROGRESS :
                content::DownloadItem::INTERRUPTED));
    EXPECT_CALL(item_, IsInProgress())
        .WillRepeatedly(Return(
            reason == content::DOWNLOAD_INTERRUPT_REASON_NONE));
  }

  content::MockDownloadItem& item() {
    return item_;
  }

  TestDownloadItemModel& model() {
    return *model_;
  }

 private:
  scoped_ptr<TestDownloadItemModel> model_;

  NiceMock<content::MockDownloadItem> item_;
};

}  // namespace

TEST_F(DownloadItemModelTest, InterruptedStatus) {
  // Test that we have the correct interrupt status message for downloads that
  // are in the INTERRUPTED state.
  const struct TestCase {
    // The reason.
    content::DownloadInterruptReason reason;

    // Expected status string. This will include the progress as well.
    const char* expected_status;
  } kTestCases[] = {
    { content::DOWNLOAD_INTERRUPT_REASON_NONE,
      "1/2 B" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
      "1/2 B Download Error" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED,
      "1/2 B Insufficient Permissions" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE,
      "1/2 B Disk Full" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG,
      "1/2 B Path Too Long" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_TOO_LARGE,
      "1/2 B File Too Large" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_VIRUS_INFECTED,
      "1/2 B Virus Detected" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED,
      "1/2 B Blocked" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED,
      "1/2 B Virus Scan Failed" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_TRANSIENT_ERROR,
      "1/2 B System Busy" },
    { content::DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED,
      "1/2 B Network Error" },
    { content::DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT,
      "1/2 B Network Timeout" },
    { content::DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED,
      "1/2 B Network Disconnected" },
    { content::DOWNLOAD_INTERRUPT_REASON_NETWORK_SERVER_DOWN,
      "1/2 B Server Unavailable" },
    { content::DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED,
      "1/2 B Server Problem" },
    { content::DOWNLOAD_INTERRUPT_REASON_SERVER_NO_RANGE,
      "1/2 B Download Error" },
    { content::DOWNLOAD_INTERRUPT_REASON_SERVER_PRECONDITION,
      "1/2 B Download Error" },
    { content::DOWNLOAD_INTERRUPT_REASON_SERVER_BAD_CONTENT,
      "1/2 B No File" },
    { content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED,
      "Cancelled" },
    { content::DOWNLOAD_INTERRUPT_REASON_USER_SHUTDOWN,
      "1/2 B Shutdown" },
    { content::DOWNLOAD_INTERRUPT_REASON_CRASH,
      "1/2 B Shutdown" },
  };
  COMPILE_ASSERT(kInterruptReasonCount == ARRAYSIZE_UNSAFE(kTestCases),
                 interrupt_reason_mismatch);

  SetupDownloadItemDefaults();
  for (unsigned i = 0; i < ARRAYSIZE_UNSAFE(kTestCases); ++i) {
    const TestCase& test_case = kTestCases[i];
    SetupInterruptedDownloadItem(test_case.reason);
    EXPECT_STREQ(test_case.expected_status,
                 UTF16ToUTF8(model().GetStatusText()).c_str());
  }
}

// Note: This test is currently skipped on Android. See http://crbug.com/139398
TEST_F(DownloadItemModelTest, InterruptTooltip) {
  // Test that we have the correct interrupt tooltip for downloads that are in
  // the INTERRUPTED state.
  const struct TestCase {
    // The reason.
    content::DownloadInterruptReason reason;

    // Expected tooltip text. The tooltip text for interrupted downloads
    // typically consist of two lines. One for the filename and one for the
    // interrupt reason. The returned string contains a newline.
    const char* expected_tooltip;
  } kTestCases[] = {
    { content::DOWNLOAD_INTERRUPT_REASON_NONE,
      "foo.bar" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
      "foo.bar\nDownload Error" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED,
      "foo.bar\nInsufficient Permissions" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE,
      "foo.bar\nDisk Full" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG,
      "foo.bar\nPath Too Long" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_TOO_LARGE,
      "foo.bar\nFile Too Large" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_VIRUS_INFECTED,
      "foo.bar\nVirus Detected" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED,
      "foo.bar\nBlocked" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED,
      "foo.bar\nVirus Scan Failed" },
    { content::DOWNLOAD_INTERRUPT_REASON_FILE_TRANSIENT_ERROR,
      "foo.bar\nSystem Busy" },
    { content::DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED,
      "foo.bar\nNetwork Error" },
    { content::DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT,
      "foo.bar\nNetwork Timeout" },
    { content::DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED,
      "foo.bar\nNetwork Disconnected" },
    { content::DOWNLOAD_INTERRUPT_REASON_NETWORK_SERVER_DOWN,
      "foo.bar\nServer Unavailable" },
    { content::DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED,
      "foo.bar\nServer Problem" },
    { content::DOWNLOAD_INTERRUPT_REASON_SERVER_NO_RANGE,
      "foo.bar\nDownload Error" },
    { content::DOWNLOAD_INTERRUPT_REASON_SERVER_PRECONDITION,
      "foo.bar\nDownload Error" },
    { content::DOWNLOAD_INTERRUPT_REASON_SERVER_BAD_CONTENT,
      "foo.bar\nNo File" },
    { content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED,
      "foo.bar" },
    { content::DOWNLOAD_INTERRUPT_REASON_USER_SHUTDOWN,
      "foo.bar\nShutdown" },
    { content::DOWNLOAD_INTERRUPT_REASON_CRASH,
      "foo.bar\nShutdown" },
  };
  COMPILE_ASSERT(kInterruptReasonCount == ARRAYSIZE_UNSAFE(kTestCases),
                 interrupt_reason_mismatch);

  // Large tooltip width. Should be large enough to accommodate the entire
  // tooltip without truncation.
  const int kLargeTooltipWidth = 1000;

  // Small tooltip width. Small enough to require truncation of most
  // tooltips. Used to test eliding logic.
  const int kSmallTooltipWidth = 40;

  gfx::Font font;
  SetupDownloadItemDefaults();
  for (unsigned i = 0; i < ARRAYSIZE_UNSAFE(kTestCases); ++i) {
    const TestCase& test_case = kTestCases[i];
    SetupInterruptedDownloadItem(test_case.reason);

    // GetTooltipText() elides the tooltip so that the text would fit within a
    // given width. The following test would fail if kLargeTooltipWidth is large
    // enough to accomodate all the strings.
    EXPECT_STREQ(
        test_case.expected_tooltip,
        UTF16ToUTF8(model().GetTooltipText(font, kLargeTooltipWidth)).c_str());

    // Check that if the width is small, the returned tooltip only contains
    // lines of the given width or smaller.
    std::vector<string16> lines;
    string16 truncated_tooltip =
        model().GetTooltipText(font, kSmallTooltipWidth);
    Tokenize(truncated_tooltip, ASCIIToUTF16("\n"), &lines);
    for (unsigned i = 0; i < lines.size(); ++i)
      EXPECT_GE(kSmallTooltipWidth, font.GetStringWidth(lines[i]));
  }
}

TEST_F(DownloadItemModelTest, InProgressStatus) {
  struct TestCase {
    int64 received_bytes;               // Return value of GetReceivedBytes().
    int64 total_bytes;                  // Return value of GetTotalBytes().
    bool  time_remaining_known;         // If TimeRemaining() is known.
    bool  open_when_complete;           // GetOpenWhenComplete().
    bool  is_paused;                    // IsPaused().
    bool  is_drive_download;            // Is Drive download?
    const char* expected_status;        // Expected status text.
  } kTestCases[] = {
    // These are all the valid combinations of the above fields for a download
    // that is in IN_PROGRESS state. Go through all of them and check the return
    // value of DownloadItemModel::GetStatusText(). The point isn't to lock down
    // the status strings, but to make sure we end up with something sane for
    // all the circumstances we care about.
    //
    // For GetReceivedBytes()/GetTotalBytes(), we only check whether each is
    // non-zero. In addition, if |total_bytes| is zero, then
    // |time_remaining_known| is also false.
    //
    //         .-- .TimeRemaining() is known.
    //        |       .-- .GetOpenWhenComplete()
    //        |      |      .---- .IsPaused()
    //        |      |      |      .---- Is Drive download?
    { 0, 0, false, false, false, false, "Starting..." },
    { 1, 0, false, false, false, false, "1 B" },
    { 0, 2, false, false, false, false, "Starting..." },
    { 1, 2, false, false, false, false, "1/2 B" },
    { 0, 2, true,  false, false, false, "0/2 B, 10 secs left" },
    { 1, 2, true,  false, false, false, "1/2 B, 10 secs left" },
    { 0, 0, false, true,  false, false, "Opening when complete" },
    { 1, 0, false, true,  false, false, "Opening when complete" },
    { 0, 2, false, true,  false, false, "Opening when complete" },
    { 1, 2, false, true,  false, false, "Opening when complete" },
    { 0, 2, true,  true,  false, false, "Opening in 10 secs..." },
    { 1, 2, true,  true,  false, false, "Opening in 10 secs..." },
    { 0, 0, false, false, true,  false, "0 B, Paused" },
    { 1, 0, false, false, true,  false, "1 B, Paused" },
    { 0, 2, false, false, true,  false, "0/2 B, Paused" },
    { 1, 2, false, false, true,  false, "1/2 B, Paused" },
    { 0, 2, true,  false, true,  false, "0/2 B, Paused" },
    { 1, 2, true,  false, true,  false, "1/2 B, Paused" },
    { 0, 0, false, true,  true,  false, "0 B, Paused" },
    { 1, 0, false, true,  true,  false, "1 B, Paused" },
    { 0, 2, false, true,  true,  false, "0/2 B, Paused" },
    { 1, 2, false, true,  true,  false, "1/2 B, Paused" },
    { 0, 2, true,  true,  true,  false, "0/2 B, Paused" },
    { 1, 2, true,  true,  true,  false, "1/2 B, Paused" },
#if defined(OS_CHROMEOS)
    // For Drive downloads, .TimeRemaining() is ignored since the actual time
    // remaining should come from the upload portion. Currently that
    // functionality is missing. So the |time_remaining_known| == true test
    // cases are equivalent to the |time_remaining_known| == false test cases.
    { 0, 0, false, false, false, true,  "Downloading..." },
    { 1, 0, false, false, false, true,  "1 B" },
    { 0, 2, false, false, false, true,  "Downloading..." },
    { 1, 2, false, false, false, true,  "1/2 B" },
    { 0, 2, true,  false, false, true,  "Downloading..." },
    { 1, 2, true,  false, false, true,  "1/2 B" },
    { 0, 0, false, true,  false, true,  "Opening when complete" },
    { 1, 0, false, true,  false, true,  "Opening when complete" },
    { 0, 2, false, true,  false, true,  "Opening when complete" },
    { 1, 2, false, true,  false, true,  "Opening when complete" },
    { 0, 2, true,  true,  false, true,  "Opening when complete" },
    { 1, 2, true,  true,  false, true,  "Opening when complete" },
    { 0, 0, false, false, true,  true,  "0 B, Paused" },
    { 1, 0, false, false, true,  true,  "1 B, Paused" },
    { 0, 2, false, false, true,  true,  "0/2 B, Paused" },
    { 1, 2, false, false, true,  true,  "1/2 B, Paused" },
    { 0, 2, true,  false, true,  true,  "0/2 B, Paused" },
    { 1, 2, true,  false, true,  true,  "1/2 B, Paused" },
    { 0, 0, false, true,  true,  true,  "0 B, Paused" },
    { 1, 0, false, true,  true,  true,  "1 B, Paused" },
    { 0, 2, false, true,  true,  true,  "0/2 B, Paused" },
    { 1, 2, false, true,  true,  true,  "1/2 B, Paused" },
    { 0, 2, true,  true,  true,  true,  "0/2 B, Paused" },
    { 1, 2, true,  true,  true,  true,  "1/2 B, Paused" },
#endif
  };

  SetupDownloadItemDefaults();

  for (unsigned i = 0; i < ARRAYSIZE_UNSAFE(kTestCases); i++) {
    TestCase& test_case = kTestCases[i];
    Mock::VerifyAndClearExpectations(&item());
    Mock::VerifyAndClearExpectations(&model());
    EXPECT_CALL(model(), GetCompletedBytes())
        .WillRepeatedly(Return(test_case.received_bytes));
    EXPECT_CALL(model(), GetTotalBytes())
        .WillRepeatedly(Return(test_case.total_bytes));
    EXPECT_CALL(model(), IsDriveDownload())
        .WillRepeatedly(Return(test_case.is_drive_download));
    EXPECT_CALL(item(), TimeRemaining(_))
        .WillRepeatedly(testing::DoAll(
            testing::SetArgPointee<0>(base::TimeDelta::FromSeconds(10)),
            Return(test_case.time_remaining_known)));
    EXPECT_CALL(item(), GetOpenWhenComplete())
        .WillRepeatedly(Return(test_case.open_when_complete));
    EXPECT_CALL(item(), IsPaused())
        .WillRepeatedly(Return(test_case.is_paused));

    EXPECT_STREQ(test_case.expected_status,
                 UTF16ToUTF8(model().GetStatusText()).c_str());
  }
}
