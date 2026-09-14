# Topology Refinement

This directory implements the shared topology-search engine used for Local SPR
after small-tip insertion and NNI inside divide-and-conquer sectors.

## Files

- `local_spr.hpp/.cpp`: public refinement context, workspace lifetime, and the
  high-level round/repair-unit loop.
- `local_spr_internal.hpp`: internal candidate, workspace, and search-context
  types shared by implementation files. It is not a workflow API.
- `local_spr_topology.cpp`: graph distances, masks, candidate legality, prune,
  regraft, NNI construction, and host-topology repair.
- `local_spr_scoring.cpp`: GPU subtree loading, candidate likelihood scoring,
  ranking, concurrency lanes, and workspace reuse.
- `nni.hpp/.cpp`: small topology helpers reused by NNI candidate generation.

## One Engine, Two Move Types

`mlipper::TopologyMoveType` selects `SPR` or `NNI`.

- Small-tip supplies recently inserted attachment edges as anchors and uses a
  bounded SPR radius.
- D&C supplies sector-owned internal splits and allows only NNI moves belonging
  to that sector.

Both routes enter `run_topology_refinement(TopologyRefinementRunContext&)`.

## Key Types and Functions

- `TopologyRefinementRunContext`: non-owning references to the current CPU/GPU
  tree state, model vectors, anchors, options, and reusable session workspaces.
- `LocalSPRPersistentWorkspace`: move-only owner used across rounds/batches to
  avoid repeated scratch allocation.
- `build_local_spr_repair_units()` / `build_nni_repair_units()`: group anchors
  into independently processed search units.
- `select_local_spr_seed_edges()` and `collect_candidate_edges()`: define the
  local search neighborhood.
- `prune_subtree_for_spr()` and `regraft_subtree_for_spr()`: apply/revert the
  host topology transformation used to construct candidates.
- `rank_local_spr_candidates()`: evaluate candidates and retain the best legal
  moves.
- `select_local_spr_candidates()`: choose a compatible set from ranked moves.
- `candidate_regraft_edges()`: enumerate legal NNI/SPR-adjacent edge options.

## Round Structure

```text
anchors
  -> repair units
  -> candidate neighborhoods
  -> prepare bounded subtree and directional boundary CLVs
  -> score candidates on GPU
  -> select non-conflicting improving moves
  -> revalidate against current topology
  -> commit moves
  -> rebuild affected host/GPU state
```

Candidates are generated from a snapshot, but earlier accepted moves can make a
later candidate illegal. Always retain the final legality/revalidation step.

## State Rules

- `TopologyRefinementState.device` is a non-owning view; the session/workspace
  remains responsible for CUDA memory.
- A prune/regraft must keep parent/left/right links mutually consistent.
- Rebuild preorder/postorder traversal after committing topology changes.
- D&C virtual boundary tips carry precomputed CLVs and must use the preserve-tip
  update path.
- Stable node labels identify committed query/attachment nodes across packing;
  array node IDs do not.

Topology-only helpers are covered by `tests/topology_refinement_test.cpp`.
Scoring or acceptance changes require a GPU workflow regression as well.
