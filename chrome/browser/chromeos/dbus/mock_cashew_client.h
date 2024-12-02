// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DBUS_MOCK_CASHEW_CLIENT_H_
#define CHROME_BROWSER_CHROMEOS_DBUS_MOCK_CASHEW_CLIENT_H_
#pragma once

#include "chrome/browser/chromeos/dbus/cashew_client.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace chromeos {

class MockCashewClient : public CashewClient {
 public:
  MockCashewClient();
  virtual ~MockCashewClient();

  MOCK_METHOD1(SetDataPlansUpdateHandler, void(DataPlansUpdateHandler handler));
  MOCK_METHOD0(ResetDataPlansUpdateHandler, void());
  MOCK_METHOD0(RequestDataPlansUpdate, void());
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_DBUS_MOCK_CASHEW_CLIENT_H_
