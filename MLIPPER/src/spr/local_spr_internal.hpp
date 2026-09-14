#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "local_spr.hpp"
#include "placement/placement.cuh"
#include "util/mlipper_util.h"
#include "tree/divide_and_conquer.hpp"

struct PruneInfo {
    int pruned_id = -1, free_internal_id = -1, sibling_id = -1, grandparent_id = -1;
};

using LocalSPRInsertionAnchor = mlipper::divide_and_conquer::TreeEdgeEndpoints;

struct LocalSPRRepairUnit {
    int unit_id = -1;
    std::vector<LocalSPRInsertionAnchor> anchors;
    std::vector<int> anchor_indices;
    std::vector<char> envelope_mask;
    std::vector<int> envelope_nodes;
};

struct LocalSPRCandidateMove {
    int repair_unit_id = -1, prune_root_id = -1;
    int regraft_child_id = -1, regraft_parent_id = -1, old_parent_id = -1;
    double approx_gain = -std::numeric_limits<double>::infinity();
    double pendant_length = 0.0, proximal_length = 0.0;
    std::vector<int> subtree_nodes;
    std::vector<int> regraft_path_nodes;
};

struct LocalSPRSearchSummary {
    size_t unit_count = 0, enumerated_candidates = 0;
    size_t retained_candidates = 0, selected_candidates = 0;
};

struct LocalSPRPruneRootWorkItem {
    int prune_root_id = -1, skeleton_distance = std::numeric_limits<int>::max();
    std::vector<int> subtree_nodes;
    std::vector<int> legal_inner_candidate_edges;
};

struct LocalSPRPlacementPassResult {
    RawPlacementResult placement_result;
    std::vector<NodeOpInfo> required_downward_update_ops;
};

struct LocalSPRPruneRootEvaluationResult {
    std::vector<LocalSPRCandidateMove> candidates;
    size_t enumerated_candidates = 0;
};

struct LocalSPRPreparedPruneRoot {
    bool valid = false;
    int lane_index = -1, prune_root_id = -1;
    size_t enumerated_candidates = 0;
    std::vector<int> subtree_nodes;
    std::vector<char> subtree_mask;
    TreeBuildResult pruned_tree;
    PruneInfo prune_info;
    HostPacking pruned_host;
    std::vector<int> node_to_tip;
    std::vector<NodeOpInfo> inner_candidate_ops;
    std::vector<NodeOpInfo> inner_required_update_ops;
};

// Owns one stream's reusable device buffers. Dirty-node tracking permits the
// next candidate to restore only upward CLVs overwritten by the previous one.
struct LocalSPRScoringWorkspace {
    OwnedSubtreeWorkspace subtree_workspace{};
    PlacementOpBuffer tree_ops{};
    PlacementOpBuffer candidate_ops{};
    PlacementScratchOverride placement_scratch{};
    int previous_main_pmat_node = -1;
    int* d_upward_restore_nodes = nullptr;
    int upward_restore_capacity = 0;
    bool upward_state_initialized = false;
    std::vector<int> dirty_upward_nodes;
};

struct LocalSPRBatchScoringWorkspace {
    bool initialized = false;
    int batch_capacity = 0, node_capacity = 0;
    TreeBuildResult tree{};
    HostPacking host{};
    OwnedSubtreeWorkspace subtree_workspace{};
    PlacementOpBuffer candidate_ops{};
    PlacementScratchOverride placement_scratch{};
};

struct LocalSPRScoringLane {
    cudaStream_t stream = nullptr;
    cudaEvent_t ready_event = nullptr;
    LocalSPRScoringWorkspace workspace{};
};

class FixedThreadPool {
public:
    explicit FixedThreadPool(int worker_count)
    {
        const int pool_size = std::max(1, worker_count);
        workers_.reserve(static_cast<size_t>(pool_size));
        for (int worker_idx = 0; worker_idx < pool_size; ++worker_idx) {
            workers_.emplace_back([this]() { worker_loop(); });
        }
    }

    ~FixedThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    template <typename Fn>
    auto submit(Fn&& fn)
        -> std::future<std::invoke_result_t<std::decay_t<Fn>&>>
    {
        using Result = std::invoke_result_t<std::decay_t<Fn>&>;
        auto task =
            std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(fn));
        std::future<Result> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw std::runtime_error(
                    "local SPR thread pool is shutting down");
            }
            tasks_.emplace_back([task]() { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

private:
    void worker_loop()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() {
                    return stopping_ || !tasks_.empty();
                });
                if (stopping_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
};

struct LocalSPRExecutionResources {
    int worker_count = 0;
    std::unique_ptr<FixedThreadPool> worker_pool;
};

struct LocalSPRRankingWorkspace {
    // Streams and events are bound to owner_device_id and must be released or
    // rebuilt before this workspace is reused on another CUDA device.
    cudaStream_t owner_stream = nullptr;
    int owner_device_id = -1;
    int scoring_lane_count = 0;
    std::vector<LocalSPRScoringLane> scoring_lanes;
    LocalSPRBatchScoringWorkspace batch_scoring_workspace;
};

struct LocalSPRPersistentWorkspaceImpl {
    LocalSPRExecutionResources execution_resources{};
};

inline void ensure_local_spr_scoring_lane_current_device(
    const LocalSPRScoringLane& lane,
    const char* context)
{
    ensure_device_tree_current_device(
        lane.workspace.subtree_workspace.dev,
        context);
}

inline int local_spr_target_id_from_op(const NodeOpInfo& op)
{
    const bool target_is_left =
        (op.dir_tag == static_cast<uint8_t>(CLV_DIR_DOWN_LEFT));
    const bool target_is_right =
        (op.dir_tag == static_cast<uint8_t>(CLV_DIR_DOWN_RIGHT));
    return target_is_left ? op.left_id : (target_is_right ? op.right_id : op.parent_id);
}

struct LocalSPREvalWorkspace {
    OwnedSubtreeWorkspace subtree_workspace{};
    TreeBuildResult tree{};
    HostPacking host_pack{};
    PlacementOpBuffer ops{};
    cudaStream_t stream = nullptr;
    int device_id = -1;
    bool initialized = false;
};

struct LocalSPRSessionWorkspaceSet {
    LocalSPREvalWorkspace eval_workspace{};
    LocalSPRRankingWorkspace ranking_workspace{};
};

struct IntDisjointSet {
    std::vector<int> parent, rank;

    explicit IntDisjointSet(int n) : parent((size_t)n), rank((size_t)n, 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }

    int find(int x) {
        if (parent[(size_t)x] != x) {
            parent[(size_t)x] = find(parent[(size_t)x]);
        }
        return parent[(size_t)x];
    }

    void unite(int a, int b) {
        int ra = find(a);
        int rb = find(b);
        if (ra == rb) return;
        if (rank[(size_t)ra] < rank[(size_t)rb]) std::swap(ra, rb);
        parent[(size_t)rb] = ra;
        if (rank[(size_t)ra] == rank[(size_t)rb]) {
            ++rank[(size_t)ra];
        }
    }
};

struct TopologyRefinementSearchContext {
    TopologyRefinementState& state;
    cudaStream_t stream = nullptr;
    const std::vector<unsigned>& pattern_weights_arg;
    const std::vector<double>& rate_weights;
    const std::vector<double>& rate_multipliers;
    const std::vector<double>& pi;
    size_t sites = 0;
    int states = 0, rate_cats = 0;
    bool per_rate_scaling = false;
    PlacementTuningConfig placement_tuning;
    int radius = 4;
    int topk_per_unit = 0;
    mlipper::TopologyMoveType move_type = mlipper::TopologyMoveType::SPR;
    const std::vector<int>& forbidden_regraft_node_ids;
    const std::unordered_set<int>& allowed_nni_central_edge_child_ids;
};

void local_spr_assert(bool condition, const std::string& message);

void collect_subtree_node_ids(const TreeBuildResult& tree, int node_id, std::vector<int>& node_ids);

bool subtree_fully_inside_mask(
    const TreeBuildResult& tree, int node_id, const std::vector<char>& mask,
    std::vector<int>* node_ids_out = nullptr);

void rebuild_host_topology_from_tree_local(const TreeBuildResult& tree, HostPacking& host);

std::vector<int> build_local_spr_node_to_tip(const TreeBuildResult& tree, const HostPacking& host);

void build_selected_downward_ops(
    const TreeBuildResult& tree, const std::vector<int>& node_to_tip,
    const std::vector<int>& target_child_ids, std::vector<NodeOpInfo>& host_ops);

void build_required_downward_update_ops(
    const TreeBuildResult& tree, const std::vector<int>& node_to_tip,
    const std::vector<int>& target_child_ids, std::vector<NodeOpInfo>& host_ops);

std::vector<NodeOpInfo> filter_local_spr_new_ops(
    const std::vector<NodeOpInfo>& ops, const std::vector<NodeOpInfo>& already_computed_ops);

std::vector<char> build_local_spr_subtree_mask(const TreeBuildResult& tree, const std::vector<int>& subtree_nodes);

std::vector<int> filter_local_spr_candidate_edges(
    const TreeBuildResult& pruned_tree, const std::vector<char>& envelope_mask,
    const std::vector<char>& subtree_mask, const std::vector<int>& edge_candidates);

HostPacking build_local_spr_tree_host_packing(
    const TopologyRefinementRunContext& ctx,
    const TreeBuildResult& tree);

void local_spr_assert_candidate_legal(
    const TreeBuildResult& tree, const LocalSPRRepairUnit& unit, int prune_root_id,
    const std::vector<int>& subtree_nodes, const std::vector<int>& legal_edges, int regraft_child_id);

std::vector<LocalSPRInsertionAnchor> build_local_spr_insertion_anchors(
    const TreeBuildResult& tree,
    const std::vector<mlipper::PlacementResult>& committed_placements);
std::vector<LocalSPRInsertionAnchor> refresh_local_spr_anchor_parents(
    const TreeBuildResult& tree,
    const std::vector<LocalSPRInsertionAnchor>& anchors);

std::vector<LocalSPRRepairUnit> build_local_spr_repair_units(
    const TreeBuildResult& tree, const std::vector<LocalSPRInsertionAnchor>& anchors,
    int cluster_threshold, int envelope_radius);

std::vector<LocalSPRRepairUnit> build_nni_repair_units(
    const TreeBuildResult& tree,
    const std::unordered_set<int>& central_edge_child_ids);

bool local_spr_candidate_better(const LocalSPRCandidateMove& lhs, const LocalSPRCandidateMove& rhs);

void keep_local_spr_topk(std::vector<LocalSPRCandidateMove>& topk, LocalSPRCandidateMove candidate, int topk_limit);

std::vector<int> select_local_spr_seed_edges(
    const RawPlacementResult& placement_result, const TreeBuildResult& tree,
    const std::vector<int>& center_dist, double baseline_logL, int seed_limit, int required_edge_radius);

std::vector<LocalSPRCandidateMove> select_local_spr_candidates(
    const std::vector<LocalSPRCandidateMove>& ranked_candidates,
    int node_count);

bool local_spr_candidate_still_legal(
    const TreeBuildResult& tree, const LocalSPRRepairUnit& unit, const LocalSPRCandidateMove& candidate,
    std::vector<int>* current_subtree_nodes_out = nullptr);

bool prune_subtree_for_spr(TreeBuildResult& tree, int pruned_id, PruneInfo& info);

std::vector<char> build_unit_anchor_skeleton_mask(
    const TreeBuildResult& tree,
    const std::vector<LocalSPRInsertionAnchor>& anchors);

std::vector<int> collect_candidate_edges(
    const TreeBuildResult& tree, int endpoint_a, int endpoint_b, int radius, int exclude_a, int exclude_b);

std::vector<int> compute_center_distances(const TreeBuildResult& tree, int endpoint_a, int endpoint_b);

std::vector<int> collect_outward_edges_from_seed(
    const TreeBuildResult& tree, const std::vector<int>& center_dist,
    int seed_edge_child_id, int min_radius_exclusive, int max_radius, int exclude_a, int exclude_b);

void regraft_subtree_for_spr(
    TreeBuildResult& tree, const PruneInfo& info, int target_child_id,
    double pendant_length, double proximal_length,
    double pruned_branch_min, double target_branch_min);

std::vector<LocalSPRCandidateMove> rank_local_spr_candidates(
    const TopologyRefinementSearchContext& ctx, const TreeBuildResult& base_tree,
    const std::vector<LocalSPRRepairUnit>& repair_units, LocalSPRRankingWorkspace& ranking_workspace,
    FixedThreadPool& worker_pool,
    LocalSPRSearchSummary& search_summary);

void release_local_spr_session_workspace_set(
    LocalSPRSessionWorkspaceSet& workspace_set,
    cudaStream_t stream);

int local_spr_scoring_lane_count();

void release_local_spr_ranking_workspace(LocalSPRRankingWorkspace& workspace);
