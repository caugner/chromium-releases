// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/drive_scheduler.h"

#include "base/bind.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/chromeos/drive/drive_test_util.h"
#include "chrome/browser/chromeos/drive/file_system/drive_operations.h"
#include "chrome/browser/chromeos/drive/file_system/remove_operation.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;

namespace drive {

namespace {

class MockNetworkChangeNotifier : public net::NetworkChangeNotifier {
 public:
  MOCK_CONST_METHOD0(GetCurrentConnectionType,
                     net::NetworkChangeNotifier::ConnectionType());
};

class MockRemoveOperation : public file_system::RemoveOperation {
 public:
  MockRemoveOperation()
      : file_system::RemoveOperation(NULL, NULL, NULL, NULL) {
  }

  MOCK_METHOD3(Remove, void(const FilePath& file_path,
                            bool is_recursive,
                            const FileOperationCallback& callback));
};

// Action used to set mock expectations for
// DriveFunctionRemove::Remove().
ACTION_P(MockRemove, status) {
  base::MessageLoopProxy::current()->PostTask(FROM_HERE,
                                              base::Bind(arg2, status));
}

}  // namespace

class DriveSchedulerTest : public testing::Test {
 public:
  DriveSchedulerTest()
      : ui_thread_(content::BrowserThread::UI, &message_loop_),
        profile_(new TestingProfile) {
  }

  virtual void SetUp() OVERRIDE {
    mock_network_change_notifier_.reset(new MockNetworkChangeNotifier);

    mock_remove_operation_ = new StrictMock<MockRemoveOperation>();
    drive_operations_.InitForTesting(NULL, NULL, mock_remove_operation_);
    scheduler_.reset(new DriveScheduler(profile_.get(),
                                        &drive_operations_));

    scheduler_->Initialize();
    scheduler_->SetDisableThrottling(true);
  }

  virtual void TearDown() OVERRIDE {
    // The scheduler should be deleted before NetworkLibrary, as it
    // registers itself as observer of NetworkLibrary.
    scheduler_.reset();
    google_apis::test_util::RunBlockingPoolTask();
    mock_network_change_notifier_.reset();
  }

  // Sets up MockNetworkChangeNotifier as if it's connected to a network with
  // the specified connection type.
  void ChangeConnectionType(net::NetworkChangeNotifier::ConnectionType type) {
    EXPECT_CALL(*mock_network_change_notifier_, GetCurrentConnectionType())
        .WillRepeatedly(Return(type));
    // Notify the sync client that the network is changed. This is done via
    // NetworkChangeNotifier in production, but here, we simulate the behavior
    // by directly calling OnConnectionTypeChanged().
    scheduler_->OnConnectionTypeChanged(type);
  }

  // Sets up MockNetworkChangeNotifier as if it's connected to wifi network.
  void ConnectToWifi() {
    ChangeConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  }

  // Sets up MockNetworkChangeNotifier as if it's connected to cellular network.
  void ConnectToCellular() {
    ChangeConnectionType(net::NetworkChangeNotifier::CONNECTION_2G);
  }

  // Sets up MockNetworkChangeNotifier as if it's connected to wimax network.
  void ConnectToWimax() {
    ChangeConnectionType(net::NetworkChangeNotifier::CONNECTION_4G);
  }

  // Sets up MockNetworkChangeNotifier as if it's disconnected.
  void ConnectToNone() {
    ChangeConnectionType(net::NetworkChangeNotifier::CONNECTION_NONE);
  }

 protected:
  MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  scoped_ptr<TestingProfile> profile_;
  scoped_ptr<DriveScheduler> scheduler_;
  scoped_ptr<MockNetworkChangeNotifier> mock_network_change_notifier_;

  file_system::DriveOperations drive_operations_;
  StrictMock<MockRemoveOperation>* mock_remove_operation_;
};

TEST_F(DriveSchedulerTest, RemoveFile) {
  ConnectToWifi();

  FilePath file_in_root(FILE_PATH_LITERAL("drive/File 1.txt"));
  EXPECT_CALL(*mock_remove_operation_, Remove(file_in_root, _, _))
      .WillOnce(MockRemove(DRIVE_FILE_OK));

  DriveFileError error;
  scheduler_->Remove(
      file_in_root, false,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  google_apis::test_util::RunBlockingPoolTask();

  ASSERT_EQ(DRIVE_FILE_OK, error);
}

TEST_F(DriveSchedulerTest, RemoveFileRetry) {
  ConnectToWifi();

  FilePath file_in_root(FILE_PATH_LITERAL("drive/File 1.txt"));
  EXPECT_CALL(*mock_remove_operation_, Remove(file_in_root, _, _))
      .WillOnce(MockRemove(DRIVE_FILE_ERROR_THROTTLED))
      .WillOnce(MockRemove(DRIVE_FILE_OK));

  DriveFileError error;
  scheduler_->Remove(
      file_in_root, false,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  google_apis::test_util::RunBlockingPoolTask();

  ASSERT_EQ(DRIVE_FILE_OK, error);
}

TEST_F(DriveSchedulerTest, QueueOperation_Offline) {
  ConnectToNone();

  // This file will not be removed, as network is not connected.
  EXPECT_CALL(*mock_remove_operation_, Remove(_, _, _)).Times(0);

  FilePath file_in_root(FILE_PATH_LITERAL("drive/File 1.txt"));
  DriveFileError error;
  scheduler_->Remove(
      file_in_root, false,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  google_apis::test_util::RunBlockingPoolTask();
}

TEST_F(DriveSchedulerTest, QueueOperation_CelluarDisabled) {
  ConnectToCellular();

  // This file will not be removed, as fetching over cellular network is
  // disabled by default.
  EXPECT_CALL(*mock_remove_operation_, Remove(_, _, _)).Times(0);

  FilePath file_in_root(FILE_PATH_LITERAL("drive/File 1.txt"));
  DriveFileError error;
  scheduler_->Remove(
      file_in_root, false,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  google_apis::test_util::RunBlockingPoolTask();
}

TEST_F(DriveSchedulerTest, QueueOperation_CelluarEnabled) {
  // Enable fetching over cellular network.
  profile_->GetPrefs()->SetBoolean(prefs::kDisableDriveOverCellular, false);

  ConnectToCellular();

  // This file will be removed, as syncing over cellular network is explicitly
  // enabled.
  EXPECT_CALL(*mock_remove_operation_, Remove(_, _, _)).Times(1);

  FilePath file_in_root(FILE_PATH_LITERAL("drive/File 1.txt"));
  DriveFileError error;
  scheduler_->Remove(
      file_in_root, false,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  google_apis::test_util::RunBlockingPoolTask();
}

TEST_F(DriveSchedulerTest, QueueOperation_WimaxDisabled) {
  // Then connect to wimax. This will kick off StartSyncLoop().
  ConnectToWimax();

  // This file will not be removed, as syncing over wimax network is disabled
  // by default.
  EXPECT_CALL(*mock_remove_operation_, Remove(_, _, _)).Times(0);

  FilePath file_in_root(FILE_PATH_LITERAL("drive/File 1.txt"));
  DriveFileError error;
  scheduler_->Remove(
      file_in_root, false,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  google_apis::test_util::RunBlockingPoolTask();
}

TEST_F(DriveSchedulerTest, QueueOperation_CelluarEnabledWithWimax) {
  // Enable fetching over cellular network.
  profile_->GetPrefs()->SetBoolean(prefs::kDisableDriveOverCellular, false);

  ConnectToWimax();

  // This file will be removed, as syncing over cellular network is explicitly
  // enabled.
  EXPECT_CALL(*mock_remove_operation_, Remove(_, _, _)).Times(1);

  FilePath file_in_root(FILE_PATH_LITERAL("drive/File 1.txt"));
  DriveFileError error;
  scheduler_->Remove(
      file_in_root, false,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  google_apis::test_util::RunBlockingPoolTask();
}

TEST_F(DriveSchedulerTest, QueueOperation_DriveDisabled) {
  // Disable the Drive feature.
  profile_->GetPrefs()->SetBoolean(prefs::kDisableDrive, true);

  // This file will not be removed, as the Drive feature is disabled.
  EXPECT_CALL(*mock_remove_operation_, Remove(_, _, _)).Times(0);

  FilePath file_in_root(FILE_PATH_LITERAL("drive/File 1.txt"));
  DriveFileError error;
  scheduler_->Remove(
      file_in_root, false,
      base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback, &error));
  google_apis::test_util::RunBlockingPoolTask();
}

}  // namespace drive
