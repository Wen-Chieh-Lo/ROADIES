#pragma once

#include <string>
#include <vector>

struct RawPlacementResult;
struct TreeBuildResult;

namespace mlipper {
namespace jplaceio {

void write_jplace(
    const std::string& output_path,
    const TreeBuildResult& tree,
    const std::vector<::RawPlacementResult>& placement_results,
    const std::vector<std::string>& query_names,
    const std::string& invocation);

} // namespace jplaceio
} // namespace mlipper
