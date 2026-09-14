# MLIPPER

MLIPPER is a GPU phylogenetics program with two maintained workflows:

- **Small-tip placement:** add query taxa to an existing backbone tree, refine
  the affected neighborhoods with Local SPR, then optimize model parameters
  and full-tree branch lengths.
- **Divide and conquer (D&C):** ask DIPPER to construct a starting tree, split
  the tree into bounded sectors, run boundary-aware NNI, then optimize model
  parameters and branch lengths with site batching and sector partitioning.

MLIPPER produces a gene tree (or a placement-only jplace file). It does not
combine gene trees into a species tree and it does not calculate normalized
Robinson-Foulds (nRF) distance. Those are downstream evaluation steps, such as
the ones performed by ROADIES.

For the ROADIES per-gene interface, see
[`README_ROADIES.md`](README_ROADIES.md).

## Motivation and Design Lineage

Large gene-tree workflows face a recurring tradeoff. Distance-based methods
can construct or extend trees quickly, but their topology and branch lengths
are not optimized under the same evolutionary likelihood model used by later
phylogenetic analysis. A conventional whole-tree maximum-likelihood search can
perform that refinement, but repeatedly running it for many large gene trees
is expensive in both time and memory.

MLIPPER bridges these two stages. It starts from a tree that is already cheap
to obtain—either a ROADIES backbone with missing taxa or a tree constructed by
DIPPER—and spends likelihood computation only where it is useful. Query taxa
are placed and committed in small batches, nearby topology is repaired with
Local SPR, and large de novo trees are refined as bounded NNI sectors. Model
parameters and all branch lengths can then be optimized without handing the
tree to a separate program. GPU CLVs, transition matrices, and site batching
make this iterative workflow practical within one process.

MLIPPER draws on several earlier lines of work:

| Earlier work | Idea carried into MLIPPER | Important difference in MLIPPER |
| --- | --- | --- |
| [DIPPER](https://doi.org/10.1101/2025.08.12.669583) | GPU distance calculation, placement-based construction, and divide-and-conquer starting trees | DIPPER is directly integrated to propose the starting topology; MLIPPER subsequently evaluates and refines it with sequence likelihood |
| [EPA-ng and evolutionary placement](https://doi.org/10.1093/sysbio/syy054) | Evaluate where an aligned query fits on a reference tree and report likelihood weight ratios | MLIPPER can emit compatible placement output, but its gene-tree workflow also commits queries sequentially and allows later topology refinement |
| [MAPLE](https://doi.org/10.1038/s41588-023-01368-0) and [UShER/matOptimize](https://doi.org/10.1093/bioinformatics/btac401) | Build or update a large tree by combining fast placement with local SPR-style improvement | These methods target densely sampled, closely related genomes using specialized approximate-likelihood or parsimony representations; MLIPPER uses dense GTR+Gamma likelihood CLVs for aligned gene data |
| [Sectorial search](https://doi.org/10.1111/j.1096-0031.1999.tb00278.x) | Reoptimize bounded parts of a large tree instead of searching the full topology at once | MLIPPER preserves the omitted tree context with directional boundary CLVs and accepts NNI changes using likelihood rather than the original parsimony objective |
| [RAxML-NG](https://doi.org/10.1093/bioinformatics/btz305) and libpll-style likelihood computation | Felsenstein pruning, GTR parameterization, Gamma rates, and iterative branch-length optimization | MLIPPER provides its own CUDA execution, state lifecycle, acceptance checks, and restricted supported-model surface; it is not a general RAxML-NG replacement |

The resulting design is intentionally hybrid:

```text
fast distance/backbone construction
  -> likelihood-based query placement
  -> local or sector topology repair
  -> model and whole-tree branch-length optimization
```

The main lesson from the earlier work is that scalability comes from limiting
the search and reusing tree state, while statistical consistency requires
evaluating accepted changes under one explicit likelihood model. MLIPPER
therefore uses fast methods to choose where to search, but rebuilt likelihood
state—not distance, parsimony, or an approximate local score—as the final
acceptance criterion.

## Vocabulary

You only need a few terms to start reading the code:

- **reference/backbone taxa:** taxa already present in the input tree
- **query taxa:** taxa that small-tip placement adds to the backbone
- **placement:** score candidate tree edges for a query
- **commit:** modify the tree by attaching a query to its selected edge
- **SPR:** subtree-prune-and-regraft; the local topology refinement used after
  small-tip commits
- **NNI:** nearest-neighbor interchange; the topology move used by D&C sectors
- **model optimization:** update supported GTR+G parameters
- **branch optimization:** update branch lengths without changing topology

## Choose the Workflow

| Goal | Required selector/output | Result |
| --- | --- | --- |
| Add a small set of taxa to a known tree | `--commit-to-tree OUT.nwk` | committed tree, Local SPR and final optimization by default |
| Score placements without changing the tree | `--jplace-out OUT.jplace` | EPA-ng-compatible accumulated-LWR-filtered placements |
| Build and refine a full tree in sectors | `--divide-and-conquer` | DIPPER starting tree, sector NNI, site-batched/sector final optimization |

There is one executable, `MLIPPER`. Without `--divide-and-conquer`, the CLI
uses the small-tip/placement parser.

## First-Day Setup

### 1. Check out MLIPPER

The compatible DIPPER source used for starting-tree construction is vendored
under `third_party/dipper`. A clean MLIPPER checkout therefore contains all
project source required by the default build.

### 2. Check the machine

The native build path requires a Linux machine with an NVIDIA GPU and an
existing CUDA toolkit. Verify CUDA before building:

```bash
nvidia-smi
nvcc --version
```

The build also needs a C++17 compiler, CMake, Git, Boost Program Options, TBB,
BLAS/LAPACK, zlib, and a compatible libpll-2. The older Ubuntu `libpll-dev`
package does not provide the rooted-tree API used by MLIPPER. The reproducible
dependency versions are defined by `docker/Dockerfile`; Coraxlib is cloned at
the revision pinned in the `Makefile` during the first build.

### 3. Build the production configuration

On a configured host, the Makefile is the only build entrypoint:

```bash
make -j4 MLIPPER
./MLIPPER --help
```

The production and default configuration is `USE_DOUBLE=1`. A float build is
available only for explicit precision/performance experiments:

```bash
make clean
make -j4 USE_DOUBLE=0 MLIPPER
```

`CUDA_HOME` defaults to `/usr/local/cuda-12`. Override build paths when the
machine differs:

```bash
make -j4 MLIPPER \
  CUDA_HOME=/path/to/cuda \
  PLL_INC_DIR=/path/to/include \
  PLL_LIB_DIR=/path/to/lib
```

`DIPPER_DIR` may still be overridden for an explicit upstream compatibility
experiment, but production builds use the pinned vendored copy.

### 4. Run the CPU regression tests

```bash
make test
```

These host unit regressions cover alignment preprocessing, model parsing,
input validation, tree/jplace I/O, topology-only SPR/NNI behavior, and the
workflow-output comparator. They do not initialize CUDA and are the fastest
check after an ordinary edit.

The repository also includes a six-taxon GPU integration fixture:

```bash
make test-gpu-integration
```

It exercises placement-only and D&C, including accepted NNI replay and final
optimization. It is an integration guard, not a scientific benchmark.

Larger real-data datasets and scientifically approved golden outputs remain
external. Obtain those paths from the project dataset location before the
external workflow sign-off described in [`tests/README.md`](tests/README.md).

At the end of the first setup session, a new developer should be able to check
all of the following:

- explain whether a requested change belongs to small-tip, placement-only, or
  D&C
- build the default double-precision binary
- run all CPU tests
- print the correct CLI help for both the default and D&C parsers
- locate the integrated DIPPER source and its upstream synchronization policy
- locate the approved real-data inputs and goldens, even if no GPU is currently
  free

Use these help commands to verify both CLI surfaces without starting a GPU
workflow:

```bash
./MLIPPER --help
./MLIPPER --divide-and-conquer --help
```

## Input and Output Contract

Paths may be absolute or relative to the directory from which `MLIPPER` is
invoked. Input files must already exist. The parent directory of an output file
must already exist and be writable; an existing output file is overwritten.

### Input files

| CLI input | Format and meaning | Small-tip commit | Placement only | D&C |
| --- | --- | :---: | :---: | :---: |
| `--tree-alignment REF.fa` | Aligned reference taxa in FASTA, sequential PHYLIP, or interleaved PHYLIP format | required | required | required as the full-taxon alignment |
| `--query-alignment QUERY.fa` | Aligned taxa to score or add; defaults to `--tree-alignment` when omitted | required in normal split-alignment use | required in normal split-alignment use | not accepted |
| `--tree BACKBONE.nwk` | Newick file containing the reference/backbone topology | required, unless `--tree-newick` is used | required, unless `--tree-newick` is used | not accepted |
| `--tree-newick '(A:0.1,B:0.1);'` | Inline Newick alternative to `--tree`; the two flags are mutually exclusive | optional alternative | optional alternative | not accepted |
| `--best-model GENE.raxml.bestModel` | RAxML-NG `bestModel` file used to configure GTR+Gamma | preferred | preferred | required |

For the two small-tip modes, `REF.fa` contains the taxa already present in
`BACKBONE.nwk`, while `QUERY.fa` contains the taxa being placed. The following
invariants are checked before GPU initialization:

- every sequence within an alignment has the same nonzero site count;
- reference and query alignments have the same number of aligned sites;
- sequence names are nonempty and unique within each alignment;
- taxon names in the reference alignment and backbone tree match exactly;
- supported DNA symbols must map through libpll's DNA alphabet;
- in commit mode, query names must not overlap reference names.

D&C uses `--tree-alignment` differently: it is one alignment containing every
taxon for the output tree. No input tree or query alignment is supplied,
because the selected DIPPER mode constructs the starting topology.

The maintained model is DNA GTR with discrete-Gamma rate categories:

- exactly 4 states
- GTR substitution model
- 1--8 discrete-Gamma categories with equal category weights
- `pinv = 0`

FreeRate (`+R`), mixture models, custom rate-category weights, invariant-site
likelihoods, amino-acid models, and non-GTR models are rejected rather than
silently approximated. `--best-model` is the preferred production interface;
explicit `--states`, `--subst-model`, `--ncat`, `--alpha`, `--freqs`,
`--empirical-freqs`, and `--rates` remain available within the supported model.

### Output files

| Workflow selector | File written | Contents |
| --- | --- | --- |
| `--commit-to-tree OUT.nwk` | `OUT.nwk` | Final Newick tree after query commits, Local SPR unless disabled, and requested final optimization. Numerically negligible internal branches below `1e-6` are collapsed during serialization. |
| `--jplace-out OUT.jplace` | `OUT.jplace` | EPA-ng-compatible JSON placement records on the unchanged backbone. Candidate rows are retained through the accumulated-LWR threshold controlled by `--filter-acc-lwr` (default `0.99`). |
| `--divide-and-conquer --write-tree OUT.nwk` | `OUT.nwk` | Final Newick tree after DIPPER construction, sector NNI, model optimization, and sector-partitioned branch-length optimization. |

`--commit-to-tree` and `--jplace-out` are mutually exclusive. In D&C,
`--write-tree` is optional at the parser level, but omitting it means no tree
artifact is saved. Likewise, a small-tip invocation without either output flag
may perform placement work but writes no result file. For reproducible runs,
always specify the output flag shown for the intended workflow.

MLIPPER also writes progress, likelihood audits, GPU admission information, and
the final output path to the terminal. These log messages are diagnostics, not
a stable machine-readable output format. Redirect them explicitly when an
experiment needs a retained log:

```bash
./MLIPPER ... --commit-to-tree OUT.nwk > run.log 2>&1
```

## Run Small-Tip Placement and Commit

```bash
./MLIPPER \
  --tree-alignment REF.fa \
  --query-alignment QUERY.fa \
  --tree BACKBONE.nwk \
  --best-model GENE.raxml.bestModel \
  --commit-to-tree OUT.nwk \
  --gpu-id 0
```

The default committed-tree workflow is:

```text
parse and validate inputs
  -> compress repeated alignment columns
  -> load the backbone into MlipperSession
  -> place and commit queries in batches of 5
  -> run Local SPR after each batch
  -> optimize GTR+G parameters
  -> optimize full-tree branch lengths
  -> write Newick
```

Useful tuning flags are:

```text
--batch-insert-size N
--local-spr-radius N
--local-spr-cluster-threshold N
--local-spr-rounds N
```

Local SPR and both final optimization stages are production defaults. The
following flags are intended for ablations:

```text
--no-local-spr
--no-model-optimization
--no-global-branch-optimization
```

The ROADIES wrapper intentionally differs here: it disables both final
optimization stages by default to reproduce the signed-off per-gene workflow.
Pass `--final-optimization` to the wrapper to opt in.

## Run Placement Only

```bash
./MLIPPER \
  --tree-alignment REF.fa \
  --query-alignment QUERY.fa \
  --tree BACKBONE.nwk \
  --best-model GENE.raxml.bestModel \
  --jplace-out placements.jplace \
  --filter-acc-lwr 0.99 \
  --gpu-id 0
```

For each query, MLIPPER computes likelihood weight ratios (LWRs), orders
candidates by decreasing LWR, and writes the smallest prefix whose accumulated
LWR reaches the threshold. The default threshold is `0.99`. This path does not
commit queries, run Local SPR, or perform final tree optimization.

## Run Divide and Conquer

```bash
./MLIPPER \
  --divide-and-conquer \
  --tree-alignment FULL.fa \
  --best-model GENE.raxml.bestModel \
  --dipper-starting-tree-mode nj-placement \
  --write-tree OUT.nwk \
  --gpu-id 0
```

`--dipper-starting-tree-mode` accepts `nj-placement` (default) or
`divide-and-conquer`. This flag changes how DIPPER constructs the starting
tree; MLIPPER's later sector refinement is the same.

The D&C workflow is:

```text
parse and validate the full alignment
  -> build a DIPPER starting tree
  -> compress repeated alignment columns
  -> initialize boundary-aware D&C GPU state
  -> partition internal edges into sectors
  -> run NNI over each sector
  -> run site-batched model optimization
  -> run sector-partitioned branch-length optimization
  -> write Newick
```

Scheduler controls are:

```text
--divide-and-conquer-core-edges N
--divide-and-conquer-tip-budget N
--divide-and-conquer-sweeps N
```

Use the defaults until a dataset-specific experiment justifies changing them.

## GPU Selection

Use `--gpu-id N` to wait for one CUDA ordinal in the process-visible device
set, or `--gpu-auto` to admit the job to the visible GPU with the lowest
projected reserved-memory ratio among those with sufficient memory headroom.
The ordinal follows `CUDA_VISIBLE_DEVICES` or Docker
GPU visibility, not necessarily the host-global numbering.

Admission behavior can be tuned with these runtime environment variables:

| Variable | Default | Meaning |
| --- | ---: | --- |
| `MLIPPER_GPU_AUTO_SM_UTILIZATION_MAX_PCT` | `90` | maximum current SM utilization |
| `MLIPPER_GPU_AUTO_MEMORY_BUDGET_FRACTION` | `0.85` | usable fraction of total VRAM |
| `MLIPPER_GPU_AUTO_MEMORY_SAFETY_MB` | `4096` | additional reserved headroom |
| `MLIPPER_GPU_AUTO_POLL_MS` | `1000` | retry interval when no GPU is admissible |

MLIPPER estimates its own memory requirement from the tree, alignment, query
count, precision, and workspace layout. ROADIES only needs to expose the
candidate GPU set; `--gpu-auto` selects and reserves a device within that set.

## How the Code Is Called

`src/main.cpp` is the executable entrypoint. `MlipperSession` is the shared
stateful engine, not a second entrypoint.

Small-tip commit:

```text
main
  -> cli::loadSmallTipConfigFromCommandLine
  -> MlipperSession::loadBackboneTree/loadAlignment/loadModel
  -> MlipperSession::initializeCPU
  -> MlipperSession::runSmallTipBatches
       -> placement and commit
       -> Local SPR
  -> MlipperSession::runFinalModelOptimization
  -> MlipperSession::writeTree
```

D&C:

```text
main
  -> cli::loadDivideAndConquerConfigFromCommandLine
  -> gpu::select_device_or_wait_or_throw
  -> workflow::buildDipperStartingTree
  -> preprocess_alignments
  -> MlipperSession::initializeDivideAndConquerGPUWithReservation
  -> MlipperSession::runDivideAndConquerNNI
  -> MlipperSession::runDivideAndConquerFinalOptimization
  -> MlipperSession::writeTree
```

The session owns workflow lifetime and GPU allocations. `DeviceTree` is a
non-owning CUDA descriptor passed to kernels; do not add allocation ownership
to it. Stable node labels survive topology edits, while node IDs are current
array indices and may change when the tree is rebuilt.

## Recommended Reading Order

Do not start by reading `mlipper_session.cpp` from top to bottom. Trace one
workflow from its public boundary and only descend into the implementation you
need:

1. [`src/README.md`](src/README.md) — see the source map and both end-to-end
   call flows.
2. `src/main.cpp` — see the complete order of both maintained workflows.
3. [`src/io/README.md`](src/io/README.md) and `src/io/CLI.hpp` — learn the typed
   configuration passed into each workflow.
4. The public section of `src/mlipper_session.hpp` — learn the supported engine
   operations and their required order.
5. [`src/tree/README.md`](src/tree/README.md) and `src/tree/tree.hpp` — learn the
   CPU tree, stable labels, GPU descriptor, and workspace ownership vocabulary.
6. `src/mlipper_session.cpp` — search for the one public operation being
   changed, then follow only the helpers it calls.
7. Enter a component through its folder README only when the change belongs to
   that layer: `placement/`, `spr/`, `optimize/`, `likelihood/`, `gpu/`, or
   `pmatrix/`.
8. Use [`docs/README.md`](docs/README.md) to find the relevant design baseline,
   performance evidence, or historical experiment only after locating the
   current implementation path.

For a first code-reading exercise, trace the small-tip command from
`runSmallTipWorkflow()` to `runSmallTipBatches()`, then find where a placement
becomes a committed attachment node and where Local SPR is invoked. For D&C,
trace `runDivideAndConquerWorkflow()` through starting-tree construction,
sector NNI, and final optimization. This provides the architecture without
requiring a line-by-line reading of the session implementation.

## Where to Make a Change

| Change | Start here |
| --- | --- |
| CLI flag, defaults, or validation | `src/io/CLI.cpp`, `src/io/input_validation.*` |
| Workflow order or output dispatch | `src/main.cpp` |
| Shared state, ownership, and lifecycle | `src/mlipper_session.*` |
| Alignment parsing or repeated-column compression | `src/io/parse_file.*`, `src/util/msa_preprocess.*` |
| Tree parsing, topology, packing, or CUDA upload | `src/tree/` |
| Candidate-edge scoring and placement branch lengths | `src/placement/` |
| Local SPR scoring/topology | `src/spr/local_spr*` |
| Sector NNI behavior | `src/spr/nni.*`, `src/tree/divide_and_conquer.*` |
| Model and branch-length optimization | `src/optimize/`, optimization methods in `src/mlipper_session.cpp` |
| Likelihood CUDA kernels | `src/likelihood/` |
| Transition matrices/eigendecomposition | `src/pmatrix/` |
| Newick or jplace output | `src/io/tree_newick.*`, `src/io/jplace.*` |
| DIPPER adapter | `src/workflow/dipper_starting_tree.*` |
| Multi-process GPU admission | `src/gpu/gpu_admission.*` |
| ROADIES per-gene invocation | `scripts/run_single_gene_MLIPPER.sh`, `README_ROADIES.md` |

## Development Workflow

Use this loop for normal changes:

1. Identify which maintained workflow is affected.
2. Make the smallest change at the layer listed above.
3. Build with `make -j4 MLIPPER`; generated `.d` files track header
   dependencies automatically.
4. Run `make test`.
5. Run `make test-gpu-integration` when a CUDA GPU is available.
6. Run `./MLIPPER --help` if CLI parsing changed.
7. Run the affected real-data GPU command and compare against an approved
   golden output before sign-off.
8. Run both small-tip and D&C real-data regressions when changing shared session,
   tree, likelihood, model, PMAT, or GPU-resource code.

Use `make DEBUG=1 MLIPPER` for a debug build and:

```bash
make cuda-gdb RUN_ARGS='...'
```

for CUDA debugging. Use a clean rebuild when changing precision, CUDA
architecture flags, compiler/toolkit versions, or external dependency paths.

The refactoring stop line is semantic clarity and tested ownership. Do not
split a session operation into another file merely to reduce line count. A
refactor is justified when it removes dead code, eliminates duplicated logic,
clarifies ownership/coordinates, creates an independently testable boundary,
or fixes a demonstrated reliability problem.

For a first contribution, prefer a parser/validation fix or a CPU regression
test. Changes to placement scoring, CLV/scaler layout, topology commits,
boundary messages, or optimization acceptance require both workflow GPU
regressions and should not be used as an onboarding exercise.

## Tests and Sign-Off

`make test` is required for every change. GPU workflow regression is opt-in
because the dataset and approved golden outputs are external; setup and exact
comparison rules are in [`tests/README.md`](tests/README.md).

For algorithm changes, record at least:

- command and Git commit
- dataset/gene/iteration identity
- precision and visible GPU
- output tree or jplace artifact
- initial/final likelihood where applicable
- topology comparison (RF/nRF) against the explicitly named golden tree
- runtime and peak GPU memory when performance is part of the claim

Do not call a result “golden” without recording which reference tree and
normalization procedure produced the metric.

## Docker and ROADIES

The Docker definitions use the production double-precision configuration. The
vendored DIPPER source is part of the repository and Docker build context, so a
clean checkout does not require a sibling DIPPER repository.

Build the smaller runtime image with:

```bash
docker build -f docker/Dockerfile.runtime \
  -t wenchiehlo/mlipper-roadies:latest .
```

The larger development image is built with:

```bash
docker build -f docker/Dockerfile \
  -t wenchiehlo/mlipper:latest .
```

ROADIES should normally call `scripts/run_single_gene_MLIPPER.sh`, not
reconstruct the binary CLI itself. The wrapper contract and container path
behavior are documented in [`README_ROADIES.md`](README_ROADIES.md).

## Current Limitations

- model support is limited to 4-state DNA, GTR, 1--8 equal-weight
  discrete-Gamma categories, and `pinv = 0`; FreeRate, mixture models, custom
  category weights, invariant-site likelihoods, amino-acid models, and non-GTR
  models are rejected. See the
  [supported model scope](docs/reference/supported_model_scope.md) before
  extending model parsing, likelihood, or optimization
- the small-tip helper contract assumes split reference/query alignments
- DIPPER updates must preserve the MLIPPER integration changes documented in
  `third_party/dipper/README.md`
- the committed GPU fixture is an integration check; real-data scientific
  regression datasets and goldens are not stored in this repo
- the program uses one selected CUDA device per invocation; admission permits
  multiple processes only when live utilization and memory checks allow it
- the `docs/` experiment reports describe historical experiments and are not
  the source of truth for the current production CLI
