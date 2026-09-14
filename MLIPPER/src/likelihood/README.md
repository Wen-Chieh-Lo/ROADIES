# Likelihood Computation

This directory contains CUDA operations corresponding to libpll's partial and
likelihood concepts. The maintained naming follows the libpll mental model:
update partials first, then evaluate either a root likelihood or candidate-edge
likelihoods.

## Core Concepts

- A CLV (conditional likelihood vector) stores likelihoods for every site,
  rate category, and state at a node or directional edge.
- An upward CLV summarizes descendants in postorder.
- A downward CLV summarizes the rest of the tree in preorder.
- A scaler prevents numerical underflow; pattern weights restore multiplicity
  after repeated alignment columns are compressed.
- A PMAT is the transition matrix for one branch and rate category.

All functions consume a non-owning `DeviceTree`. Allocation and traversal
schedule preparation happen in `tree/tree_generation_device.cu`.

## Files and Key Functions

### `partials.cuh/.cu`

GPU counterparts of `pll_update_partials`:

- `InitializeTipPartialsKernel`: decode tip character masks into initial CLVs.
- `UpdatePartialsUpwardKernel`: apply postorder operations for one tree.
- `UpdatePartialsUpwardLevelKernel`: evaluate independent operations from one
  traversal level in a batched grid.
- `UpdatePartialsDownwardKernel` and `UpdatePartialsDownwardLevelKernel`:
  propagate directional likelihood state from parent/outside toward children.
- `BuildTreeMidBaseWarpSiteKernel`, `UpdateTreeUpwardWarpSiteKernel`, and
  `RefreshTreeChildDownWarpSiteKernel`: DNA+G4 single-operation primitives used
  during sequential branch optimization.

### `root_loglikelihood.cuh/.cu`

- `likelihood::root::compute_root_loglikelihood()`: combine one root CLV with
  equilibrium frequencies, rate weights, scalers, and pattern weights. This
  plays the role of libpll's `pll_compute_root_loglikelihood`.

### `placement_likelihood.cuh/.cu`

- `likelihood::placement::update_midpoint_partials()`: build the three-way
  midpoint partials for candidate attachment edges.
- `likelihood::placement::compute_edge_loglikelihoods()`: score a batch of
  candidate edges, analogous to batched `pll_compute_edge_loglikelihood`.

## Normal Call Paths

Full-tree likelihood:

```text
PrepareTreeClvOperations()
  -> UpdateTreeClvs*()
  -> likelihood::root::compute_root_loglikelihood()
```

Placement likelihood:

```text
EvaluatePlacementCandidates()
  -> likelihood::placement::update_midpoint_partials()
  -> optimize pendant/proximal lengths
  -> likelihood::placement::compute_edge_loglikelihoods()
```

Do not launch partial kernels directly from workflow code. Prefer the host
orchestration functions documented in `tree/README.md` and
`placement/README.md`; they maintain traversal and scaler invariants.

## Correctness Invariants

- `DeviceTree.sites`, `states`, and `rate_cats` define every CLV stride.
- The PMAT orientation must agree with `downward_pmat_indexing`.
- A topology or branch-length change invalidates specific CLVs and/or PMATs.
- Tip-preserving D&C paths must not overwrite virtual boundary-tip CLVs.
- Likelihood accumulation is double precision even when `fp_t` is float.
- Any new fast path must retain the generic path and be compared against it.
