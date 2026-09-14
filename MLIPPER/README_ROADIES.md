# MLIPPER For ROADIES

This document describes the current MLIPPER interface for the ROADIES
per-gene tree stage.

The intended use is simple:

1. ROADIES prepares one gene’s inputs.
2. ROADIES calls one wrapper script.
3. MLIPPER writes one committed gene tree.

## What ROADIES Should Use

For ROADIES, the intended entrypoint is:

- `scripts/run_single_gene_MLIPPER.sh`

The intended Docker image for that wrapper is:

- `wenchiehlo/mlipper-roadies:latest`

ROADIES does not need to call the `MLIPPER` binary directly unless you want to
debug the wrapper.

There are two supported execution modes:

- Docker mode
  This is the default. ROADIES runs `scripts/run_single_gene_MLIPPER.sh`, the
  wrapper starts the Docker image, and the MLIPPER binary comes from inside the
  image.

- Host mode
  ROADIES passes `--no-docker`. The wrapper runs a local `MLIPPER` binary from
  the host. MLIPPER and its runtime libraries must already be installed.

This setup is currently validated on `peregrine`. The Docker image builds the
pinned libpll-2 revision; host mode requires the same compatible API.

## Architecture

The Docker-mode architecture has four layers:

1. `ROADIES`
   ROADIES prepares one gene’s files and launches independent jobs.

2. `run_single_gene_MLIPPER.sh`
   This is a thin wrapper. It validates input paths, maps host paths into the
   container, exposes the requested GPU set, and translates the per-gene
   contract into one MLIPPER invocation.

3. `wenchiehlo/mlipper-roadies:latest`
   This image provides the runtime environment and the compiled `MLIPPER`
   binary.

4. `MLIPPER`
   The binary reads the reference MSA, query MSA, backbone tree, and bestModel
   file, then writes one committed gene tree.

In other words, ROADIES should think of MLIPPER as:

- one per-gene wrapper
- one per-gene output tree

not as a larger batch orchestration system.

The host-mode architecture replaces the Docker image layer with host
`MLIPPER`:

- host `MLIPPER`
  The wrapper runs the local binary directly. By default this is
  `REPO_ROOT/MLIPPER`, or the path passed via `--local-mlipper`.

## Input Contract

Per gene, MLIPPER expects these four core inputs:

- reference MSA: `ref.fa`
- query MSA: `query.fa`
- backbone tree: one `*.raxml.bestTree`
- per-gene model file: one `*.raxml.bestModel`

What each file means:

- `ref.fa`
  The reference or backbone alignment. These taxa are already present in the
  backbone tree.

- `query.fa`
  The query alignment. These taxa are the ones MLIPPER will commit back into
  the tree.

- `*.raxml.bestTree`
  The backbone topology for that gene.

- `*.raxml.bestModel`
  The per-gene model description. MLIPPER reads this file directly via
  `--best-model`.

Important limitation:

- MLIPPER expects split reference/query alignments.
- If ROADIES only has one combined full alignment, ROADIES needs an adapter
  step to split it before calling MLIPPER.
- In Docker mode, all per-gene input files and the output path should share a
  reasonably small common parent directory. The wrapper mounts that common
  parent into the container as `/workspace/job`.
- Recommended layout: keep `ref.fa`, `query.fa`, the backbone tree, the
  `bestModel`, and the output tree under the same per-gene or per-job directory.
- Avoid spreading inputs and outputs across unrelated filesystem roots, because
  that can force the wrapper to mount an overly broad parent such as `/`.

## Output Contract

The main output is:

- one committed final gene tree in Newick format

The current wrapper writes it to whatever path ROADIES passes as:

- `--out-tree`

The common filename used in this repo is:

- `mlipper_gene_tree.nwk`

ROADIES downstream should treat that tree as the main artifact from this
stage.

## Wrapper Interface

The wrapper takes these required arguments:

- `--ref-msa`
- `--query-msa`
- `--backbone-tree`
- `--best-model`
- `--out-tree`

Required argument meanings:

- `--ref-msa`
  Path to the reference/backbone alignment.

- `--query-msa`
  Path to the query alignment.

- `--backbone-tree`
  Path to the backbone Newick tree.

- `--best-model`
  Path to the per-gene `bestModel` file.

- `--out-tree`
  Path where the committed output tree should be written.

Optional wrapper arguments:

- `--docker-image`
- `--gpu-id`
- `--docker-gpus`
- `--no-docker`
- `--local-mlipper`
- `--no-local-spr`
- `--final-optimization`
- `--batch-size`
- `--local-spr-radius`
- `--local-spr-rounds`

Optional argument meanings:

- `--docker-image`
  Override the Docker image tag. The wrapper default is
  `wenchiehlo/mlipper-roadies:latest`.

- `--gpu-id`
  GPU id used when `--docker-gpus` is not provided.

- `--docker-gpus`
  Raw Docker `--gpus` specification. This overrides `--gpu-id`.

- `--no-docker`
  Run host `MLIPPER` directly instead of Docker.

- `--local-mlipper`
  Override the local `MLIPPER` binary path used with `--no-docker`. The default
  is `REPO_ROOT/MLIPPER`.

- `--no-local-spr`
  Disable local SPR refinement. The wrapper enables it by default.

- `--final-optimization`
  Enable final model-parameter optimization and full-tree branch-length
  optimization. These stages are disabled by default in the ROADIES wrapper
  to preserve the signed-off per-gene workflow. Direct MLIPPER invocations
  retain their own defaults.

- `--batch-size`
  Batch insert size passed to MLIPPER when local SPR is enabled.

- `--local-spr-radius`
  Local SPR radius.

- `--local-spr-rounds`
  Number of local SPR rounds.

## What The Wrapper Actually Runs

In Docker mode, the wrapper runs Docker and launches image-internal `MLIPPER` at:

- `/workspace/MLIPPER/MLIPPER`

The wrapper mounts the input/output path root into `/workspace/job`. It does not
mount the host repo over `/workspace/MLIPPER`, so the binary comes from the
Docker image.

In host mode, the wrapper runs the local binary directly:

- default: `REPO_ROOT/MLIPPER`
- override: `--local-mlipper PATH`

Before host execution, the wrapper requires an already-built executable. It
does not modify the host or install dependencies.

At minimum, it forwards these MLIPPER arguments:

- `--tree-alignment`
- `--query-alignment`
- `--tree`
- `--best-model`
- `--commit-to-tree`

If local SPR is enabled, it also forwards:

- `--batch-insert-size`
- `--local-spr-radius`
- `--local-spr-rounds`

Local SPR is enabled by default in MLIPPER's committed-tree workflow. If it is
disabled, the wrapper forwards `--no-local-spr` instead.

By default, the ROADIES wrapper also forwards:

- `--no-model-optimization`
- `--no-global-branch-optimization`

Pass `--final-optimization` to opt into both final optimization stages. This
wrapper-specific default preserves parity with the signed-off ROADIES gene-tree
workflow and does not change the direct MLIPPER or D&C defaults.

In Docker mode, the wrapper also does two operational things for ROADIES:

- it converts host paths into container paths
- it exposes all GPUs in automatic-admission mode, or the requested GPU in
  manual mode

## GPU Control

The intended automatic-admission split is:

- ROADIES launches independent MLIPPER processes without choosing GPU ids
- the wrapper passes `--gpu-auto`
- MLIPPER selects a visible GPU and maintains the cross-process reservation
- the reservation covers DIPPER starting-tree construction through final
  optimization and is released when the session ends

Recommended usage:

- automatic host mode: use `--gpu-auto --no-docker`
- automatic Docker mode: use `--gpu-auto`; the wrapper exposes all GPUs and
  shares the host IPC namespace so concurrent containers see one reservation
  table
- `--gpu-id N` remains available as a manual override for debugging

MLIPPER itself now exposes:

- `--gpu-id N`
  selects a CUDA device ordinal within the currently visible GPU set and waits
  for that specific GPU to satisfy the same shared-admission thresholds
- `--gpu-auto`
  uses shared GPU admission: it probes visible GPUs and admits the process to
  the device with the lowest projected reserved-memory ratio among those whose
  current SM utilization and memory headroom pass the configured thresholds,
  then waits/retries if none currently fit

The `MLIPPER --gpu-auto` tuning knobs are
  `MLIPPER_GPU_AUTO_SM_UTILIZATION_MAX_PCT`,
  `MLIPPER_GPU_AUTO_MEMORY_BUDGET_FRACTION`,
  `MLIPPER_GPU_AUTO_MEMORY_SAFETY_MB`,
  `MLIPPER_GPU_AUTO_POLL_MS`

## Model Handling

The preferred model path for ROADIES is:

- `--best-model`

Current helper-path support is:

- DNA only
- 4 states only
- `GTR` only

What `--best-model` currently overwrites:

- `--states`
- `--subst-model`
- `--ncat`
- `--alpha`
- `--rates`
- `--freqs` / `--empirical-freqs`

What it does not currently overwrite:

- `--pinv`

Only discrete-Gamma (`+G`) rate heterogeneity is supported. FreeRate (`+R`),
mixture models, and custom `--rate-weights` are rejected.

Current `pinv` note:

- keep `pinv = 0.0` unless nonzero invariant-site behavior has been explicitly
  revalidated end-to-end

## Empirical Frequencies

If the model implies empirical frequencies, MLIPPER estimates them from the
reference alignment.

Current behavior:

- partially informative ambiguity symbols are distributed across represented
  states
- fully ambiguous symbols such as `N`, `-`, `.`, and `?` are distributed
  evenly across all represented states
- if any state would otherwise receive zero mass, MLIPPER applies a tiny
  positive floor before renormalization

## How To Use

### Docker mode

This is the default mode. Use it when ROADIES should run the binary packaged in
the Docker image.

Example per-gene job layout:

```text
GENE/
  ref.fa
  query.fa
  backbone.nwk
  gene.raxml.bestModel
  mlipper_gene_tree.nwk        # written by MLIPPER
```

With that layout, ROADIES calls:

```bash
scripts/run_single_gene_MLIPPER.sh \
  --ref-msa GENE/ref.fa \
  --query-msa GENE/query.fa \
  --backbone-tree GENE/backbone.nwk \
  --best-model GENE/gene.raxml.bestModel \
  --out-tree GENE/mlipper_gene_tree.nwk \
  --gpu-id 0
```

Internally, the wrapper mounts `GENE/` into the container under
`/workspace/job/...` and rewrites the file paths for MLIPPER. ROADIES should
only pass host paths like `GENE/ref.fa`; it should not pass `/workspace/job`
paths directly.

#### 1. Pull or build the image

```bash
docker pull wenchiehlo/mlipper-roadies:latest
```

Or build it from this repo:

```bash
docker build -f docker/Dockerfile.runtime -t wenchiehlo/mlipper-roadies:latest .
```

A clean checkout includes the compatible DIPPER source under
`third_party/dipper`, so the image no longer depends on a sibling checkout.

During image build, Docker does the setup work:

- builder stage installs compile-time dependencies and builds the pinned
  compatible libpll-2 revision
- builder stage runs `make clean && make USE_DOUBLE=1 MLIPPER`
- runtime stage installs BLAS/LAPACK/TBB runtime libraries and copies the
  pinned libpll-2 shared library from the builder
- runtime stage copies the compiled binary into `/workspace/MLIPPER/MLIPPER`

The ROADIES image uses the production double-precision build. A float build is
only for an explicit precision/performance experiment and must use a separate
image tag rather than replacing the production `latest` image.

#### 2. Run one gene

```bash
scripts/run_single_gene_MLIPPER.sh \
  --ref-msa GENE/ref.fa \
  --query-msa GENE/query.fa \
  --backbone-tree GENE/backbone.nwk \
  --best-model GENE/gene.raxml.bestModel \
  --out-tree GENE/mlipper_gene_tree.nwk \
  --gpu-id 0
```

#### 3. Consume the output

ROADIES should use the tree written to:

- `GENE/mlipper_gene_tree.nwk`

or whatever path was passed as `--out-tree`.

Expected success criteria:

- exit code `0`
- non-empty output Newick tree

### Host mode

Use host mode when ROADIES should run a local host `MLIPPER` binary instead of a
Docker image.

#### 1. Prepare the host

Build MLIPPER on a host where CUDA and the documented native dependencies are
already available:

```bash
make -j4 USE_DOUBLE=1 MLIPPER
```

#### 2. Run one gene without Docker

```bash
scripts/run_single_gene_MLIPPER.sh \
  --no-docker \
  --ref-msa GENE/ref.fa \
  --query-msa GENE/query.fa \
  --backbone-tree GENE/backbone.nwk \
  --best-model GENE/gene.raxml.bestModel \
  --out-tree GENE/mlipper_gene_tree.nwk
```

With explicit GPU restriction:

```bash
CUDA_VISIBLE_DEVICES=0 scripts/run_single_gene_MLIPPER.sh \
  --no-docker \
  --ref-msa GENE/ref.fa \
  --query-msa GENE/query.fa \
  --backbone-tree GENE/backbone.nwk \
  --best-model GENE/gene.raxml.bestModel \
  --out-tree GENE/mlipper_gene_tree.nwk
```

With a custom local binary:

```bash
scripts/run_single_gene_MLIPPER.sh \
  --no-docker \
  --local-mlipper /path/to/MLIPPER \
  --ref-msa GENE/ref.fa \
  --query-msa GENE/query.fa \
  --backbone-tree GENE/backbone.nwk \
  --best-model GENE/gene.raxml.bestModel \
  --out-tree GENE/mlipper_gene_tree.nwk
```

Expected success criteria:

- exit code `0`
- non-empty output Newick tree

## Current Limitations

- the helper path assumes split reference/query alignments
- the helper path assumes DNA with 4 states
- the helper path assumes DNA `GTR`
- amino-acid and non-GTR models are not supported in the current helper path
- `--best-model` does not currently import `pinv`; use `pinv = 0.0` unless that
  path is explicitly revalidated
- MLIPPER uses one selected GPU per invocation; ROADIES or the wrapper controls
  device visibility, while MLIPPER admission checks live utilization and VRAM
  headroom before starting work
- the image builds a pinned libpll-2 revision instead of depending on the older
  distro `libpll-dev`/`libpll0` packages
- host mode requires a working host CUDA toolkit, compatible libraries, and a
  prebuilt `MLIPPER` executable
- the current ROADIES setup is validated on `peregrine`; if ROADIES is moved
  to a different host environment, the image should be re-smoke-tested there
- the ROADIES image and wrapper are intended for per-gene execution
- the current interface guarantees the committed output tree
