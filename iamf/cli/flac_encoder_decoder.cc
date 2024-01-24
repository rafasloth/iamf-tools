/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * www.aomedia.org/license/patent.
 */
#include "iamf/cli/flac_encoder_decoder.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "iamf/cli/audio_frame_with_data.h"
#include "iamf/cli/encoder_base.h"
#include "iamf/cli/proto/codec_config.pb.h"
#include "iamf/flac_decoder_config.h"
#include "iamf/ia.h"
#include "iamf/obu_util.h"
#include "include/FLAC/format.h"
#include "include/FLAC/ordinals.h"
#include "include/FLAC/stream_encoder.h"

namespace iamf_tools {

namespace {

absl::Status Configure(
    const iamf_tools_cli_proto::FlacEncoderMetadata& encoder_metadata,
    const FlacDecoderConfig& decoder_config, int num_channels,
    uint32_t num_samples_per_frame, uint32_t output_sample_rate,
    uint8_t input_pcm_bit_depth_, FLAC__StreamEncoder* const encoder) {
  FLAC__bool ok = true;
  // Configure values based on the associated Codec Config OBU.

  ok &= FLAC__stream_encoder_set_channels(encoder, num_channels);

  ok &= FLAC__stream_encoder_set_bits_per_sample(
      encoder, static_cast<uint32_t>(input_pcm_bit_depth_));

  ok &= FLAC__stream_encoder_set_sample_rate(encoder, output_sample_rate);

  // IAMF requires a constant block size.
  ok &= FLAC__stream_encoder_set_blocksize(encoder, num_samples_per_frame);

  uint64_t total_samples_in_stream;
  RETURN_IF_NOT_OK(
      decoder_config.GetTotalSamplesInStream(total_samples_in_stream));

  ok &= FLAC__stream_encoder_set_total_samples_estimate(
      encoder, total_samples_in_stream);

  // Set arguments configured by the user-provided `encoder_metadata_`.
  ok &= FLAC__stream_encoder_set_compression_level(
      encoder, encoder_metadata.compression_level());

  ok &= FLAC__stream_encoder_set_verify(encoder, true);

  if (!ok) {
    LOG(ERROR) << "Failed to configure Flac encoder.";
    return absl::UnknownError("");
  }

  return absl::OkStatus();
}
}  // namespace

FLAC__StreamEncoderWriteStatus LibFlacWriteCallback(
    const FLAC__StreamEncoder* /*encoder*/, const FLAC__byte buffer[],
    size_t bytes, unsigned int samples, unsigned int current_frame,
    void* client_data) {
  const unsigned int kLibFlacMetadataSentinel = 0;
  if (samples == kLibFlacMetadataSentinel) {
    // `libflac` uses a value of `0` to indicate this callback is for metadata.
    LOG(INFO) << "`iamf_tools` currently ignores all additional FLAC metadata.";
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
  }

  auto flac_encoder = static_cast<FlacEncoder*>(client_data);

  auto flac_frame_iter =
      flac_encoder->frame_index_to_frame_.find(current_frame);
  if (flac_frame_iter == flac_encoder->frame_index_to_frame_.end()) {
    LOG(ERROR) << "Failed to find a frame with index " << current_frame
               << " in Flac encoder. Data may be lost or corrupted.";
    return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
  }

  // Append to `audio_frame_` and track how many samples it represents. It will
  // be finalized later to ensure frames are finalized in chronological order.
  FlacFrame& flac_frame = flac_frame_iter->second;
  flac_frame.audio_frame_with_data->obu.audio_frame_.insert(
      flac_frame.audio_frame_with_data->obu.audio_frame_.end(), buffer,
      buffer + bytes);
  flac_frame.num_samples += samples;

  return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

void LibFlacMetadataCallback(const FLAC__StreamEncoder* /*encoder*/,
                             const FLAC__StreamMetadata* metadata,
                             void* client_data) {
  LOG_FIRST_N(INFO, 1) << "Begin `flac_metadata_callback`.";

  if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
    LOG(INFO) << "Received `STREAMINFO` metadata.";
    // Just validate we got the `STREAMINFO` metadata at some point. IAMF
    // requires some fields to be set constant and different from what will be
    // returned by `libflac`.
    auto flac_encoder = static_cast<FlacEncoder*>(client_data);

    flac_encoder->streaminfo_finished_ = true;
  }
}

absl::Status FlacEncoder::InitializeEncoder() {
  // Initialize the encoder.
  encoder_ = FLAC__stream_encoder_new();
  if (encoder_ == nullptr) {
    LOG(ERROR) << "Failed to initialize Flac encoder.";
    return absl::UnknownError("");
  }

  // Configure the FLAC encoder based on user input data.
  RETURN_IF_NOT_OK(Configure(encoder_metadata_, decoder_config_, num_channels_,
                             num_samples_per_frame_, output_sample_rate_,
                             input_pcm_bit_depth_, encoder_));

  // Initialize the FLAC encoder.
  FLAC__StreamEncoderInitStatus init_status = FLAC__stream_encoder_init_stream(
      encoder_, LibFlacWriteCallback, /*seek_callback=*/nullptr,
      /*tell_callback=*/nullptr, LibFlacMetadataCallback,
      static_cast<void*>(this));

  if (init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
    LOG(ERROR) << "Failed to initialize Flac stream: " << init_status;
    return absl::UnknownError("");
  }

  return absl::OkStatus();
}

absl::Status FlacEncoder::FinalizeFrames() {
  // Process the frames so they are finalized in chronological order.
  unsigned int next_frame_index = 0;

  // Process frames in chronological order. Sleep until frames are ready.
  while (!frame_index_to_frame_.empty()) {
    auto next_frame_to_finalize_iter =
        frame_index_to_frame_.find(next_frame_index);
    if (next_frame_to_finalize_iter == frame_index_to_frame_.end()) {
      return absl::UnknownError("");
    }

    auto& next_frame_to_finalize_ = next_frame_to_finalize_iter->second;
    if (next_frame_to_finalize_.num_samples != num_samples_per_frame_) {
      // The next frame is not ready to be finalized.
      // Sleep and try again.
      absl::SleepFor(absl::Milliseconds(50));
      continue;
    }

    // Finalize the audio frame.
    finalized_audio_frames_.emplace_back(
        std::move(*next_frame_to_finalize_.audio_frame_with_data));

    // The frame is fully processed and no longer needed.
    frame_index_to_frame_.erase(next_frame_to_finalize_iter);
    next_frame_index++;
  }

  return absl::OkStatus();
}

absl::Status FlacEncoder::FinalizeAndFlush(
    std::list<AudioFrameWithData>& audio_frames) {
  // Signal to `libflac` the encoder is finished.
  if (!FLAC__stream_encoder_finish(encoder_)) {
    LOG(ERROR) << "Failed to finalize Flac encoder.";
    return absl::UnknownError("");
  }

  while (!streaminfo_finished_) {
    // Finalizing the encoder causes the `STREAMINFO` metadata to be generated.
    // Wait until the `flac_metadata_callback` is called with that type of
    // block.
    absl::SleepFor(absl::Milliseconds(50));
  }

  // Flush all finished frames.
  RETURN_IF_NOT_OK(FinalizeFrames());

  return EncoderBase::FinalizeAndFlush(audio_frames);
}

FlacEncoder::~FlacEncoder() {
  FLAC__stream_encoder_delete(encoder_);

  if (!frame_index_to_frame_.empty()) {
    LOG(ERROR) << "Some frames were not fully processed. Maybe `Finalize()` "
                  "was not called.";
  }
}

absl::Status FlacEncoder::EncodeAudioFrame(
    int input_bit_depth, const std::vector<std::vector<int32_t>>& samples,
    std::unique_ptr<AudioFrameWithData> partial_audio_frame_with_data) {
  RETURN_IF_NOT_OK(ValidateInputSamples(samples));
  const int num_samples_per_channel = static_cast<int>(num_samples_per_frame_);

  LOG_FIRST_N(INFO, 1) << "num_samples_per_channel: "
                       << num_samples_per_channel;
  LOG_FIRST_N(INFO, 1) << "num_channels: " << num_channels_;

  // FLAC requires a right-justified sign extended value. Calculate what the
  // mask is to sign extend a `input_bit_depth`-bit value.
  uint32_t base_sign_extension_mask = 0;
  for (int i = 31; i > input_bit_depth - 1; --i) {
    base_sign_extension_mask |= 1 << i;
  }

  // Convert input to the array that will be passed to `flac_encode`.
  std::vector<FLAC__int32> encoder_input_pcm(
      num_samples_per_channel * num_channels_, 0);
  int write_position = 0;
  for (int t = 0; t < samples.size(); t++) {
    for (int c = 0; c < samples[0].size(); ++c) {
      // Only apply the sign extension mask when the left-justified value has
      // '1' in the MSB.
      const uint32_t sign_extension_mask =
          (samples[t][c] & 0x80000000) ? base_sign_extension_mask : 0;
      // Shift the input value to be right-justified.
      const uint32_t sample_right_justified =
          static_cast<uint32_t>(samples[t][c]) >> (32 - input_bit_depth) |
          sign_extension_mask;
      RETURN_IF_NOT_OK(
          WritePcmSample(sample_right_justified, 32,
                         /*big_endian=*/false,
                         reinterpret_cast<uint8_t*>(encoder_input_pcm.data()),
                         write_position));
    }
  }

  LOG_FIRST_N(INFO, 1) << "Encoding " << encoder_input_pcm.size() * 4
                       << " bytes representing " << num_samples_per_channel
                       << " x " << num_channels_ << " samples.";

  if (!FLAC__stream_encoder_process_interleaved(
          encoder_, encoder_input_pcm.data(), num_samples_per_channel)) {
    LOG(ERROR) << "Flac failed to encode.";
    return absl::UnknownError("");
  }

  // Transfer ownership of the partial audio frame so it can be finalized later.
  frame_index_to_frame_[next_frame_index_++].audio_frame_with_data =
      std::move(partial_audio_frame_with_data);
  return absl::OkStatus();
}

}  // namespace iamf_tools
