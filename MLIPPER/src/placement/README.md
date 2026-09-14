# Placement and Branch Derivatives

This directory scores query attachments and implements the derivative-based
branch-length updates shared by placement and existing-tree optimization.

## Files

- `placement.cuh/.cu`: placement candidate evaluation, ranking, LWR collection,
  and host-level full-tree/selected-edge branch optimizers.
- `derivative.cuh/.cu`: CUDA derivative kernels and single-edge Newton update
  primitives.

The files currently remain together because placement and tree-edge
optimization reuse substantial derivative and PMAT machinery. Workflow code
should call the host functions in the headers, not individual internal kernels.

## Placement Result

`RawPlacementResult` contains the best edge and optimized distal/pendant branch
lengths. When accumulated-LWR output is enabled, `top_placements` holds the
ranked prefix needed for jplace output.

`target_id` is a node ID representing the edge from that node to its parent.
It is not a stable label across topology reconstruction.

## Key Functions

- `EvaluatePlacementCandidates()`: score candidate operations for one query,
  optimize pendant/proximal lengths, optionally refine the local child side,
  and return the best/ranked placements.
- `RunAcceptedFullTreeSequentialBranchLengthOptimization()`: optimize existing
  tree edges in coordinate order. Each accepted edge update refreshes the PMAT
  and directional state required by the next edge.
- `RunSelectedTreeEdgeJacobiBranchLengthOptimization()`: optimize a selected
  edge set from a common CLV state, then accept or reject the group according
  to `BranchOptimizationOptions`.
- `OptimizeSingleTreeEdgeFromCurrentClvs()`: generic single-edge Newton update
  from the directional CLVs surrounding an existing edge.
- `OptimizeSingleTreeEdgeFromCurrentClvsWarpSite()`: specialized DNA+G4 version
  used when the device/model shape supports it.
- `LikelihoodDerivativePendantKernel` and
  `LikelihoodDerivativeProximalKernel`: placement-side derivative kernels for
  the two variable attachment branches.

## Placement Call Flow

```text
tree::EvaluatePlacementQueries()
  -> EvaluatePlacementCandidates()
       -> build midpoint partials
       -> optimize pendant/proximal lengths
       -> compute candidate likelihoods
       -> rank candidates / accumulate LWR
  -> optionally commit the selected query into the CPU tree
```

`PlacementScratchOverride` allows D&C/Local SPR workspaces to provide already
allocated scratch buffers. Every pointer is non-owning, and each capacity field
must cover the corresponding operation or node count.

## Branch Optimization Semantics

- Sequential/Gauss-Seidel: later edges see earlier accepted updates. Used for
  full-tree final branch optimization.
- Jacobi: selected edges are optimized from one common state before the group
  is committed. Used for local or sector refinement.
- Branch bounds and tolerances come from `optimize/optimization_types.hpp`.
- A branch update is not complete until its PMAT and affected CLVs are current.

## Common Failure Modes

- Treating node IDs as stable across a committed topology edit.
- Using stale midpoint/downward CLVs after changing a branch or topology.
- Passing scratch buffers sized for fewer operations than `num_ops`.
- Comparing a local candidate likelihood as if it were a full-tree acceptance
  audit.
- Adding a new branch bound locally instead of using the shared optimization
  policy.
