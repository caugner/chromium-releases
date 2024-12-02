// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/source_buffer_stream.h"

#include "base/logging.h"
#include "media/base/data_buffer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

static const int kDefaultFramesPerSecond = 30;
static const int kDefaultKeyframesPerSecond = 6;

class SourceBufferStreamTest : public testing::Test {
 protected:
  SourceBufferStreamTest() {
    SetStreamInfo(kDefaultFramesPerSecond, kDefaultKeyframesPerSecond);
  }

  void SetStreamInfo(int frames_per_second, int keyframes_per_second) {
    frames_per_second_ = frames_per_second;
    keyframes_per_second_ = keyframes_per_second;
    frame_duration_ = ConvertToFrameDuration(frames_per_second);
  }

  void AppendBuffers(int starting_position, int number_of_buffers) {
    AppendBuffers(starting_position, number_of_buffers, true);
  }

  void AppendBuffers_ExpectFailure(
      int starting_position, int number_of_buffers) {
    AppendBuffers(starting_position, number_of_buffers, false);
  }

  void Seek(int position) {
    stream_.Seek(position * frame_duration_);
  }

  SourceBufferStream::Timespan CreateTimespan(
      int start_position, int end_position) {
    return std::make_pair(
        start_position * frame_duration_, (end_position + 1) * frame_duration_);
  }

  void CheckExpectedTimespans(
      SourceBufferStream::TimespanList expected_times) {
    SourceBufferStream::TimespanList actual_times = stream_.GetBufferedTime();
    EXPECT_EQ(expected_times.size(), actual_times.size());

    for (SourceBufferStream::TimespanList::iterator actual_itr =
         actual_times.begin(), expected_itr = expected_times.begin();
         actual_itr != actual_times.end() &&
         expected_itr != expected_times.end();
         actual_itr++, expected_itr++) {
      EXPECT_EQ(actual_itr->first / frame_duration_,
                expected_itr->first / frame_duration_);
      EXPECT_EQ(actual_itr->second / frame_duration_,
                expected_itr->second / frame_duration_);
    }
  }

  void CheckExpectedBuffers(
      int starting_position, int ending_position) {
    CheckExpectedBuffers(starting_position, ending_position, false);
  }

  void CheckExpectedBuffers(
      int starting_position, int ending_position, bool expect_keyframe) {
    int current_position = starting_position;
    for (; current_position <= ending_position; current_position++) {
      scoped_refptr<StreamParserBuffer> buffer;
      if (!stream_.GetNextBuffer(&buffer))
        break;

      if (expect_keyframe && current_position == starting_position)
        EXPECT_TRUE(buffer->IsKeyframe());

      EXPECT_EQ(buffer->GetTimestamp() / frame_duration_, current_position);
    }

    EXPECT_EQ(current_position, ending_position + 1);
  }

  base::TimeDelta frame_duration() const { return frame_duration_; }

  SourceBufferStream stream_;

 private:
  base::TimeDelta ConvertToFrameDuration(int frames_per_second) {
    return base::TimeDelta::FromMicroseconds(
        base::Time::kMicrosecondsPerSecond / frames_per_second);
  }

  void AppendBuffers(int starting_position, int number_of_buffers,
                     bool expect_success) {
    int keyframe_interval = frames_per_second_ / keyframes_per_second_;

    SourceBufferStream::BufferQueue queue;
    for (int i = 0; i < number_of_buffers; i++) {
      int position = starting_position + i;
      bool is_keyframe = position % keyframe_interval == 0;
      scoped_refptr<StreamParserBuffer> buffer =
          StreamParserBuffer::CopyFrom(NULL, 0, is_keyframe);
      buffer->SetDuration(frame_duration_);
      buffer->SetTimestamp(frame_duration_ * position);
      queue.push_back(buffer);
    }
    EXPECT_EQ(stream_.Append(queue), expect_success);
  }

  int frames_per_second_;
  int keyframes_per_second_;
  base::TimeDelta frame_duration_;
  DISALLOW_COPY_AND_ASSIGN(SourceBufferStreamTest);
};

TEST_F(SourceBufferStreamTest, Append_SingleRange) {
  // Append 15 buffers at positions 0 through 14.
  AppendBuffers(0, 15);

  // Check expected range.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(0, 14));
  CheckExpectedTimespans(expected);
  // Check buffers in range.
  Seek(0);
  CheckExpectedBuffers(0, 14);
}

TEST_F(SourceBufferStreamTest, Append_SingleRange_OneBufferAtATime) {
  // Append 15 buffers starting at position 0, one buffer at a time.
  for (int i = 0; i < 15; i++)
    AppendBuffers(i, 1);

  // Check expected range.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(0, 14));
  CheckExpectedTimespans(expected);
  // Check buffers in range.
  Seek(0);
  CheckExpectedBuffers(0, 14);
}

TEST_F(SourceBufferStreamTest, Append_DisjointRanges) {
  // Append 5 buffers at positions 0 through 4.
  AppendBuffers(0, 5);

  // Append 10 buffers at positions 15 through 24.
  AppendBuffers(15, 10);

  // Check expected ranges.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(0, 4));
  expected.push_back(CreateTimespan(15, 24));
  CheckExpectedTimespans(expected);
  // Check buffers in ranges.
  Seek(0);
  CheckExpectedBuffers(0, 4);
  Seek(15);
  CheckExpectedBuffers(15, 24);
}

TEST_F(SourceBufferStreamTest, Append_AdjacentRanges) {
  // Append 12 buffers at positions 0 through 11.
  AppendBuffers(0, 12);

  // Append 11 buffers at positions 15 through 25.
  AppendBuffers(15, 11);

  // Append 3 buffers at positions 12 through 14 to bridge the gap.
  AppendBuffers(12, 3);

  // Check expected range.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(0, 25));
  CheckExpectedTimespans(expected);
  // Check buffers in range.
  Seek(0);
  CheckExpectedBuffers(0, 25);
}

TEST_F(SourceBufferStreamTest, Append_DoesNotBeginWithKeyframe) {
  // Append fails because the range doesn't begin with a keyframe.
  AppendBuffers_ExpectFailure(3, 5);

  // Append 10 buffers at positions 5 through 14.
  AppendBuffers(5, 10);

  // Check expected range.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(5, 14));
  CheckExpectedTimespans(expected);
  // Check buffers in range.
  Seek(5);
  CheckExpectedBuffers(5, 14);

  // Append fails because the range doesn't begin with a keyframe.
  AppendBuffers_ExpectFailure(17, 10);
  CheckExpectedTimespans(expected);
  Seek(5);
  CheckExpectedBuffers(5, 14);
}

TEST_F(SourceBufferStreamTest, Overlap_Complete) {
  // Append 5 buffers at positions 5 through 9.
  AppendBuffers(5, 5);

  // Append 15 buffers at positions 0 through 14.
  AppendBuffers(0, 15);

  // Check expected range.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(0, 14));
  CheckExpectedTimespans(expected);
  // Check buffers in range.
  Seek(0);
  CheckExpectedBuffers(0, 14);
}

TEST_F(SourceBufferStreamTest, Overlap_Complete_EdgeCase) {
  // Make each frame a keyframe so that it's okay to overlap frames at any point
  // (instead of needing to respect keyframe boundaries).
  SetStreamInfo(30, 30);

  // Append 6 buffers at positions 6 through 11.
  AppendBuffers(6, 6);

  // Append 8 buffers at positions 5 through 12.
  AppendBuffers(5, 8);

  // Check expected range.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(5, 12));
  CheckExpectedTimespans(expected);
  // Check buffers in range.
  Seek(5);
  CheckExpectedBuffers(5, 12);
}

TEST_F(SourceBufferStreamTest, Overlap_Start) {
  // Append 5 buffers at positions 5 through 9.
  AppendBuffers(5, 5);

  // Append 6 buffers at positions 8 through 13.
  AppendBuffers(8, 6);

  // Check expected range.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(5, 13));
  CheckExpectedTimespans(expected);
  // Check buffers in range.
  Seek(5);
  CheckExpectedBuffers(5, 13);
}

TEST_F(SourceBufferStreamTest, Overlap_End) {
  // Append 6 buffers at positions 10 through 15.
  AppendBuffers(10, 6);

  // Append 8 buffers at positions 5 through 12.
  AppendBuffers(5, 8);

  // Check expected range.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(5, 12));
  CheckExpectedTimespans(expected);
  // Check buffers in range.
  Seek(5);
  CheckExpectedBuffers(5, 12);
}

TEST_F(SourceBufferStreamTest, Overlap_Several) {
  // Append 2 buffers at positions 5 through 6.
  AppendBuffers(5, 2);

  // Append 2 buffers at positions 10 through 11.
  AppendBuffers(10, 2);

  // Append 2 buffers at positions 15 through 16.
  AppendBuffers(15, 2);

  // Check expected ranges.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(5, 6));
  expected.push_back(CreateTimespan(10, 11));
  expected.push_back(CreateTimespan(15, 16));
  CheckExpectedTimespans(expected);

  // Append buffers at positions 0 through 19.
  AppendBuffers(0, 20);

  // Check expected range.
  expected.clear();
  expected.push_back(CreateTimespan(0, 19));
  CheckExpectedTimespans(expected);
  // Check buffers in range.
  Seek(0);
  CheckExpectedBuffers(0, 19);
}

TEST_F(SourceBufferStreamTest, Overlap_SeveralThenMerge) {
  // Append 2 buffers at positions 5 through 6.
  AppendBuffers(5, 2);

  // Append 2 buffers at positions 10 through 11.
  AppendBuffers(10, 2);

  // Append 2 buffers at positions 15 through 16.
  AppendBuffers(15, 2);

  // Append 2 buffers at positions 20 through 21.
  AppendBuffers(20, 2);

  // Append buffers at positions 0 through 19.
  AppendBuffers(0, 20);

  // Check expected ranges.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(0, 21));
  CheckExpectedTimespans(expected);
  // Check buffers in range.
  Seek(0);
  CheckExpectedBuffers(0, 21);
}

TEST_F(SourceBufferStreamTest, Seek_Keyframe) {
  // Append 6 buffers at positions 0 through 5.
  AppendBuffers(0, 6);

  // Seek to beginning.
  Seek(0);
  CheckExpectedBuffers(0, 5, true);
}

TEST_F(SourceBufferStreamTest, Seek_NonKeyframe) {
  // Append 15 buffers at positions 0 through 14.
  AppendBuffers(0, 15);

  // Seek to buffer at position 13.
  Seek(13);

  // Expect seeking back to the nearest keyframe.
  CheckExpectedBuffers(10, 14, true);

  // Seek to buffer at position 3.
  Seek(3);

  // Expect seeking back to the nearest keyframe.
  CheckExpectedBuffers(0, 3, true);
}

TEST_F(SourceBufferStreamTest, Seek_NotBuffered) {
  // Seek to beginning.
  Seek(0);

  // Try to get buffer; nothing's appended.
  scoped_refptr<StreamParserBuffer> buffer;
  EXPECT_FALSE(stream_.GetNextBuffer(&buffer));

  // Append 2 buffers at positions 0.
  AppendBuffers(0, 2);
  Seek(0);
  CheckExpectedBuffers(0, 1);

  // Try to get buffer out of range.
  Seek(2);
  EXPECT_FALSE(stream_.GetNextBuffer(&buffer));
}

TEST_F(SourceBufferStreamTest, Seek_InBetweenTimestamps) {
  // Append 10 buffers at positions 0 through 9.
  AppendBuffers(0, 10);

  base::TimeDelta bump = frame_duration() / 4;
  CHECK(bump > base::TimeDelta());

  // Seek to buffer a little after position 5.
  stream_.Seek(5 * frame_duration() + bump);
  CheckExpectedBuffers(5, 5, true);

  // Seek to buffer a little before position 5.
  stream_.Seek(5 * frame_duration() - bump);
  CheckExpectedBuffers(0, 0, true);
}

// TODO(vrk): When overlaps are handled more elegantly, this test should be
// rewritten to test for more meaningful outcomes. Right now we are just
// testing to make sure nothing crazy happens in this scenario (like losing
// the seek position or garbage collecting the data at position 13).
// Bug for overlaps is crbug.com/125072.
TEST_F(SourceBufferStreamTest, GetNextBuffer_AfterOverlap) {
  // Append 15 buffers at positions 0 through 14.
  AppendBuffers(0, 15);

  // Seek to buffer at position 13.
  Seek(13);

  // Append 5 buffers at positions 10 through 14.
  // The current implementation expects a failure, though fixing
  // crbug.com/125072 should change this expectation.
  AppendBuffers_ExpectFailure(10, 5);

  // Make sure we can still get the buffer at 13.
  CheckExpectedBuffers(10, 13);
}

TEST_F(SourceBufferStreamTest, GetNextBuffer_AfterMerges) {
  // Append 5 buffers at positions 10 through 14.
  AppendBuffers(10, 5);

  // Seek to buffer at position 12.
  Seek(12);

  // Append 5 buffers at positions 5 through 9.
  AppendBuffers(5, 5);

  // Make sure ranges are merged.
  SourceBufferStream::TimespanList expected;
  expected.push_back(CreateTimespan(5, 14));
  CheckExpectedTimespans(expected);

  // Make sure the next buffer is correct.
  CheckExpectedBuffers(10, 10);

  // Append 5 buffers at positions 15 through 19.
  AppendBuffers(15, 5);
  expected.clear();
  expected.push_back(CreateTimespan(5, 19));
  CheckExpectedTimespans(expected);

  // Make sure the remaining next buffers are correct.
  CheckExpectedBuffers(11, 14);
}

TEST_F(SourceBufferStreamTest, GetNextBuffer_ExhaustThenAppend) {
  // Append 4 buffers at positions 0 through 3.
  AppendBuffers(0, 4);

  // Seek to buffer at position 0 and get all buffers.
  Seek(0);
  CheckExpectedBuffers(0, 3);

  // Next buffer is at position 4, so should not be able to fulfill request.
  scoped_refptr<StreamParserBuffer> buffer;
  EXPECT_FALSE(stream_.GetNextBuffer(&buffer));

  // Append 2 buffers at positions 4 through 5.
  AppendBuffers(4, 2);
  CheckExpectedBuffers(4, 5);
}

}  // namespace media
