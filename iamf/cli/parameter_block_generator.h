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

#ifndef CLI_PARAMETER_BLOCK_GENERATOR_H_
#define CLI_PARAMETER_BLOCK_GENERATOR_H_

#include <cstdint>
#include <list>
#include <memory>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "iamf/cli/audio_element_with_data.h"
#include "iamf/cli/demixing_module.h"
#include "iamf/cli/global_timing_module.h"
#include "iamf/cli/parameter_block_with_data.h"
#include "iamf/cli/proto/parameter_block.pb.h"
#include "iamf/cli/recon_gain_generator.h"
#include "iamf/obu/ia_sequence_header.h"
#include "iamf/obu/leb128.h"
#include "iamf/obu/mix_presentation.h"
#include "iamf/obu/param_definitions.h"
#include "iamf/obu/parameter_block.h"

namespace iamf_tools {

// TODO(b/296815263): Add tests for this class.
// TODO(b/306319126): Generate one parameter block at a time.

/*\!brief Generator of parameter blocks.
 *
 * The use pattern of this class is:
 *
 *   - Initialize (`Initialize()`).
 *   - Repeat for each temporal unit (along with the audio frame generation):
 *     - For all parameter blocks metadata that start at the current
 *       timestamp:
 *       - Add the metadata (`AddMetadata()`).
 *     - Generate demixing parameter blocks (`GenerateDemixing()`).
 *     - Generate mix gain parameter blocks (`GenerateMixGain()`).
 *     - // After audio frames are decoded and demixed.
 *     - Generate recon gain parameter blocks (`GenerateReconGain()`).
 */
class ParameterBlockGenerator {
 public:
  /*\!brief Constructor.
   *
   * \param override_computed_recon_gains Whether to override recon gains
   *     with user provided values.
   * \param parameter_id_to_metadata Mapping from parameter IDs to per-ID
   *     parameter metadata.
   */
  ParameterBlockGenerator(
      bool override_computed_recon_gains,
      absl::flat_hash_map<DecodedUleb128, PerIdParameterMetadata>&
          parameter_id_to_metadata)
      : override_computed_recon_gains_(override_computed_recon_gains),
        parameter_id_to_metadata_(parameter_id_to_metadata) {}

  /*\!brief Initializes the class.
   *
   * Must be called before any `Generate*()` function, otherwise they will
   * be no-ops (not failing).
   *
   * \param ia_sequence_header_obu IA Sequence Header OBU.
   * \param audio_elements Input Audio Element OBUs with data.
   * \param mix_presentation_obus Input Mix Presentation OBUs with all
   *     `ParamDefinitions` filled in.
   * \param param_definitions Mapping from parameter IDs to param definitions.
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status Initialize(
      const std::optional<IASequenceHeaderObu>& ia_sequence_header_obu,
      const absl::flat_hash_map<DecodedUleb128, AudioElementWithData>&
          audio_elements,
      const std::list<MixPresentationObu>& mix_presentation_obus,
      const absl::flat_hash_map<DecodedUleb128, const ParamDefinition*>&
          param_definitions);

  /*\!brief Adds one parameter block metadata.
   *
   * \param parameter_block_metadata parameter block metadata to add.
   * \param duration Output duration of the corresponding parameter block; may
   *      come from the added metadata or its param definition.
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status AddMetadata(
      const iamf_tools_cli_proto::ParameterBlockObuMetadata&
          parameter_block_metadata,
      uint32_t& duration);

  /*\!brief Generates a list of demixing parameter blocks with data.
   *
   * \param global_timing_module Global timing module to keep track of the
   *     timestamps of the generated parameter blocks.
   * \param output_parameter_blocks Output list of parameter blocks with data.
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status GenerateDemixing(
      GlobalTimingModule& global_timing_module,
      std::list<ParameterBlockWithData>& output_parameter_blocks);

  /*\!brief Generates a list of mix gain parameter blocks with data.
   *
   * \param global_timing_module Global timing module to keep track of the
   *     timestamps of the generated parameter blocks.
   * \param output_parameter_blocks Output list of parameter blocks with data.
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status GenerateMixGain(
      GlobalTimingModule& global_timing_module,
      std::list<ParameterBlockWithData>& output_parameter_blocks);

  /*\!brief Generates a list of recon gain parameter blocks with data.
   *
   * \param id_to_time_to_labeled_frame Data structure for samples.
   * \param id_to_time_to_labeled_decoded_frame Data structure for decoded
   *     samples.
   * \param global_timing_module Global timing module to keep track of the
   *     timestamps of the generated parameter blocks.
   * \param output_parameter_blocks Output list of parameter blocks with data.
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status GenerateReconGain(
      const IdTimeLabeledFrameMap& id_to_time_to_labeled_frame,
      const IdTimeLabeledFrameMap& id_to_time_to_labeled_decoded_frame,
      GlobalTimingModule& global_timing_module,
      std::list<ParameterBlockWithData>& output_parameter_blocks);

 private:
  /*\!brief Generates a list of parameter blocks with data.
   *
   * \param proto_metadata_list Input list of user-defined metadata about
   *     parameter blocks.
   * \param global_timing_module Global Timing Module.
   * \param output_parameter_blocks Output list of parameter blocks with data.
   * \return `absl::OkStatus()` on success. A specific status on failure.
   */
  absl::Status GenerateParameterBlocks(
      std::list<iamf_tools_cli_proto::ParameterBlockObuMetadata>&
          proto_metadata_list,
      GlobalTimingModule& global_timing_module,
      std::list<ParameterBlockWithData>& output_parameter_blocks);

  const bool override_computed_recon_gains_;

  // Mapping from parameter IDs to parameter metadata.
  absl::flat_hash_map<DecodedUleb128, PerIdParameterMetadata>&
      parameter_id_to_metadata_;

  ProfileVersion primary_profile_;

  std::unique_ptr<ReconGainGenerator> recon_gain_generator_;

  // User metadata about Parameter Block OBUs categorized based on
  // the parameter definition type.
  absl::flat_hash_map<
      ParamDefinition::ParameterDefinitionType,
      std::list<iamf_tools_cli_proto::ParameterBlockObuMetadata>>
      typed_proto_metadata_;
};

}  // namespace iamf_tools

#endif  // CLI_PARAMETER_BLOCK_GENERATOR_H_
