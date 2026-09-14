#pragma once

#include <vector>

#include "tree/tree.hpp"

struct PruneInfo;

namespace mlipper::nni {

std::vector<int> candidate_regraft_edges(
    const TreeBuildResult& pruned_tree,
    const PruneInfo& prune_info);

} // namespace mlipper::nni
