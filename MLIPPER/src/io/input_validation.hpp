#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace parse {
struct Alignment;
struct ModelConfig;
}

namespace mlipper {
namespace input {

class CliError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ValidationError : public CliError {
public:
    ValidationError(const std::string& option_name, const std::string& message)
        : CliError(option_name + ": " + message) {}
};

std::filesystem::path normalize_cli_path(
    const std::filesystem::path& base,
    const std::string& raw_path);

void validate_output_path(
    const std::filesystem::path& base,
    const std::string& option_name,
    const std::string& raw_path);

void validate_alignment_names(
    const parse::Alignment& alignment,
    const std::string& option_name);

void validate_alignment_symbols(
    const parse::Alignment& alignment,
    int states,
    const std::string& option_name);

void validate_model_inputs(const parse::ModelConfig& model);

void validate_query_reference_name_overlap(
    const parse::Alignment& tree_alignment,
    const parse::Alignment& query_alignment,
    const std::string& option_name);

} // namespace input
} // namespace mlipper
