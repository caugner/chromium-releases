// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_POWER_POWER_SUPPLY_STATUS_H_
#define ASH_SYSTEM_POWER_POWER_SUPPLY_STATUS_H_

#include <string>

#include "ash/ash_export.h"
#include "base/basictypes.h"

namespace ash {

struct ASH_EXPORT PowerSupplyStatus {
  bool line_power_on;

  bool battery_is_present;
  bool battery_is_full;

  // Time in seconds until the battery is empty or full, 0 for unknown.
  int64 battery_seconds_to_empty;
  int64 battery_seconds_to_full;

  double battery_percentage;

  PowerSupplyStatus();
  std::string ToString() const;
};

}  // namespace ash

#endif  // ASH_SYSTEM_POWER_POWER_SUPPLY_STATUS_H_
