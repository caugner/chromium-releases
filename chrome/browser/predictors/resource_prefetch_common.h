// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREDICTORS_RESOURCE_PREFETCH_COMMON_H_
#define CHROME_BROWSER_PREDICTORS_RESOURCE_PREFETCH_COMMON_H_

#include "base/time.h"
#include "googleurl/src/gurl.h"

namespace content {
class WebContents;
}

namespace predictors {

// Represents a single navigation for a render view.
struct NavigationID {
  // TODO(shishir): Maybe take process_id, view_id and url as input in
  // constructor.
  NavigationID();
  NavigationID(const NavigationID& other);
  explicit NavigationID(const content::WebContents& web_contents);
  bool operator<(const NavigationID& rhs) const;
  bool operator==(const NavigationID& rhs) const;

  bool IsSameRenderer(const NavigationID& other) const;

  // Returns true iff the render_process_id_, render_view_id_ and
  // main_frame_url_ has been set correctly.
  bool is_valid() const;

  int render_process_id;
  int render_view_id;
  GURL main_frame_url;

  // NOTE: Even though we store the creation time here, it is not used during
  // comparison of two NavigationIDs because it cannot always be determined
  // correctly.
  base::TimeTicks creation_time;
};

// Represents the config for the resource prefetch prediction algorithm. It is
// useful for running experiments.
struct ResourcePrefetchPredictorConfig {
  // Initializes the config with default values.
  ResourcePrefetchPredictorConfig();

  // If a navigation hasn't seen a load complete event in this much time, it
  // is considered abandoned.
  int max_navigation_lifetime_seconds;
  // Size of LRU caches for the URL data.
  int max_urls_to_track;
  // The number of times, we should have seen a visit to this URL in history
  // to start tracking it. This is to ensure we dont bother with oneoff
  // entries.
  int min_url_visit_count;
  // The maximum number of resources to store per entry.
  int max_resources_per_entry;
  // The number of consecutive misses after we stop tracking a resource URL.
  int max_consecutive_misses;

  // The minimum confidence (accuracy of hits) required for a resource to be
  // prefetched.
  float min_resource_confidence_to_trigger_prefetch;
  // The minimum number of times we must have a URL on record to prefetch it.
  int min_resource_hits_to_trigger_prefetch;

  // Maximum number of prefetches that can be inflight for a single navigation.
  int max_prefetches_inflight_per_navigation;
  // Maximum number of prefetches that can be inflight for a host for a single
  // navigation.
  int max_prefetches_inflight_per_host_per_navigation;
};

}  // namespace predictors

#endif  // CHROME_BROWSER_PREDICTORS_RESOURCE_PREFETCH_COMMON_H_
