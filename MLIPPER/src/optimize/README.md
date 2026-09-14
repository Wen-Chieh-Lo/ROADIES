# Model and Branch-Length Optimization

This directory separates numerical search from the GPU/tree state needed to
evaluate a candidate model.

## Responsibility Boundary

```text
GlobalModelOptimizer
  numerical parameterization, search, acceptance, outer convergence
        |
        v callbacks
ModelOptimizationBackend
  install candidate model, rebuild PMAT/CLV state, evaluate likelihood,
  invoke the selected branch optimizer
```

`GlobalModelOptimizer` must not know about `MlipperSession`, CUDA buffers, or
resident versus site-batched CLVs. `ModelOptimizationBackend` adapts a supplied
`ModelOptimizationContext`; it does not own the session.

## Files

- `model_parameter_optimizer.hpp/.cpp`: GTR-rate, equilibrium-frequency, and
  Gamma-alpha numerical optimization plus the outer iterative loop.
- `model_optimization_backend.hpp/.cpp`: GPU-aware model installation,
  likelihood evaluation, site batching, and branch-optimizer connection.
- `optimization_types.hpp`: shared constants, typed branch-update choices,
  options, and results used by placement, Local SPR, small-tip, and D&C.

## Key Numerical Functions

- `GlobalModelOptimizer::optimizeSubstitutionRates()`: optimize five relative
  GTR rates while keeping one reference rate fixed.
- `optimizeFrequencies()`: optimize relative frequency parameters and normalize
  them to a valid probability vector.
- `optimizeAlpha()`: bounded one-dimensional optimization of Gamma alpha and
  regeneration of discrete rate categories.
- `optimize()`: alternate enabled parameter groups and branch-length sweeps
  until the likelihood tolerance or maximum rounds is reached.

The internal `evaluateBounded()` and `evaluateOneDimensional()` functions are
C-compatible adapters used by the numerical libraries. They convert raw
parameter arrays/scalars back into the C++ objective stored in their context.
They are not workflow entrypoints.

## Key Backend Functions

- `ModelOptimizationBackend::evaluate()`: evaluate the currently installed
  model using resident or site-batched CLVs.
- `installSubstitutionModel()`: update frequencies/rates, rebuild the GTR Q
  matrix and eigendecomposition, upload model data, rebuild PMATs, and refresh
  CLVs when requested.
- `installGammaAlpha()`: rebuild Gamma rates and dependent GPU model state.
- `optimizeBranchLengths()`: dispatch to resident sequential or site-batched
  sequential full-tree branch optimization.
- `callbacks()`: bind these operations into the narrow callback interface used
  by `GlobalModelOptimizer`.

## Site-Batched Workspace

- `chooseOptimizationSiteBatchSize()`: estimate bytes per site and select a
  batch that fits the target/free-memory budget.
- `RootSiteBatchWorkspace::evaluate()`: copy successive site ranges into a
  reusable upward-only workspace and sum their root likelihoods.
- `optimizeSequentialBranchNewtonSweeps()`: accumulate edge derivatives across
  site batches, update one edge, and continue in coordinate order.

`OptimizationWorkspaceKind` distinguishes global-parameter evaluation from the
larger all-branch-gradient workspace.

## Typed Branch Options

- `EdgeUpdateScheme`: Jacobi or sequential/Gauss-Seidel updates.
- `ClvRetention`: rebuild all CLVs or preserve installed tip CLVs for virtual
  D&C boundary tips.
- `AcceptanceScope`: validate likelihood on a local subtree or the full tree.
- `BranchOptimizationOptions`: shared run-time policy.
- `BranchLengthOptimizationResult`: likelihood, timing, attempt, and acceptance
  report returned to workflows.

When changing acceptance tolerances or branch bounds, update
`optimization_types.hpp` instead of introducing a local constant in a CUDA
file. When changing the optimizer parameterization, consult the design origin
recorded in
`docs/reference/model_and_branch_optimization_reference.md`.
