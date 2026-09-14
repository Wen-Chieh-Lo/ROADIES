# GPU Utilities

This directory manages GPU admission and small pieces of CUDA memory ownership.
It does not implement phylogenetic kernels; those live in `tree/`,
`likelihood/`, `placement/`, and `pmatrix/`.

## Files

- `gpu_admission.hpp/.cpp`: choose a CUDA device, estimate memory demand, and
  coordinate multiple MLIPPER processes through shared reservations.
- `device_buffer.hpp`: minimal move-only RAII wrapper around one `cudaMalloc`
  allocation.

## GPU Selection

`MlipperGpuAcquireMode` has three modes:

- `UseCurrentDevice`: use the current CUDA device without shared admission.
- `AdmitSpecificDevice`: wait until a requested visible device is admissible.
- `AutoAdmitAnyVisible`: inspect every visible device and reserve the one with
  the lowest projected reserved-memory ratio.

The main entrypoint is:

```cpp
DeviceReservation select_device_or_wait_or_throw(
    const MlipperGpuConfig&, int estimated_process_memory_mb);
```

Auto selection rejects a GPU when its SM-utilization threshold is exceeded or
when the estimated process memory plus safety margin would exceed its memory
budget. Selection and reservation are serialized briefly so concurrent jobs do
not all choose the same snapshot.

## Key Functions

- `estimate_mlipper_gpu_process_memory_mb()`: estimate a resident small-tip
  tree, queries, CLVs, PMATs, scalers, and placement scratch buffers.
- `estimate_divide_and_conquer_gpu_process_memory_mb()`: estimate one bounded
  D&C subtree plus Local SPR workspace.
- `estimate_dipper_and_divide_and_conquer_gpu_process_memory_mb()`: reserve the
  larger of the known D&C estimate and the conservative DIPPER fallback.
- `select_device_or_wait_or_throw()`: apply the requested acquisition mode,
  wait when capacity is temporarily unavailable, and return an RAII handle.
- `ensure_reservation_capacity_or_wait()`: grow an existing reservation when a
  small-tip tree requires more capacity; it never drops the original
  reservation before the larger one is accepted.
- `current_device_or_throw()`, `set_device_or_throw()`, and
  `current_device_properties_or_throw()`: checked CUDA runtime wrappers.

## Reservation Lifetime

`DeviceReservation` is move-only. Moving it transfers responsibility for the
shared admission entry. `reset()` or destruction removes the entry. The entry
records PID and Linux process start time so stale entries and PID reuse can be
distinguished.

The reservation is accounting, not a CUDA-driver memory guarantee. Every real
allocation must still check `cudaMalloc` failures. Programs that do not use
MLIPPER admission can consume memory after a reservation is granted.

## `DeviceBuffer<T>`

Use `DeviceBuffer<T>` for a simple resizable scratch allocation:

- `ensureCapacity(count)` keeps an existing large-enough buffer or reallocates.
- `get()` exposes a non-owning raw pointer to kernels.
- `reset()` releases the CUDA allocation.
- copying is disabled; moving transfers ownership.

Do not embed this owner inside `DeviceTree`. Kernel-facing `DeviceTree` must
remain trivially copyable and contains only non-owning pointers.

## Runtime Controls

- `MLIPPER_GPU_AUTO_SM_UTILIZATION_MAX_PCT` (default `90`)
- `MLIPPER_GPU_AUTO_MEMORY_BUDGET_FRACTION` (default `0.85`)
- `MLIPPER_GPU_AUTO_MEMORY_SAFETY_MB` (default `4096`)
- `MLIPPER_GPU_AUTO_POLL_MS` (default `1000`)

When changing accounting, update `tests/gpu_admission_test.cpp` and run a
concurrent `--gpu-auto` workflow test on a multi-GPU host.
