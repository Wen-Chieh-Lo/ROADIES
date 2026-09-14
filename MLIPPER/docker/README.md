# Docker Images

This directory contains reproducible CUDA build/runtime environments. Docker
does not change MLIPPER's workflow semantics; it supplies the compiler,
libraries, executable, and GPU visibility.

## Files

- `Dockerfile`: the general development/full image used by broader MLIPPER
  experiments.
- `Dockerfile.runtime`: the smaller multi-stage runtime image. The builder
  compiles `USE_DOUBLE=1`; the runtime stage contains only MLIPPER and required
  shared libraries.

Use the ROADIES image for the maintained per-gene integration unless an
experiment explicitly requires tools from the full image.

## Build

From the repository root:

```sh
docker build -f docker/Dockerfile.runtime -t mlipper:runtime .
```

The build context must be the MLIPPER repository root. It includes the pinned
DIPPER source under `third_party/dipper`; no sibling checkout is required.

## Run and GPU Visibility

Manual selection:

```sh
docker run --rm --gpus 'device=0' mlipper:runtime --help
```

Automatic admission needs every candidate GPU visible and shared host IPC:

```sh
docker run --rm --gpus all --ipc=host mlipper:runtime ... --gpu-auto
```

`--ipc=host` lets separate MLIPPER containers see the same POSIX shared-memory
reservation tables. Without it, containers can independently admit themselves
to the same GPU and defeat cross-process accounting.

## When Editing a Dockerfile

- Keep CUDA development packages in the builder and runtime libraries in the
  final stage.
- Keep the production build in double precision.
- Update both images when a required runtime shared library changes.
- Test `MLIPPER --help` and one real GPU workflow from the built image.
- The canonical ROADIES invocation logic is in
  `scripts/run_single_gene_MLIPPER.sh`; do not duplicate its mount rules here.
