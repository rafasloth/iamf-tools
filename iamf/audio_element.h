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
#ifndef AUDIO_ELEMENT_H_
#define AUDIO_ELEMENT_H_

#include <cstdint>
#include <limits>
#include <memory>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "iamf/ia.h"
#include "iamf/obu_base.h"
#include "iamf/obu_header.h"
#include "iamf/param_definitions.h"
#include "iamf/write_bit_buffer.h"

namespace iamf_tools {

/*!\brief One of the parameters associated with an Audio Element OBU. */
struct AudioElementParam {
  friend bool operator==(const AudioElementParam& lhs,
                         const AudioElementParam& rhs) {
    if (lhs.param_definition_type != rhs.param_definition_type) {
      return false;
    }
    // Compare the underlying `ParamDefinition` data by value.
    return *lhs.param_definition == *rhs.param_definition;
  }

  // Serialized to a ULEB128.
  ParamDefinition::ParameterDefinitionType param_definition_type;

  // Actual sub-class stored depends on `param_definition_type`.
  std::unique_ptr<ParamDefinition> param_definition;
};

/*!\brief An element of the `ScalableChannelLayoutConfig` vector. */
struct ChannelAudioLayerConfig {
  /*!\brief A 4-bit enum for the type of layout. */
  enum LoudspeakerLayout : uint8_t {
    kLayoutMono = 0,      // C.
    kLayoutStereo = 1,    // L/R
    kLayout5_1_ch = 2,    // L/C/R/Ls/Rs/LFE.
    kLayout5_1_2_ch = 3,  // L/C/R/Ls/Rs/Ltf/Rtf/LFE.
    kLayout5_1_4_ch = 4,  // L/C/R/Ls/Rs/Ltf/Rtf/Ltr/Rtr/LFE.
    kLayout7_1_ch = 5,    // L/C/R/Lss/Rss/Lrs/Rrs/LFE.
    kLayout7_1_2_ch = 6,  // L/C/R/Lss/Rss/Lrs/Rrs/Ltf/Rtf/LFE.
    kLayout7_1_4_ch = 7,  // L/C/R/Lss/Rss/Lrs/Rrs/Ltf/Rtf/Ltb/Rtb/LFE.
    kLayout3_1_2_ch = 8,  // L/C/R//Ltf/Rtf/LFE.
    kLayoutBinaural = 9,  // L/R.
    kLayoutReservedBegin = 10,
    kLayoutReservedEnd = 15,
  };

  friend bool operator==(const ChannelAudioLayerConfig& lhs,
                         const ChannelAudioLayerConfig& rhs) = default;

  LoudspeakerLayout loudspeaker_layout;  // 4 bits.
  uint8_t output_gain_is_present_flag;   // 1 bit.
  uint8_t recon_gain_is_present_flag;    // 1 bit.
  uint8_t reserved_a;                    // 2 bits.
  uint8_t substream_count;
  uint8_t coupled_substream_count;

  // if (output_gain_is_present_flag(i) == 1) {
  uint8_t output_gain_flag = 0;  // 6 bits.
  uint8_t reserved_b = 0;        // 2 bits.
  int16_t output_gain = 0;
  // }
};

/*!\brief Config to reconstruct an Audio Element OBU using a channel layout.
 *
 * The metadata required for combining the substreams identified here in order
 * to reconstruct a scalable channel layout.
 */
struct ScalableChannelLayoutConfig {
  friend bool operator==(const ScalableChannelLayoutConfig& lhs,
                         const ScalableChannelLayoutConfig& rhs) = default;

  uint8_t num_layers;  // 3 bits.
  uint8_t reserved;    // 5 bits.

  // Vector of length `num_layers`.
  std::vector<ChannelAudioLayerConfig> channel_audio_layer_configs;
};

/*!\brief Configuration for mono-coded Ambisonics. */
struct AmbisonicsMonoConfig {
  // RFC 8486 reserves 255 to signal an inactive ACN (ambisonics channel
  // number).
  static constexpr uint8_t kInactiveAmbisonicsChannelNumber = 255;

  friend bool operator==(const AmbisonicsMonoConfig& lhs,
                         const AmbisonicsMonoConfig& rhs) = default;

  /*!\brief Validates the configuration.
   *
   * \param num_substreams_in_audio_element Number of substreams in the
   *     corresponding OBU.
   * \return `absl::OkStatus()` if successful. A specific status on failure.
   */
  absl::Status Validate(DecodedUleb128 num_substreams_in_audio_element) const;

  uint8_t output_channel_count;  // (C).
  uint8_t substream_count;       // (N).

  // Vector of length (C).
  std::vector<uint8_t> channel_mapping;
};

/*!\brief Configuration for projection-coded Ambisonics. */
struct AmbisonicsProjectionConfig {
  friend bool operator==(const AmbisonicsProjectionConfig& lhs,
                         const AmbisonicsProjectionConfig& rhs) = default;

  /*!\brief Validates the configuration.
   *
   * \param num_substreams_in_audio_element Number of substreams in the
   *     corresponding OBU.
   * \return `absl::OkStatus()` if successful. A specific status on failure.
   */
  absl::Status Validate(DecodedUleb128 num_substreams_in_audio_element) const;

  uint8_t output_channel_count;     // (C).
  uint8_t substream_count;          // (N).
  uint8_t coupled_substream_count;  // (M).

  // Vector of length (N + M) * C.
  std::vector<int16_t> demixing_matrix;
};

/*!\brief Config to reconstruct an Audio Element OBU using Ambisonics layout.
 *
 * The metadata required for combining the substreams identified here in order
 * to reconstruct an Ambisonics layout.
 */
struct AmbisonicsConfig {
  /*!\brief A `DecodedUleb128` enum for the method of coding Ambisonics. */
  enum AmbisonicsMode : DecodedUleb128 {
    kAmbisonicsModeMono = 0,
    kAmbisonicsModeProjection = 1,
    kAmbisonicsModeReservedStart = 2,
    kAmbisonicsModeReservedEnd = std::numeric_limits<DecodedUleb128>::max(),
  };
  friend bool operator==(const AmbisonicsConfig& lhs,
                         const AmbisonicsConfig& rhs) = default;

  /*!\brief Gets the next valid number of output channels.
   *
   * \param requested_output_channel_count Requested number of channels.
   * \param next_valid_output_channel_count Minimum valid `output_channel_count`
   *     that has at least the required number of channels.
   * \return `absl::OkStatus()` if successful. `kIamfInvalid` argument if
   *     the input is too large.
   */
  static absl::Status GetNextValidOutputChannelCount(
      uint8_t requested_output_channel_count,
      uint8_t& next_valid_output_channel_count);

  AmbisonicsMode ambisonics_mode;  // Serialized to a ULEB128.

  // The active field depends on `ambisonics_mode`.
  std::variant<AmbisonicsMonoConfig, AmbisonicsProjectionConfig>
      ambisonics_config;
};

struct ExtensionConfig {
  friend bool operator==(const ExtensionConfig& lhs,
                         const ExtensionConfig& rhs) = default;

  DecodedUleb128 audio_element_config_size;
  std::vector<uint8_t> audio_element_config_bytes;
};

/*!\brief Audio Element OBU.
 *
 * After constructing, the following MUST be called and return successfully.
 * 1. `InitializeAudioSubstreams()` and `InitializeParams()`.
 * 2. Exactly one of [
 *      `InitializeScalableChannelLayout()`,
 *      `InitializeAmbisonicsMono()`,
 *      `InitializeAmbisonicsProjection()`,
 *      `InitializeExtensionConfig()`
 *    ].
 *
 */
class AudioElementObu : public ObuBase {
 public:
  /*!\brief A 3-bit enum for the type of Audio Element. */
  enum AudioElementType : uint8_t {
    kAudioElementChannelBased = 0,
    kAudioElementSceneBased = 1,
    // Values in the range of [2 - 7] are reserved.
    kAudioElementBeginReserved = 2,
    kAudioElementEndReserved = 7,
  };

  /*!\brief Constructor.
   *
   * \param header `ObuHeader` of the OBU.
   * \param audio_element_id `audio_element_id` in the OBU.
   * \param audio_element_type Type of the OBU.
   * \param reserved Reserved bits of the OBU.
   * \param codec_config_id ID of the associated Codec Config OBU.
   */
  AudioElementObu(const ObuHeader& header, DecodedUleb128 audio_element_id,
                  AudioElementType audio_element_type, uint8_t reserved,
                  DecodedUleb128 codec_config_id);

  /*\!brief Move constructor.*/
  AudioElementObu(AudioElementObu&& other) = default;

  /*!\brief Destructor. */
  ~AudioElementObu() override {}

  friend bool operator==(const AudioElementObu& lhs,
                         const AudioElementObu& rhs) = default;

  /*\!brief Initializes the `audio_substream_ids_` vector.
   *
   * \param num_substreams Number of substreams.
   * \return `absl::OkStatus()` if successful. A specific status on failure.
   */
  void InitializeAudioSubstreams(uint32_t num_substreams);

  /*\!brief Initializes the `audio_element_params_` vector.
   *
   * \param num_parameters Number of parameters.
   */
  void InitializeParams(uint32_t num_parameters);

  /*\!brief Initializes a channel-based Audio Element OBU.
   *
   * Must be called after `audio_element_type_` is initialized to
   * `kAudioElementChannelBased`.
   *
   * \param num_layers Number of layers in the `ScalableChannelLayoutConfig`.
   * \param reserved Reserved bits of the `ScalableChannelLayoutConfig`.
   * \return `absl::OkStatus()` if successful. A specific status on failure.
   */
  absl::Status InitializeScalableChannelLayout(uint32_t num_layers,
                                               uint32_t reserved);

  /*\!brief Initializes an Ambisonics Mono Audio Element OBU.
   *
   * Must be called if and only if
   * `audio_element_type_` == `kAudioElementSceneBased` and
   * `ambisonics_mode` == `kAmbisonicsModeMono`.
   *
   * \param output_channel_count Number of output channels.
   * \param substream_count Number of substreams.
   * \return `absl::OkStatus()` if successful. A specific status on failure.
   */
  absl::Status InitializeAmbisonicsMono(uint32_t output_channel_count,
                                        uint32_t substream_count);

  /*\!brief Initializes an Ambisonics Projection Audio Element OBU.
   *
   * Must be called if and only if
   * `audio_element_type_` == `kAudioElementSceneBased` and
   * `ambisonics_mode` == `kAmbisonicsModeProjection`.
   *
   * \param output_channel_count Number of output channels.
   * \param substream_count Number of substreams.
   * \param coupled_substream_count Number of coupled substreams.
   * \return `absl::OkStatus()` if successful. A specific status on failure.
   */
  absl::Status InitializeAmbisonicsProjection(uint32_t output_channel_count,
                                              uint32_t substream_count,
                                              uint32_t coupled_substream_count);

  /*\!brief Initializes an extended type of Audio Element OBU.
   *
   * For future use when new `audio_element_type_` values are defined. Must be
   * called if and only if `audio_element_type_` is in the range of
   * [`kAudioElementBeginReserved`, `kAudioElementEndReserved`].
   *
   * \param audio_element_config_size Size in bytes of the
   *     `audio_element_config_bytes`.
   */
  void InitializeExtensionConfig(uint32_t audio_element_config_size);

  /*\!brief Prints logging information about the OBU.*/
  void PrintObu() const override;

  const DecodedUleb128 audio_element_id_;
  const AudioElementType audio_element_type_;  // 3 bits.
  uint8_t reserved_ = 0;                       // 5 bits.

  // ID of the associated Codec Config OBU.
  const DecodedUleb128 codec_config_id_;

  // Length and vector of substream IDs.
  DecodedUleb128 num_substreams_;
  std::vector<DecodedUleb128> audio_substream_ids_;

  // Length and vector of audio element parameters.
  DecodedUleb128 num_parameters_;
  std::vector<AudioElementParam> audio_element_params_;

  // Active field depends on `audio_element_type_`.
  std::variant<ScalableChannelLayoutConfig, AmbisonicsConfig, ExtensionConfig>
      config_;

 private:
  /*\!brief Writes the OBU payload to the buffer.
   *
   * \param wb Buffer to write to.
   * \return `absl::OkStatus()` if the payload is valid. A specific status on
   *     failure.
   */
  absl::Status ValidateAndWritePayload(WriteBitBuffer& wb) const override;
};

}  // namespace iamf_tools

#endif  // AUDIO_ELEMENT_H_
