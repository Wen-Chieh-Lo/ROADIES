# Runtime Wrapper

This directory contains the maintained wrapper around one MLIPPER invocation.
Multi-gene scheduling, dataset-specific experiment drivers, species-tree
analysis, and downstream metrics belong in ROADIES or an experiment repository.

## `run_single_gene_MLIPPER.sh`

This is the ROADIES-facing per-gene entrypoint. It:

- validates the reference/query alignments, backbone tree, model, and output;
- runs either the local MLIPPER binary or the runtime Docker image;
- maps host input/output paths into one container mount;
- forwards explicit `--gpu-id` or MLIPPER-managed `--gpu-auto` selection;
- enables Local SPR by default;
- keeps final model and global branch-length optimization opt-in for ROADIES
  parity.

Read the complete interface with:

```sh
scripts/run_single_gene_MLIPPER.sh --help
```

ROADIES may launch multiple copies of this wrapper. Device selection and
cross-process memory admission remain MLIPPER responsibilities when
`--gpu-auto` is used.

The wrapper does not install dependencies, build MLIPPER, schedule a collection
of genes, run ASTRAL, or calculate RF/nRF.
