# Transition Matrices

This directory converts a substitution model and branch length into transition
probability matrices (PMATs). Likelihood and derivative kernels consume these
matrices but do not construct the model eigendecomposition themselves.

## Mathematical Path

```text
frequencies + GTR exchangeability rates
  -> Q matrix                    (tree/model code)
  -> eigendecomposition          (gtr_eigendecomp_cpu)
  -> exp(Q * rate * length)      (PMAT construction)
  -> CLV updates / likelihood
```

## Files and Functions

- `pmat.h/.cpp`
  - `EigResult`: eigenvalues, eigenvectors, and inverse eigenvectors.
  - `gtr_eigendecomp_cpu()`: compute the reversible-model decomposition used by
    both CPU packing and GPU PMAT builders.
  - `pmatrix_from_triple()`: CPU construction of one transition matrix from the
    decomposition, rate multiplier, branch length, and invariant proportion.
- `pmat_gpu.cuh/.cu`
  - `pmatrix_from_triple_device()`: device-side construction for a small state
    space, currently up to 16 states.
  - `build_all_branch_pmats_device()`: rebuild the main PMAT pool for every
    node/branch without copying branch lengths back to the host.
  - `build_single_branch_pmat_device()`: refresh one branch after a sequential
    coordinate update.

## Ownership and Layout

`EigResult` owns CPU `double` vectors. The corresponding `DeviceTree` fields
(`d_lambdas`, `d_V`, `d_Vinv`) are non-owning GPU pointers. A PMAT contains
`rate_categories * states * states` values per node slot.

The node slot represents the branch from that node to its parent. The root slot
does not represent an ordinary parent edge but remains part of the fixed array
layout.

## When PMATs Must Be Rebuilt

- substitution rates or equilibrium frequencies changed;
- Gamma alpha changed the category rate multipliers;
- a branch length changed;
- topology packing moved branch lengths to different node IDs.

Use the all-branch builder after a global model change. Use the single-branch
builder inside sequential branch optimization. Rebuilding too little gives
stale likelihoods; rebuilding everything inside every edge update destroys the
intended optimization performance.

Numerical cleanup in `pmat.cpp` clamps small negative values and normalizes
rows. Changes here affect every workflow and require likelihood regression.
