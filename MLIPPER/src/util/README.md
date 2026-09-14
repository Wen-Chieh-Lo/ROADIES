# Shared Utilities

This directory contains reusable parsing, preprocessing, size-checking, and
low-level definitions used across MLIPPER. New workflow orchestration should
not be added here.

## Files

- `precision.hpp`: compile-time floating-point aliases and host/device math
  wrappers.
- `checked_size.hpp`: checked size arithmetic for host and device allocations.
- `model_utils.hpp/.cpp`: model parsing, normalization, and validation helpers.
- `msa_preprocess.hpp/.cpp`: alignment compatibility checks and repeated-site
  compression.
- `mlipper_util.h`: legacy shared CUDA structures and utility primitives that
  still have multiple low-level consumers.

## Precision Contract

`USE_DOUBLE=1` defines `MLIPPER_USE_DOUBLE`, making:

- `fp_t = double`
- `fp2_t = double2`
- `fp4_t = double4`

The experimental float build uses the corresponding float types. Use helpers
such as `fp_exp`, `fp_log`, `fp_fma`, `fp_dot4`, and `make_fp4` in shared
host/device code instead of scattering precision-specific branches.

The production and regression configuration is double precision. A float result
must not silently replace a double-precision golden.

## `mlipper_util.h`

This header includes shared operation descriptors and CUDA helper code used by
tree traversal, placement, likelihood, and derivative kernels. Before moving or
deleting a type, search both `.cpp` and `.cu` consumers.

Keep additions narrowly scoped:

- put GPU admission and ownership in `gpu/`;
- put branch/model policy in `optimize/optimization_types.hpp`;
- put tree representations in `tree/tree.hpp`;
- put algorithm-specific helpers in their algorithm directory.

Because this header is widely included, adding heavyweight dependencies or
non-inline definitions can increase build time or cause duplicate symbols.
