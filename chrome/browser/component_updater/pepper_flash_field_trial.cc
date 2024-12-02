// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/component_updater/pepper_flash_field_trial.h"

#include "base/command_line.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/field_trial.h"
#include "chrome/common/chrome_switches.h"

namespace {

const char* const kFieldTrialName = "PepperFlash";
const char* const kDisableGroupName = "DisableByDefault";
const char* const kEnableGroupName = "EnableByDefault";

void ActivateFieldTrial() {
  // The field trial will expire on Jan 1st, 2014.
  scoped_refptr<base::FieldTrial> trial(
      new base::FieldTrial(kFieldTrialName, 1000, kDisableGroupName,
                           2014, 1, 1));

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kPpapiFlashFieldTrial)) {
    std::string switch_value =
        command_line->GetSwitchValueASCII(switches::kPpapiFlashFieldTrial);
    if (switch_value == switches::kPpapiFlashFieldTrialEnableByDefault) {
      trial->AppendGroup(kEnableGroupName, 1000);
      return;
    } else if (switch_value ==
        switches::kPpapiFlashFieldTrialDisableByDefault) {
      return;
    }
  }

  // Disable by default if one time randomization is not available.
  if (!base::FieldTrialList::IsOneTimeRandomizationEnabled())
    return;

  trial->UseOneTimeRandomization();
  // 50% goes into the enable-by-default group.
  trial->AppendGroup(kEnableGroupName, 500);
}

}  // namespace

// static
bool PepperFlashFieldTrial::InEnableByDefaultGroup() {
  static bool activated = false;
  if (!activated) {
    ActivateFieldTrial();
    activated = true;
  }

  int group = base::FieldTrialList::FindValue(kFieldTrialName);
  return group != base::FieldTrial::kNotFinalized &&
         group != base::FieldTrial::kDefaultGroupNumber;
}
