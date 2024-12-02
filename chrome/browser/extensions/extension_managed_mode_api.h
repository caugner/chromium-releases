// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines the Chrome Extensions Managed Mode API relevant classes to realize
// the API as specified in the extension API JSON.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGED_MODE_API_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGED_MODE_API_H_
#pragma once

#include "chrome/browser/extensions/extension_function.h"

class GetManagedModeFunction : public SyncExtensionFunction {
 public:
  virtual ~GetManagedModeFunction();
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION_NAME("experimental.managedMode.get")
};

class EnterManagedModeFunction : public SyncExtensionFunction {
 public:
  virtual ~EnterManagedModeFunction();
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION_NAME("experimental.managedMode.enter")
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGED_MODE_API_H_
