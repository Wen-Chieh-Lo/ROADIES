# Tree State, GPU Packing, and Divide and Conquer

This directory defines the central CPU and GPU tree representations and the
operations that keep them synchronized. Read this before changing placement,
likelihood, topology refinement, or GPU allocation.

## Files

- `tree.hpp`: shared tree/state structures and host orchestration declarations.
- `tree_generation.cpp`: parse Newick with libpll, validate tips, construct the
  CPU tree, build the GTR Q matrix, and pack host arrays.
- `tree_generation_device.cu`: allocate/reload CUDA buffers, prepare traversal
  operations, launch CLV updates, and commit placement queries.
- `tree_topology_utils.hpp`: rebuild traversals and construct the coordinate
  order for sequential branch optimization.
- `divide_and_conquer.hpp/.cpp`: choose bounded sectors, extract subtrees, and
  construct directional boundary messages.

## The Three Tree Representations

### `TreeBuildResult`

Owns the mutable CPU topology: `TreeNode` objects, root ID, preorder/postorder
vectors, and tip-name lookup. `TreeNode::id` is an array index;
`stable_node_label` is the identity retained across topology repacking.

### `HostPacking`

Owns contiguous host arrays derived from `TreeBuildResult`: parent/child links,
branch lengths, tip characters, PMATs, traversal order, and pattern weights.
It is a transfer/evaluation representation, not an independent topology.

### `DeviceTree` and `OwnedDeviceTree`

`DeviceTree` is a trivially-copyable, non-owning CUDA descriptor passed by value
to kernels. `OwnedDeviceTree` extends that descriptor with move-only RAII
ownership. Only the owner calls `reset()`/`cudaFree`.

Never copy an `OwnedDeviceTree`, free pointers through a plain `DeviceTree`, or
assume a raw pointer remains valid after workspace rebuild.

## Key Construction Functions

- `build_tree_from_newick_with_pll()`: parse/validate a rooted binary tree and
  map alignment names to tips.
- `pack_host_arrays_from_tree_and_msa()`: encode topology, tip characters, and
  branch lengths into contiguous arrays.
- `fill_pmats_in_host_packing()`: build CPU PMAT arrays for all or selected
  changed nodes.
- `allocate_device_tree_on_current_gpu()`: allocate one owned GPU tree and copy
  model/tree/query state.
- `reload_device_tree_live_data*()`: refresh an existing allocation after state
  changes; variants either rebuild all data or preserve CLVs needed by Local
  SPR/D&C.
- `load_subtree_workspace()`: build or reuse a bounded owned subtree workspace.

## CLV Operation Functions

- `PrepareTreeClvOperations()`: create reusable upward and downward traversal
  operation buffers.
- `UpdateTreeClvs()`: full initial/global rebuild.
- `UpdateTreeClvsPreservingTipClvs()`: rebuild internal state without replacing
  virtual boundary-tip messages.
- `UpdateTreeClvsUpwardOnly()`: postorder-only path for root likelihood/model
  optimization.
- `UpdateTreeClvsPrepared()`: execute already prepared upward/downward buffers.
- `UpdateTreeClvsAfterPrune()`: refresh only state affected during local
  prune/regraft scoring.
- `BuildSingleTreeMidBaseWarpSitePrepared()`,
  `UpdateSingleTreeClvUpwardWarpSitePrepared()`, and
  `RefreshSingleTreeChildDownPrepared()`: one-step directional updates for
  sequential branch optimization.

## Placement Integration

- `build_placement_query()`: convert alignment rows to named query records.
- `make_query_view()`: point a non-owning `DeviceTree` view at one query slot.
- `UploadPlacementOps()`: upload candidate/traversal operations.
- `EvaluatePlacementQueries()`: score queries and optionally commit them into
  the CPU tree using `PlacementCommitContext`.

After a commit, the topology, host packing, traversal operations, query/tip
capacities, PMATs, and affected CLVs must all describe the same tree.

## Divide-and-Conquer Functions

- `build_subtree_plan_from_anchors()` and `build_nni_sector_plan()`: choose the
  nodes/edges belonging to a bounded work unit.
- `build_subtree_from_plan()`: create the local CPU topology.
- `prepare_anchor_subtree_with_directional_boundaries()` and
  `prepare_nni_sector_with_directional_boundaries()`: attach virtual boundary
  tips carrying likelihood messages from the omitted side of each cut.
- `build_query_batch_from_placement_queries()`: encode those virtual/query
  records for the subtree device workspace.

Boundary messages make a local sector score consistent with its surrounding
full tree. Treating a boundary tip as an ordinary sequence tip changes the
likelihood problem.

## Required Update Order After Topology Change

```text
repair TreeBuildResult links
  -> rebuild_traversals()
  -> rebuild/update HostPacking
  -> refresh branch PMATs
  -> prepare/upload operation buffers
  -> refresh affected CLVs and scalers
  -> evaluate likelihood
```

Most subtle bugs in this folder come from skipping one representation in this
sequence rather than from the topology edit itself.
