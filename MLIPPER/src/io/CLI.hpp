#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "mlipper_session.hpp"
#include "workflow/dipper_starting_tree.hpp"

namespace mlipper::cli {

enum class WorkflowKind {
    SmallTip,
    DivideAndConquer,
};

WorkflowKind selectWorkflow(int argc, char** argv);

struct SmallTipConfig {
    // Fully parsed, validated, and preprocessed values. Paths are normalized
    // against config_base, so workflow code performs no further CLI I/O.
    std::string backbone_tree;
    parse::Alignment tree_alignment;
    parse::Alignment query_alignment;
    parse::ModelConfig model;
    std::vector<unsigned> pattern_weights;
    MlipperPlacementParams params;
    MlipperLocalSPRParams local_spr_params;
    MlipperGpuConfig gpu_config;
    FinalModelOptimizationOptions final_optimization;
    std::filesystem::path tree_output_path;
    std::filesystem::path jplace_output_path;
    std::string invocation;
};

struct SmallTipParseResult {
    SmallTipConfig config;
    bool should_run = false;
    int exit_code = 0;
};

SmallTipParseResult loadSmallTipConfigFromCommandLine(
    int argc,
    char** argv,
    const std::filesystem::path& config_base);

void reportFinalOptimization(const FinalModelOptimizationResult& result);

struct DivideAndConquerConfig {
    // D&C builds its own starting topology and therefore owns no backbone-tree input.
    parse::Alignment tree_alignment;
    parse::ModelConfig model;
    bool model_uses_empirical_freqs = false;
    workflow::DipperTreeMode dipper_starting_tree_mode =
        workflow::DipperTreeMode::NJPlacement;
    MlipperDivideAndConquerParams params;
    MlipperGpuConfig gpu_config;
    std::filesystem::path write_tree_path;
    DivideAndConquerNNIOptions nni_options;
    DivideAndConquerFinalOptimizationOptions final_optimization;
};

struct DivideAndConquerParseResult {
    DivideAndConquerConfig config;
    bool should_run = false;
    int exit_code = 0;
};

DivideAndConquerParseResult loadDivideAndConquerConfigFromCommandLine(
    int argc,
    char** argv,
    const std::filesystem::path& config_base);

void reportDivideAndConquerResult(const DivideAndConquerNNIResult& result);
void reportDivideAndConquerFinalOptimization(
    const DivideAndConquerFinalOptimizationResult& result);

} // namespace mlipper::cli
