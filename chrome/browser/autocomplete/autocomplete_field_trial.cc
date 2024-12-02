// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/autocomplete_field_trial.h"

#include <string>

#include "base/metrics/field_trial.h"
#include "base/string_number_conversions.h"
#include "chrome/common/metrics/variations/variation_ids.h"
#include "chrome/common/metrics/variations/variations_util.h"

namespace {

// Field trial names.
static const char kDisallowInlineHQPFieldTrialName[] =
    "OmniboxDisallowInlineHQP";
// Because we regularly change the name of the suggest field trial in
// order to shuffle users among groups, we use the date the current trial
// was created as part of the name.
static const char kSuggestFieldTrialStarted2012Q4Name[] =
    "OmniboxSearchSuggestTrialStarted2012Q4";
static const char kHQPNewScoringFieldTrialName[] = "OmniboxHQPNewScoring";
static const char kHUPCullRedirectsFieldTrialName[] = "OmniboxHUPCullRedirects";
static const char kHUPCreateShorterMatchFieldTrialName[] =
    "OmniboxHUPCreateShorterMatch";

// Field trial experiment probabilities.

// For inline History Quick Provider field trial, put 0% ( = 0/100 )
// of the users in the disallow-inline experiment group.
const base::FieldTrial::Probability kDisallowInlineHQPFieldTrialDivisor = 100;
const base::FieldTrial::Probability
    kDisallowInlineHQPFieldTrialExperimentFraction = 0;

// For the search suggestion field trial, divide the people in the
// trial into 20 equally-sized buckets.  The suggest provider backend
// will decide what behavior (if any) to change based on the group.
const int kSuggestFieldTrialNumberOfGroups = 20;

// For History Quick Provider new scoring field trial, put 0% ( = 0/100 )
// of the users in the new scoring experiment group.
const base::FieldTrial::Probability kHQPNewScoringFieldTrialDivisor = 100;
const base::FieldTrial::Probability
    kHQPNewScoringFieldTrialExperimentFraction = 0;

// For HistoryURL provider cull redirects field trial, put 0% ( = 0/100 )
// of the users in the don't-cull-redirects experiment group.
// TODO(mpearson): Remove this field trial and the code it uses once I'm
// sure it's no longer needed.
const base::FieldTrial::Probability kHUPCullRedirectsFieldTrialDivisor = 100;
const base::FieldTrial::Probability
    kHUPCullRedirectsFieldTrialExperimentFraction = 0;

// For HistoryURL provider create shorter match field trial, put 0%
// ( = 25/100 ) of the users in the don't-create-a-shorter-match
// experiment group.
// TODO(mpearson): Remove this field trial and the code it uses once I'm
// sure it's no longer needed.
const base::FieldTrial::Probability
    kHUPCreateShorterMatchFieldTrialDivisor = 100;
const base::FieldTrial::Probability
    kHUPCreateShorterMatchFieldTrialExperimentFraction = 0;

// Field trial IDs.
// Though they are not literally "const", they are set only once, in
// Activate() below.

// Field trial ID for the disallow-inline History Quick Provider
// experiment group.
int disallow_inline_hqp_experiment_group = 0;

// Field trial ID for the History Quick Provider new scoring experiment group.
int hqp_new_scoring_experiment_group = 0;

// Field trial ID for the HistoryURL provider cull redirects experiment group.
int hup_dont_cull_redirects_experiment_group = 0;

// Field trial ID for the HistoryURL provider create shorter match
// experiment group.
int hup_dont_create_shorter_match_experiment_group = 0;

}


void AutocompleteFieldTrial::Activate() {
  // Create inline History Quick Provider field trial.
  // Make it expire on November 8, 2012.
  scoped_refptr<base::FieldTrial> trial(
      base::FieldTrialList::FactoryGetFieldTrial(
      kDisallowInlineHQPFieldTrialName, kDisallowInlineHQPFieldTrialDivisor,
      "Standard", 2012, 11, 8, NULL));
  // Because users tend to use omnibox without attention to it--habits
  // get ingrained, users tend to learn that a particular suggestion is
  // at a particular spot in the drop-down--we're going to make these
  // field trials sticky.  We want users to stay in them once assigned
  // so they have a better experience and also so we don't get weird
  // effects as omnibox ranking keeps changing and users learn they can't
  // trust the omnibox.
  trial->UseOneTimeRandomization();
  disallow_inline_hqp_experiment_group = trial->AppendGroup("DisallowInline",
      kDisallowInlineHQPFieldTrialExperimentFraction);

  // Create the suggest field trial.
  // Make it expire on July 1, 2013.
  trial = base::FieldTrialList::FactoryGetFieldTrial(
      kSuggestFieldTrialStarted2012Q4Name, kSuggestFieldTrialNumberOfGroups,
      "0", 2013, 7, 1, NULL);
  trial->UseOneTimeRandomization();

  // Mark this group in suggest requests to Google.
  chrome_variations::AssociateGoogleVariationID(
      kSuggestFieldTrialStarted2012Q4Name, "0",
      chrome_variations::kSuggestTrialStarted2012Q4IDMin);
  DCHECK_EQ(kSuggestFieldTrialNumberOfGroups,
      chrome_variations::kSuggestTrialStarted2012Q4IDMax -
      chrome_variations::kSuggestTrialStarted2012Q4IDMin + 1);

  // We've already created one group; now just need to create
  // kSuggestFieldTrialNumGroups - 1 more. Mark these groups in
  // suggest requests to Google.
  for (int i = 1; i < kSuggestFieldTrialNumberOfGroups; i++) {
    const std::string group_name = base::IntToString(i);
    trial->AppendGroup(group_name, 1);
    chrome_variations::AssociateGoogleVariationID(
        kSuggestFieldTrialStarted2012Q4Name, group_name,
        static_cast<chrome_variations::VariationID>(
            chrome_variations::kSuggestTrialStarted2012Q4IDMin + i));
  }

  // Create inline History Quick Provider new scoring field trial.
  // Make it expire on January 14, 2013.
  trial = base::FieldTrialList::FactoryGetFieldTrial(
      kHQPNewScoringFieldTrialName, kHQPNewScoringFieldTrialDivisor,
      "Standard", 2013, 1, 14, NULL);
  trial->UseOneTimeRandomization();
  hqp_new_scoring_experiment_group = trial->AppendGroup("NewScoring",
      kHQPNewScoringFieldTrialExperimentFraction);

  // Create the HistoryURL provider cull redirects field trial.
  // Make it expire on March 1, 2013.
  trial = base::FieldTrialList::FactoryGetFieldTrial(
      kHUPCullRedirectsFieldTrialName, kHUPCullRedirectsFieldTrialDivisor,
      "Standard", 2013, 3, 1, NULL);
  trial->UseOneTimeRandomization();
  hup_dont_cull_redirects_experiment_group =
      trial->AppendGroup("DontCullRedirects",
                         kHUPCullRedirectsFieldTrialExperimentFraction);

  // Create the HistoryURL provider create shorter match field trial.
  // Make it expire on March 1, 2013.
  trial = base::FieldTrialList::FactoryGetFieldTrial(
      kHUPCreateShorterMatchFieldTrialName,
      kHUPCreateShorterMatchFieldTrialDivisor, "Standard", 2013, 3, 1, NULL);
  trial->UseOneTimeRandomization();
  hup_dont_create_shorter_match_experiment_group =
      trial->AppendGroup("DontCreateShorterMatch",
                         kHUPCreateShorterMatchFieldTrialExperimentFraction);
}

bool AutocompleteFieldTrial::InDisallowInlineHQPFieldTrial() {
  return base::FieldTrialList::TrialExists(kDisallowInlineHQPFieldTrialName);
}

bool AutocompleteFieldTrial::InDisallowInlineHQPFieldTrialExperimentGroup() {
  if (!base::FieldTrialList::TrialExists(kDisallowInlineHQPFieldTrialName))
    return false;

  // Return true if we're in the experiment group.
  const int group = base::FieldTrialList::FindValue(
      kDisallowInlineHQPFieldTrialName);
  return group == disallow_inline_hqp_experiment_group;
}

bool AutocompleteFieldTrial::InSuggestFieldTrial() {
  return base::FieldTrialList::TrialExists(kSuggestFieldTrialStarted2012Q4Name);
}

std::string AutocompleteFieldTrial::GetSuggestGroupName() {
  return base::FieldTrialList::FindFullName(
      kSuggestFieldTrialStarted2012Q4Name);
}

// Yes, this is roundabout.  It's easier to provide the group number as
// a string (simply by choosing group names appropriately) than provide
// it as an integer.  It might look more straightforward to use group ids
// for the group number with respect to suggest.  However, we don't want
// to assume that group ids are creates as 0, 1, 2, ... -- this isn't part
// of the field_trial.h specification.  Hence, we use the group names to
// get numbers that we know are 0, 1, 2, ...
int AutocompleteFieldTrial::GetSuggestGroupNameAsNumber() {
  int group_num;
  base::StringToInt(GetSuggestGroupName(), &group_num);
  return group_num;
}

int AutocompleteFieldTrial::GetSuggestNumberOfGroups() {
  return kSuggestFieldTrialNumberOfGroups;
}

bool AutocompleteFieldTrial::InHQPNewScoringFieldTrial() {
  return base::FieldTrialList::TrialExists(kHQPNewScoringFieldTrialName);
}

bool AutocompleteFieldTrial::InHQPNewScoringFieldTrialExperimentGroup() {
  if (!InHQPNewScoringFieldTrial())
    return false;

  // Return true if we're in the experiment group.
  const int group = base::FieldTrialList::FindValue(
      kHQPNewScoringFieldTrialName);
  return group == hqp_new_scoring_experiment_group;
}

bool AutocompleteFieldTrial::InHUPCullRedirectsFieldTrial() {
  return base::FieldTrialList::TrialExists(kHUPCullRedirectsFieldTrialName);
}

bool AutocompleteFieldTrial::InHUPCullRedirectsFieldTrialExperimentGroup() {
  if (!base::FieldTrialList::TrialExists(kHUPCullRedirectsFieldTrialName))
    return false;

  // Return true if we're in the experiment group.
  const int group = base::FieldTrialList::FindValue(
      kHUPCullRedirectsFieldTrialName);
  return group == hup_dont_cull_redirects_experiment_group;
}

bool AutocompleteFieldTrial::InHUPCreateShorterMatchFieldTrial() {
  return
      base::FieldTrialList::TrialExists(kHUPCreateShorterMatchFieldTrialName);
}

bool AutocompleteFieldTrial::
    InHUPCreateShorterMatchFieldTrialExperimentGroup() {
  if (!base::FieldTrialList::TrialExists(kHUPCreateShorterMatchFieldTrialName))
    return false;

  // Return true if we're in the experiment group.
  const int group = base::FieldTrialList::FindValue(
      kHUPCreateShorterMatchFieldTrialName);
  return group == hup_dont_create_shorter_match_experiment_group;
}
