// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/gpu/gpu_data_manager_impl.h"

#if defined(OS_MACOSX)
#include <ApplicationServices/ApplicationServices.h>
#endif  // OS_MACOSX

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/metrics/field_trial.h"
#include "base/string_piece.h"
#include "base/stringprintf.h"
#include "base/sys_info.h"
#include "base/values.h"
#include "base/version.h"
#include "content/browser/gpu/gpu_process_host.h"
#include "content/browser/gpu/gpu_util.h"
#include "content/common/gpu/gpu_messages.h"
#include "content/gpu/gpu_info_collector.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/gpu_data_manager_observer.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"
#include "grit/content_resources.h"
#include "ui/base/ui_base_switches.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_switches.h"
#include "ui/gl/gpu_switching_manager.h"
#include "webkit/plugins/plugin_switches.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

namespace content {
namespace {

// Strip out the non-digital info; if after that, we get an empty string,
// return "0".
std::string ProcessVersionString(const std::string& raw_string) {
  const std::string valid_set = "0123456789.";
  size_t start_pos = raw_string.find_first_of(valid_set);
  if (start_pos == std::string::npos)
    return "0";
  size_t end_pos = raw_string.find_first_not_of(raw_string, start_pos);
  std::string version_string = raw_string.substr(
      start_pos, end_pos - start_pos);
  if (version_string.empty())
    return "0";
  return version_string;
}

#if defined(OS_MACOSX)
void DisplayReconfigCallback(CGDirectDisplayID display,
                             CGDisplayChangeSummaryFlags flags,
                             void* gpu_data_manager) {
  if (flags & kCGDisplayAddFlag) {
    GpuDataManagerImpl* manager =
        reinterpret_cast<GpuDataManagerImpl*>(gpu_data_manager);
    DCHECK(manager);
    manager->HandleGpuSwitch();
  }
}
#endif  // OS_MACOSX

}  // namespace anonymous

// static
GpuDataManager* GpuDataManager::GetInstance() {
  return GpuDataManagerImpl::GetInstance();
}

// static
GpuDataManagerImpl* GpuDataManagerImpl::GetInstance() {
  return Singleton<GpuDataManagerImpl>::get();
}

GpuDataManagerImpl::GpuDataManagerImpl()
    : complete_gpu_info_already_requested_(false),
      blacklisted_features_(GPU_FEATURE_TYPE_UNKNOWN),
      preliminary_blacklisted_features_(GPU_FEATURE_TYPE_UNKNOWN),
      gpu_switching_(GPU_SWITCHING_OPTION_AUTOMATIC),
      observer_list_(new GpuDataManagerObserverList),
      software_rendering_(false),
      card_blacklisted_(false),
      update_histograms_(true),
      window_count_(0) {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kDisableAcceleratedCompositing)) {
    command_line->AppendSwitch(switches::kDisableAccelerated2dCanvas);
    command_line->AppendSwitch(switches::kDisableAcceleratedLayers);
  }
  if (command_line->HasSwitch(switches::kDisableGpu))
    BlacklistCard();
  if (command_line->HasSwitch(switches::kGpuSwitching)) {
    std::string option_string = command_line->GetSwitchValueASCII(
        switches::kGpuSwitching);
    GpuSwitchingOption option = StringToGpuSwitchingOption(option_string);
    if (option != GPU_SWITCHING_OPTION_UNKNOWN)
      gpu_switching_ = option;
  }

#if defined(OS_MACOSX)
  CGDisplayRegisterReconfigurationCallback(DisplayReconfigCallback, this);
#endif  // OS_MACOSX
}

void GpuDataManagerImpl::Initialize() {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kSkipGpuDataLoading))
    return;

  GPUInfo gpu_info;
  gpu_info_collector::CollectPreliminaryGraphicsInfo(&gpu_info);
#if defined(ARCH_CPU_X86_FAMILY)
  if (!gpu_info.gpu.vendor_id || !gpu_info.gpu.device_id)
    gpu_info.finalized = true;
#endif

  std::string gpu_blacklist_string;
  if (!command_line->HasSwitch(switches::kIgnoreGpuBlacklist)) {
    const base::StringPiece gpu_blacklist_json =
        GetContentClient()->GetDataResource(
            IDR_GPU_BLACKLIST, ui::SCALE_FACTOR_NONE);
    gpu_blacklist_string = gpu_blacklist_json.as_string();
  }

  InitializeImpl(gpu_blacklist_string, gpu_info);
}

void GpuDataManagerImpl::InitializeForTesting(
    const std::string& gpu_blacklist_json,
    const GPUInfo& gpu_info) {
  // This function is for testing only, so disable histograms.
  update_histograms_ = false;

  InitializeImpl(gpu_blacklist_json, gpu_info);
}

void GpuDataManagerImpl::InitializeImpl(
    const std::string& gpu_blacklist_json,
    const GPUInfo& gpu_info) {
  if (!gpu_blacklist_json.empty()) {
    std::string browser_version_string = ProcessVersionString(
        GetContentClient()->GetProduct());
    CHECK(!browser_version_string.empty());
    gpu_blacklist_.reset(new GpuBlacklist());
    bool succeed = gpu_blacklist_->LoadGpuBlacklist(
        browser_version_string,
        gpu_blacklist_json,
        GpuBlacklist::kCurrentOsOnly);
    CHECK(succeed);
  }

  UpdateGpuInfo(gpu_info);
  UpdateGpuSwitchingManager();
  UpdatePreliminaryBlacklistedFeatures();
}

GpuDataManagerImpl::~GpuDataManagerImpl() {
#if defined(OS_MACOSX)
  CGDisplayRemoveReconfigurationCallback(DisplayReconfigCallback, this);
#endif
}

void GpuDataManagerImpl::RequestCompleteGpuInfoIfNeeded() {
  if (complete_gpu_info_already_requested_ || gpu_info_.finalized)
    return;
  complete_gpu_info_already_requested_ = true;

  GpuProcessHost::SendOnIO(
      GpuProcessHost::GPU_PROCESS_KIND_UNSANDBOXED,
      CAUSE_FOR_GPU_LAUNCH_GPUDATAMANAGER_REQUESTCOMPLETEGPUINFOIFNEEDED,
      new GpuMsg_CollectGraphicsInfo());
}

bool GpuDataManagerImpl::IsCompleteGpuInfoAvailable() const {
  return gpu_info_.finalized;
}

void GpuDataManagerImpl::UpdateGpuInfo(const GPUInfo& gpu_info) {
  GetContentClient()->SetGpuInfo(gpu_info);

  if (gpu_blacklist_.get()) {
    GpuBlacklist::Decision decision =
        gpu_blacklist_->MakeBlacklistDecision(
            GpuBlacklist::kOsAny, "", gpu_info);
    if (update_histograms_)
      UpdateStats(gpu_blacklist_.get(), decision.blacklisted_features);

    UpdateBlacklistedFeatures(decision.blacklisted_features);
    if (decision.gpu_switching != GPU_SWITCHING_OPTION_UNKNOWN) {
      // Blacklist decision should not overwrite commandline switch from users.
      CommandLine* command_line = CommandLine::ForCurrentProcess();
      if (!command_line->HasSwitch(switches::kGpuSwitching))
        gpu_switching_ = decision.gpu_switching;
    }
  }

  {
    base::AutoLock auto_lock(gpu_info_lock_);
    gpu_info_ = gpu_info;
    complete_gpu_info_already_requested_ =
        complete_gpu_info_already_requested_ || gpu_info_.finalized;
  }

  // We have to update GpuFeatureType before notify all the observers.
  NotifyGpuInfoUpdate();
}

GPUInfo GpuDataManagerImpl::GetGPUInfo() const {
  GPUInfo gpu_info;
  {
    base::AutoLock auto_lock(gpu_info_lock_);
    gpu_info = gpu_info_;
  }
  return gpu_info;
}

void GpuDataManagerImpl::RequestVideoMemoryUsageStatsUpdate() const {
  GpuProcessHost::SendOnIO(
      GpuProcessHost::GPU_PROCESS_KIND_SANDBOXED,
      CAUSE_FOR_GPU_LAUNCH_NO_LAUNCH,
      new GpuMsg_GetVideoMemoryUsageStats());
}

void GpuDataManagerImpl::AddLogMessage(
    int level, const std::string& header, const std::string& message) {
  base::AutoLock auto_lock(log_messages_lock_);
  DictionaryValue* dict = new DictionaryValue();
  dict->SetInteger("level", level);
  dict->SetString("header", header);
  dict->SetString("message", message);
  log_messages_.Append(dict);
}

base::ListValue* GpuDataManagerImpl::GetLogMessages() const {
  base::ListValue* value;
  {
    base::AutoLock auto_lock(log_messages_lock_);
    value = log_messages_.DeepCopy();
  }
  return value;
}

std::string GpuDataManagerImpl::GetBlacklistVersion() const {
  if (gpu_blacklist_.get())
    return gpu_blacklist_->GetVersion();
  return "0";
}

GpuFeatureType GpuDataManagerImpl::GetBlacklistedFeatures() const {
  if (software_rendering_) {
    GpuFeatureType flags;

    // Skia's software rendering is probably more efficient than going through
    // software emulation of the GPU, so use that.
    flags = GPU_FEATURE_TYPE_ACCELERATED_2D_CANVAS;
    return flags;
  }

  return blacklisted_features_;
}

GpuSwitchingOption GpuDataManagerImpl::GetGpuSwitchingOption() const {
  return gpu_switching_;
}

base::ListValue* GpuDataManagerImpl::GetBlacklistReasons() const {
  ListValue* reasons = new ListValue();
  if (gpu_blacklist_.get())
    gpu_blacklist_->GetBlacklistReasons(reasons);
  return reasons;
}

bool GpuDataManagerImpl::GpuAccessAllowed() const {
  if (software_rendering_)
    return true;

  if (!gpu_info_.gpu_accessible)
    return false;

  if (card_blacklisted_)
    return false;

  // We only need to block GPU process if more features are disallowed other
  // than those in the preliminary gpu feature flags because the latter work
  // through renderer commandline switches.
  uint32 mask = ~(preliminary_blacklisted_features_);
  if ((blacklisted_features_ & mask) != 0)
    return false;

  if (blacklisted_features_ == GPU_FEATURE_TYPE_ALL) {
    if (gpu_blacklist_.get() && !gpu_blacklist_->needs_more_info())
      return false;
  }

  return true;
}

void GpuDataManagerImpl::HandleGpuSwitch() {
  if (complete_gpu_info_already_requested_) {
    complete_gpu_info_already_requested_ = false;
    gpu_info_.finalized = false;
    RequestCompleteGpuInfoIfNeeded();
  }
}

void GpuDataManagerImpl::AddObserver(GpuDataManagerObserver* observer) {
  observer_list_->AddObserver(observer);
}

void GpuDataManagerImpl::RemoveObserver(GpuDataManagerObserver* observer) {
  observer_list_->RemoveObserver(observer);
}

void GpuDataManagerImpl::SetWindowCount(uint32 count) {
  {
    base::AutoLock auto_lock(gpu_info_lock_);
    window_count_ = count;
  }
  GpuProcessHost::SendOnIO(
      GpuProcessHost::GPU_PROCESS_KIND_SANDBOXED,
      CAUSE_FOR_GPU_LAUNCH_NO_LAUNCH,
      new GpuMsg_SetVideoMemoryWindowCount(count));
}

uint32 GpuDataManagerImpl::GetWindowCount() const {
  base::AutoLock auto_lock(gpu_info_lock_);
  return window_count_;
}

void GpuDataManagerImpl::AppendRendererCommandLine(
    CommandLine* command_line) const {
  DCHECK(command_line);

  uint32 flags = GetBlacklistedFeatures();
  if ((flags & GPU_FEATURE_TYPE_WEBGL)) {
#if !defined(OS_ANDROID)
    if (!command_line->HasSwitch(switches::kDisableExperimentalWebGL))
      command_line->AppendSwitch(switches::kDisableExperimentalWebGL);
#endif
    if (!command_line->HasSwitch(switches::kDisablePepper3dForUntrustedUse))
      command_line->AppendSwitch(switches::kDisablePepper3dForUntrustedUse);
  }
  if ((flags & GPU_FEATURE_TYPE_MULTISAMPLING) &&
      !command_line->HasSwitch(switches::kDisableGLMultisampling))
    command_line->AppendSwitch(switches::kDisableGLMultisampling);
  if ((flags & GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING) &&
      !command_line->HasSwitch(switches::kDisableAcceleratedCompositing))
    command_line->AppendSwitch(switches::kDisableAcceleratedCompositing);
  if ((flags & GPU_FEATURE_TYPE_ACCELERATED_2D_CANVAS) &&
      !command_line->HasSwitch(switches::kDisableAccelerated2dCanvas))
    command_line->AppendSwitch(switches::kDisableAccelerated2dCanvas);
  if ((flags & GPU_FEATURE_TYPE_ACCELERATED_VIDEO_DECODE) &&
      !command_line->HasSwitch(switches::kDisableAcceleratedVideoDecode))
    command_line->AppendSwitch(switches::kDisableAcceleratedVideoDecode);
  if (ShouldUseSoftwareRendering())
    command_line->AppendSwitch(switches::kDisableFlashFullscreen3d);
}

void GpuDataManagerImpl::AppendGpuCommandLine(
    CommandLine* command_line) const {
  DCHECK(command_line);

  std::string use_gl =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(switches::kUseGL);
  FilePath swiftshader_path =
      CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          switches::kSwiftShaderPath);
  uint32 flags = GetBlacklistedFeatures();
  if ((flags & GPU_FEATURE_TYPE_MULTISAMPLING) &&
      !command_line->HasSwitch(switches::kDisableGLMultisampling))
    command_line->AppendSwitch(switches::kDisableGLMultisampling);
  if (flags & GPU_FEATURE_TYPE_TEXTURE_SHARING)
    command_line->AppendSwitch(switches::kDisableImageTransportSurface);

  if (software_rendering_) {
    command_line->AppendSwitchASCII(switches::kUseGL, "swiftshader");
    if (swiftshader_path.empty())
      swiftshader_path = swiftshader_path_;
  } else if ((flags & (GPU_FEATURE_TYPE_WEBGL |
                GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING |
                GPU_FEATURE_TYPE_ACCELERATED_2D_CANVAS)) &&
      (use_gl == "any")) {
    command_line->AppendSwitchASCII(
        switches::kUseGL, gfx::kGLImplementationOSMesaName);
  } else if (!use_gl.empty()) {
    command_line->AppendSwitchASCII(switches::kUseGL, use_gl);
  }
  if (ui::GpuSwitchingManager::GetInstance()->SupportsDualGpus()) {
    command_line->AppendSwitchASCII(switches::kSupportsDualGpus, "true");
    switch (gpu_switching_) {
      case GPU_SWITCHING_OPTION_FORCE_DISCRETE:
        command_line->AppendSwitchASCII(switches::kGpuSwitching,
            switches::kGpuSwitchingOptionNameForceDiscrete);
        break;
      case GPU_SWITCHING_OPTION_FORCE_INTEGRATED:
        command_line->AppendSwitchASCII(switches::kGpuSwitching,
            switches::kGpuSwitchingOptionNameForceIntegrated);
        break;
      case GPU_SWITCHING_OPTION_AUTOMATIC:
      case GPU_SWITCHING_OPTION_UNKNOWN:
        break;
    }
  } else {
    command_line->AppendSwitchASCII(switches::kSupportsDualGpus, "false");
  }

  if (!gpu_blacklist_.get() || !gpu_blacklist_->needs_more_info())
    command_line->AppendSwitch(switches::kSkipGpuFullInfoCollection);

  if (!swiftshader_path.empty())
    command_line->AppendSwitchPath(switches::kSwiftShaderPath,
                                   swiftshader_path);

  {
    base::AutoLock auto_lock(gpu_info_lock_);
    if (gpu_info_.optimus)
      command_line->AppendSwitch(switches::kReduceGpuSandbox);
    if (gpu_info_.amd_switchable) {
      // The image transport surface currently doesn't work with AMD Dynamic
      // Switchable graphics.
      command_line->AppendSwitch(switches::kReduceGpuSandbox);
      command_line->AppendSwitch(switches::kDisableImageTransportSurface);
    }
    // Pass GPU and driver information to GPU process. We try to avoid full GPU
    // info collection at GPU process startup, but we need gpu vendor_id,
    // device_id, driver_vendor, driver_version for deciding whether we need to
    // collect full info (on Linux) and for crash reporting purpose.
    command_line->AppendSwitchASCII(switches::kGpuVendorID,
        base::StringPrintf("0x%04x", gpu_info_.gpu.vendor_id));
    command_line->AppendSwitchASCII(switches::kGpuDeviceID,
        base::StringPrintf("0x%04x", gpu_info_.gpu.device_id));
    command_line->AppendSwitchASCII(switches::kGpuDriverVendor,
        gpu_info_.driver_vendor);
    command_line->AppendSwitchASCII(switches::kGpuDriverVersion,
        gpu_info_.driver_version);
  }
}

#if defined(OS_WIN)
bool GpuDataManagerImpl::IsUsingAcceleratedSurface() const {
  if (base::win::GetVersion() < base::win::VERSION_VISTA)
    return false;

  if (gpu_info_.amd_switchable)
    return false;
  if (software_rendering_)
    return false;
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kDisableImageTransportSurface))
    return false;
  uint32 flags = GetBlacklistedFeatures();
  if (flags & GPU_FEATURE_TYPE_TEXTURE_SHARING)
    return false;

  return true;
}
#endif

void GpuDataManagerImpl::AppendPluginCommandLine(
    CommandLine* command_line) const {
  DCHECK(command_line);

#if defined(OS_MACOSX)
  uint32 flags = GetBlacklistedFeatures();
  // TODO(jbauman): Add proper blacklist support for core animation plugins so
  // special-casing this video card won't be necessary. See
  // http://crbug.com/134015
  if ((flags & GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING) ||
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableAcceleratedCompositing)) {
    if (!command_line->HasSwitch(
           switches::kDisableCoreAnimationPlugins))
      command_line->AppendSwitch(
          switches::kDisableCoreAnimationPlugins);
  }
#endif
}

void GpuDataManagerImpl::UpdatePreliminaryBlacklistedFeatures() {
  preliminary_blacklisted_features_ = blacklisted_features_;
}

void GpuDataManagerImpl::NotifyGpuInfoUpdate() {
  observer_list_->Notify(&GpuDataManagerObserver::OnGpuInfoUpdate);
}

void GpuDataManagerImpl::UpdateVideoMemoryUsageStats(
    const GPUVideoMemoryUsageStats& video_memory_usage_stats) {
  observer_list_->Notify(&GpuDataManagerObserver::OnVideoMemoryUsageStatsUpdate,
                         video_memory_usage_stats);
}

void GpuDataManagerImpl::UpdateBlacklistedFeatures(
    GpuFeatureType features) {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  int flags = features;

  // Force disable using the GPU for these features, even if they would
  // otherwise be allowed.
  if (card_blacklisted_ ||
      command_line->HasSwitch(switches::kBlacklistAcceleratedCompositing)) {
    flags |= GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING;
  }
  if (card_blacklisted_ ||
      command_line->HasSwitch(switches::kBlacklistWebGL)) {
    flags |= GPU_FEATURE_TYPE_WEBGL;
  }
  blacklisted_features_ = static_cast<GpuFeatureType>(flags);

  EnableSoftwareRenderingIfNecessary();
}

void GpuDataManagerImpl::UpdateGpuSwitchingManager() {
  if (ui::GpuSwitchingManager::GetInstance()->SupportsDualGpus()) {
    switch (gpu_switching_) {
      case GPU_SWITCHING_OPTION_FORCE_DISCRETE:
        ui::GpuSwitchingManager::GetInstance()->ForceUseOfDiscreteGpu();
        break;
      case GPU_SWITCHING_OPTION_FORCE_INTEGRATED:
        ui::GpuSwitchingManager::GetInstance()->ForceUseOfIntegratedGpu();
        break;
      case GPU_SWITCHING_OPTION_AUTOMATIC:
      case GPU_SWITCHING_OPTION_UNKNOWN:
        break;
    }
  }
}

void GpuDataManagerImpl::RegisterSwiftShaderPath(const FilePath& path) {
  swiftshader_path_ = path;
  EnableSoftwareRenderingIfNecessary();
}

void GpuDataManagerImpl::EnableSoftwareRenderingIfNecessary() {
  if (!GpuAccessAllowed() ||
      (blacklisted_features_ & GPU_FEATURE_TYPE_WEBGL)) {
    if (!swiftshader_path_.empty() &&
        !CommandLine::ForCurrentProcess()->HasSwitch(
             switches::kDisableSoftwareRasterizer))
      software_rendering_ = true;
  }
}

bool GpuDataManagerImpl::ShouldUseSoftwareRendering() const {
  return software_rendering_;
}

void GpuDataManagerImpl::BlacklistCard() {
  card_blacklisted_ = true;

  blacklisted_features_ = GPU_FEATURE_TYPE_ALL;

  EnableSoftwareRenderingIfNecessary();
  NotifyGpuInfoUpdate();
}

}  // namespace content
