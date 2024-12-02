// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_GPU_MEMORY_MANAGER_H_
#define CONTENT_COMMON_GPU_GPU_MEMORY_MANAGER_H_

#if defined(ENABLE_GPU)

#include <set>
#include <vector>

#include "base/basictypes.h"
#include "base/cancelable_callback.h"
#include "base/hash_tables.h"
#include "base/memory/weak_ptr.h"
#include "content/common/content_export.h"
#include "content/common/gpu/gpu_memory_allocation.h"
#include "content/public/common/gpu_memory_stats.h"
#include "ui/gfx/size.h"

namespace content {
class GpuCommandBufferStubBase;
}

#if defined(COMPILER_GCC)
namespace BASE_HASH_NAMESPACE {
template<>
struct hash<content::GpuCommandBufferStubBase*> {
  size_t operator()(content::GpuCommandBufferStubBase* ptr) const {
    return hash<size_t>()(reinterpret_cast<size_t>(ptr));
  }
};
} // namespace BASE_HASH_NAMESPACE
#endif // COMPILER

namespace content {
class GpuMemoryTrackingGroup;

class CONTENT_EXPORT GpuMemoryManagerClient {
public:
  virtual ~GpuMemoryManagerClient() {}

  virtual void AppendAllCommandBufferStubs(
      std::vector<GpuCommandBufferStubBase*>& stubs) = 0;
};

class CONTENT_EXPORT GpuMemoryManager :
    public base::SupportsWeakPtr<GpuMemoryManager> {
 public:
  enum { kDefaultMaxSurfacesWithFrontbufferSoftLimit = 8 };


  // StubMemoryStat is used to store memory-allocation-related information about
  // a GpuCommandBufferStubBase for some time point.
  struct StubMemoryStat {
    bool visible;
    GpuMemoryAllocation allocation;
  };

  GpuMemoryManager(GpuMemoryManagerClient* client,
                   size_t max_surfaces_with_frontbuffer_soft_limit);
  ~GpuMemoryManager();

  // Schedule a Manage() call. If immediate is true, we PostTask without delay.
  // Otherwise PostDelayedTask using a CancelableClosure and allow multiple
  // delayed calls to "queue" up. This way, we do not spam clients in certain
  // lower priority situations. An immediate schedule manage will cancel any
  // queued delayed manage.
  void ScheduleManage(bool immediate);

  // Retrieve GPU Resource consumption statistics for the task manager
  void GetVideoMemoryUsageStats(
      content::GPUVideoMemoryUsageStats& video_memory_usage_stats) const;
  void SetWindowCount(uint32 count);

  // Add and remove structures to track context groups' memory consumption
  void AddTrackingGroup(GpuMemoryTrackingGroup* tracking_group);
  void RemoveTrackingGroup(GpuMemoryTrackingGroup* tracking_group);

  // Returns StubMemoryStat's for each GpuCommandBufferStubBase, which were
  // assigned during the most recent call to Manage().
  // Useful for tracking the memory-allocation-related presumed state of the
  // system, as seen by GpuMemoryManager.
  typedef base::hash_map<GpuCommandBufferStubBase*, StubMemoryStat>
      StubMemoryStatMap;
  const StubMemoryStatMap& stub_memory_stats_for_last_manage() const {
    return stub_memory_stats_for_last_manage_;
  }

  // Track a change in memory allocated by any context
  void TrackMemoryAllocatedChange(size_t old_size, size_t new_size);

 private:
  friend class GpuMemoryManagerTest;

  void Manage();

  // The context groups' tracking structures
  std::set<GpuMemoryTrackingGroup*> tracking_groups_;

  size_t CalculateBonusMemoryAllocationBasedOnSize(gfx::Size size) const;

  // Update the amount of GPU memory we think we have in the system, based
  // on what the stubs' contexts report.
  void UpdateAvailableGpuMemory(std::vector<GpuCommandBufferStubBase*>& stubs);

  // The amount of video memory which is available for allocation
  size_t GetAvailableGpuMemory() const {
    return bytes_available_gpu_memory_;
  }

  // Default per-OS value for the amount of available GPU memory, used
  // if we can't query the driver for an exact value.
  size_t GetDefaultAvailableGpuMemory() const {
#if defined(OS_ANDROID)
    return 64 * 1024 * 1024;
#elif defined(OS_CHROMEOS)
    return 1024 * 1024 * 1024;
#else
    return 256 * 1024 * 1024;
#endif
  }

  // The maximum amount of memory that a tab may be assigned
size_t GetMaximumTabAllocation() const {
#if defined(OS_ANDROID)
    return 128 * 1024 * 1024;
#elif defined(OS_CHROMEOS)
    return bytes_available_gpu_memory_;
#else
    // This is to avoid allowing a single page on to use a full 256MB of memory
    // (the current total limit). Long-scroll pages will hit this limit,
    // resulting in instability on some platforms (e.g, issue 141377).
    return bytes_available_gpu_memory_ / 2;
#endif
  }

  // The minimum non-zero amount of memory that a tab may be assigned
  size_t GetMinimumTabAllocation() const {
#if defined(OS_ANDROID)
    return 32 * 1024 * 1024;
#elif defined(OS_CHROMEOS)
    return 64 * 1024 * 1024;
#else
    return 64 * 1024 * 1024;
#endif
  }

  class CONTENT_EXPORT StubWithSurfaceComparator {
   public:
    bool operator()(GpuCommandBufferStubBase* lhs,
                    GpuCommandBufferStubBase* rhs);
  };

  GpuMemoryManagerClient* client_;

  base::CancelableClosure delayed_manage_callback_;
  bool manage_immediate_scheduled_;

  size_t max_surfaces_with_frontbuffer_soft_limit_;

  StubMemoryStatMap stub_memory_stats_for_last_manage_;

  // The maximum amount of memory that may be allocated for GPU resources
  size_t bytes_available_gpu_memory_;
  bool bytes_available_gpu_memory_overridden_;

  // The current total memory usage, and historical maximum memory usage
  size_t bytes_allocated_current_;
  size_t bytes_allocated_historical_max_;

  // The number of browser windows that exist. If we ever receive a
  // GpuMsg_SetVideoMemoryWindowCount, then we use this to compute memory
  // budgets, instead of doing more complicated stub-based calculations.
  bool window_count_has_been_received_;
  uint32 window_count_;

  DISALLOW_COPY_AND_ASSIGN(GpuMemoryManager);
};

}  // namespace content

#endif

#endif // CONTENT_COMMON_GPU_GPU_MEMORY_MANAGER_H_
