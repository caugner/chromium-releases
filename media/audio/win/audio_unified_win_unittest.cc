// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/test/test_timeouts.h"
#include "base/time.h"
#include "base/win/scoped_com_initializer.h"
#include "media/audio/audio_io.h"
#include "media/audio/audio_manager.h"
#include "media/audio/audio_util.h"
#include "media/audio/win/audio_unified_win.h"
#include "media/base/media_switches.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Between;
using ::testing::DoAll;
using ::testing::NotNull;
using ::testing::Return;
using base::win::ScopedCOMInitializer;

namespace media {

static const size_t kMaxDeltaSamples = 1000;
static const char* kDeltaTimeMsFileName = "unified_delta_times_ms.txt";

// Used to terminate a loop from a different thread than the loop belongs to.
// |loop| should be a MessageLoopProxy.
ACTION_P(QuitLoop, loop) {
  loop->PostTask(FROM_HERE, MessageLoop::QuitClosure());
}

class MockUnifiedSourceCallback
    : public AudioOutputStream::AudioSourceCallback {
 public:
  MOCK_METHOD2(OnMoreData, int(AudioBus* audio_bus,
                               AudioBuffersState buffers_state));
  MOCK_METHOD3(OnMoreIOData, int(AudioBus* source,
                                 AudioBus* dest,
                                 AudioBuffersState buffers_state));
  MOCK_METHOD2(OnError, void(AudioOutputStream* stream, int code));
};

// AudioOutputStream::AudioSourceCallback implementation which enables audio
// play-through. It also creates a text file that contains times between two
// successive callbacks. Units are in milliseconds. This file can be used for
// off-line analysis of the callback sequence.
class UnifiedSourceCallback : public AudioOutputStream::AudioSourceCallback {
 public:
  explicit UnifiedSourceCallback()
      : previous_call_time_(base::Time::Now()),
        text_file_(NULL),
        elements_to_write_(0) {
    delta_times_.reset(new int[kMaxDeltaSamples]);
  }

  virtual ~UnifiedSourceCallback() {
    FilePath file_name;
    EXPECT_TRUE(PathService::Get(base::DIR_EXE, &file_name));
    file_name = file_name.AppendASCII(kDeltaTimeMsFileName);

    EXPECT_TRUE(!text_file_);
    text_file_ = file_util::OpenFile(file_name, "wt");
    DLOG_IF(ERROR, !text_file_) << "Failed to open log file.";
    LOG(INFO) << ">> Output file " << file_name.value() << " has been created.";

    // Write the array which contains delta times to a text file.
    size_t elements_written = 0;
    while (elements_written < elements_to_write_) {
      fprintf(text_file_, "%d\n", delta_times_[elements_written]);
      ++elements_written;
    }
    file_util::CloseFile(text_file_);
  }

  virtual int OnMoreData(AudioBus* dest,
                         AudioBuffersState buffers_state) {
    NOTREACHED();
    return 0;
  };

  virtual int OnMoreIOData(AudioBus* source,
                           AudioBus* dest,
                           AudioBuffersState buffers_state) {
    // Store time between this callback and the previous callback.
    int diff = (base::Time::Now() - previous_call_time_).InMilliseconds();
    previous_call_time_ = base::Time::Now();
    if (elements_to_write_ < kMaxDeltaSamples) {
      delta_times_[elements_to_write_] = diff;
      ++elements_to_write_;
    }

    // Play out the recorded audio samples in loop back.
    source->CopyTo(dest);
    return source->frames();
  };

  virtual void OnError(AudioOutputStream* stream, int code) {
    NOTREACHED();
  }

 private:
  base::Time previous_call_time_;
  scoped_array<int> delta_times_;
  FILE* text_file_;
  size_t elements_to_write_;
};

// Convenience method which ensures that we fulfill all required conditions
// to run unified audio tests on Windows.
static bool CanRunUnifiedAudioTests(AudioManager* audio_man) {
  const CommandLine* cmd_line = CommandLine::ForCurrentProcess();
  if (!cmd_line->HasSwitch(switches::kEnableWebAudioInput)) {
    DVLOG(1) << "--enable-webaudio-input must be defined to run this test.";
    return false;
  }

  if (!media::IsWASAPISupported()) {
    LOG(WARNING) << "This tests requires Windows Vista or higher.";
    return false;
  }

  if (!audio_man->HasAudioOutputDevices()) {
    LOG(WARNING) << "No output devices detected.";
    return false;
  }

  if (!audio_man->HasAudioInputDevices()) {
    LOG(WARNING) << "No input devices detected.";
    return false;
  }

  if (!WASAPIUnifiedStream::HasUnifiedDefaultIO()) {
    LOG(WARNING) << "Audio IO is not supported.";
    return false;
  }

  return true;
}

// Convenience class which simplifies creation of a unified AudioOutputStream
// object.
class AudioUnifiedStreamWrapper {
 public:
  explicit AudioUnifiedStreamWrapper(AudioManager* audio_manager)
      : com_init_(ScopedCOMInitializer::kMTA),
        audio_man_(audio_manager),
        format_(AudioParameters::AUDIO_PCM_LOW_LATENCY),
        channel_layout_(CHANNEL_LAYOUT_STEREO),
        bits_per_sample_(16) {
    sample_rate_ = media::GetAudioHardwareSampleRate();
    samples_per_packet_ = media::GetAudioHardwareBufferSize();
  }

  ~AudioUnifiedStreamWrapper() {}

  // Creates AudioOutputStream object using default parameters.
  WASAPIUnifiedStream* Create() {
    return static_cast<WASAPIUnifiedStream*> (CreateOutputStream());
  }

  AudioParameters::Format format() const { return format_; }
  int channels() const { return ChannelLayoutToChannelCount(channel_layout_); }
  int bits_per_sample() const { return bits_per_sample_; }
  int sample_rate() const { return sample_rate_; }
  int samples_per_packet() const { return samples_per_packet_; }

 private:
  AudioOutputStream* CreateOutputStream() {
    AudioOutputStream* aos = audio_man_->MakeAudioOutputStream(
        AudioParameters(format_, channel_layout_, sample_rate_,
                        bits_per_sample_, samples_per_packet_));
    EXPECT_TRUE(aos);
    return aos;
  }

  ScopedCOMInitializer com_init_;
  AudioManager* audio_man_;

  AudioParameters::Format format_;
  ChannelLayout channel_layout_;
  int bits_per_sample_;
  int sample_rate_;
  int samples_per_packet_;
};

// Convenience method which creates a default WASAPIUnifiedStream object.
static WASAPIUnifiedStream* CreateDefaultUnifiedStream(
    AudioManager* audio_manager) {
  AudioUnifiedStreamWrapper aosw(audio_manager);
  return aosw.Create();
}

// Test Open(), Close() calling sequence.
TEST(WASAPIUnifiedStreamTest, OpenAndClose) {
  scoped_ptr<AudioManager> audio_manager(AudioManager::Create());
  if (!CanRunUnifiedAudioTests(audio_manager.get()))
    return;

  WASAPIUnifiedStream* wus = CreateDefaultUnifiedStream(audio_manager.get());
  EXPECT_TRUE(wus->Open());
  wus->Close();
}

// Test Open(), Start(), Close() calling sequence.
TEST(WASAPIUnifiedStreamTest, OpenStartAndClose) {
  scoped_ptr<AudioManager> audio_manager(AudioManager::Create());
  if (!CanRunUnifiedAudioTests(audio_manager.get()))
    return;

  MockUnifiedSourceCallback source;
  AudioUnifiedStreamWrapper ausw(audio_manager.get());
  WASAPIUnifiedStream* wus = ausw.Create();

  EXPECT_TRUE(wus->Open());
  EXPECT_CALL(source, OnError(wus, _))
      .Times(0);
  EXPECT_CALL(source, OnMoreIOData(NotNull(), NotNull(), _))
      .Times(Between(0, 1))
      .WillOnce(Return(ausw.samples_per_packet()));
  wus->Start(&source);
  wus->Close();
}

// Verify that IO callbacks starts as they should.
TEST(WASAPIUnifiedStreamTest, StartLoopbackAudio) {
  scoped_ptr<AudioManager> audio_manager(AudioManager::Create());
  if (!CanRunUnifiedAudioTests(audio_manager.get()))
    return;

  MessageLoopForUI loop;
  MockUnifiedSourceCallback source;
  AudioUnifiedStreamWrapper ausw(audio_manager.get());
  WASAPIUnifiedStream* wus = ausw.Create();

  EXPECT_TRUE(wus->Open());
  EXPECT_CALL(source, OnError(wus, _))
      .Times(0);
  EXPECT_CALL(source, OnMoreIOData(NotNull(), NotNull(), _))
      .Times(AtLeast(2))
      .WillOnce(Return(ausw.samples_per_packet()))
      .WillOnce(DoAll(
          QuitLoop(loop.message_loop_proxy()),
          Return(ausw.samples_per_packet())));
  wus->Start(&source);
  loop.PostDelayedTask(FROM_HERE, MessageLoop::QuitClosure(),
                       TestTimeouts::action_timeout());
  loop.Run();
  wus->Stop();
  wus->Close();
}

// Perform a real-time test in loopback where the recorded audio is echoed
// back to the speaker. This test allows the user to verify that the audio
// sounds OK. A text file with name |kDeltaTimeMsFileName| is also generated.
TEST(WASAPIUnifiedStreamTest, DISABLED_RealTimePlayThrough) {
  scoped_ptr<AudioManager> audio_manager(AudioManager::Create());
  if (!CanRunUnifiedAudioTests(audio_manager.get()))
    return;

  MessageLoopForUI loop;
  UnifiedSourceCallback source;
  WASAPIUnifiedStream* wus = CreateDefaultUnifiedStream(audio_manager.get());

  EXPECT_TRUE(wus->Open());
  wus->Start(&source);
  loop.PostDelayedTask(FROM_HERE, MessageLoop::QuitClosure(),
                       base::TimeDelta::FromMilliseconds(10000));
  loop.Run();
  wus->Close();
}

}  // namespace media
