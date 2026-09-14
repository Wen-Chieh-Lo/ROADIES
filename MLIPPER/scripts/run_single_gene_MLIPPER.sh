#!/usr/bin/env bash
set -euo pipefail

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEFAULT_DOCKER_IMAGE="${MLIPPER_DOCKER_IMAGE:-wenchiehlo/mlipper-roadies:latest}"
DEFAULT_GPU_ID="${MLIPPER_GPU_ID:-0}"

usage() {
  cat <<EOF
Usage:
  $SCRIPT_NAME \\
    --ref-msa REF.fa \\
    --query-msa QUERY.fa \\
    --backbone-tree BACKBONE.nwk \\
    --best-model GENE.raxml.bestModel \\
    --out-tree OUT.nwk \\
    [options]

Required:
  --ref-msa PATH              Reference/backbone alignment
  --query-msa PATH            Query alignment to commit into the backbone tree
  --backbone-tree PATH        Backbone/reference Newick tree
  --best-model PATH           Per-gene bestModel file
  --out-tree PATH             Output committed Newick tree

Optional:
  --docker-image IMAGE        Docker image to run (docker mode only)
                              Default: $DEFAULT_DOCKER_IMAGE
  --gpu-id INT                GPU id used when --docker-gpus is not provided
                              Default: $DEFAULT_GPU_ID
  --gpu-auto                  Let MLIPPER select a GPU using shared admission
  --docker-gpus SPEC          Raw Docker --gpus value (overrides --gpu-id)
  --no-docker                 Run local MLIPPER binary instead of Docker
  --local-mlipper PATH        Local MLIPPER binary path (defaults to REPO_ROOT/MLIPPER)
  --no-local-spr              Disable local SPR refinement
                              Default: local SPR is enabled unless this flag is set
  --final-optimization        Enable final model-parameter and full-tree branch-length
                              optimization (disabled by default for ROADIES parity)
  --batch-size INT            Batch insert size
                              Default: 50
  --local-spr-radius INT      Local SPR radius
                              Default: 4
  --local-spr-rounds INT      Local SPR rounds
                              Default: 1
  -h, --help                  Show this message

Notes:
  - This wrapper owns the Docker invocation.
  - ROADIES can use --gpu-auto without choosing a GPU id.
  - In non-docker mode, build MLIPPER on the host before running this wrapper.
  - MLIPPER itself reads the bestModel file via --best-model.
EOF
}

die() {
  echo "$SCRIPT_NAME: $*" >&2
  exit 1
}

require_file() {
  local path="$1"
  [[ -f "$path" ]] || die "missing file: $path"
}

abs_existing_path() {
  python3 - "$1" <<'PY'
import os
import sys

path = sys.argv[1]
if not os.path.exists(path):
    raise SystemExit(f"missing path: {path}")
print(os.path.realpath(path))
PY
}

abs_target_path() {
  python3 - "$1" <<'PY'
import os
import sys

print(os.path.realpath(sys.argv[1]))
PY
}

common_root_for_paths() {
  python3 - "$@" <<'PY'
import os
import sys

print(os.path.commonpath(sys.argv[1:]))
PY
}

containerize_path() {
  python3 - "$1" "$2" <<'PY'
import os
import sys

path = os.path.realpath(sys.argv[1])
root = os.path.realpath(sys.argv[2])
rel = os.path.relpath(path, root)
if rel.startswith(".."):
    raise SystemExit(f"path {path} escapes mount root {root}")
print("/workspace/job/" + rel.replace(os.sep, "/"))
PY
}

quote_cmd() {
  local piece
  for piece in "$@"; do
    printf '%q ' "$piece"
  done
  printf '\n'
}

ensure_local_binary() {
  if [[ ! -x "$local_mlipper" ]]; then
    die "local MLIPPER binary missing or not executable: $local_mlipper (build it with make MLIPPER)"
  fi
}

ref_msa=""
query_msa=""
backbone_tree=""
best_model=""
out_tree=""
docker_image="$DEFAULT_DOCKER_IMAGE"
gpu_id="$DEFAULT_GPU_ID"
gpu_id_specified=0
gpu_auto=0
docker_gpus=""
local_mlipper="$REPO_ROOT/MLIPPER"
use_docker=1
# Local SPR is enabled by default by the MLIPPER commit workflow.
local_spr_enabled=1
# ROADIES sign-off trees predate the final model/BLO stages. Keep those stages
# opt-in here so the wrapper retains the established per-gene output contract.
final_optimization_enabled=0
batch_size=50
local_spr_radius=4
local_spr_rounds=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ref-msa)
      ref_msa="${2:-}"
      shift 2
      ;;
    --query-msa)
      query_msa="${2:-}"
      shift 2
      ;;
    --backbone-tree)
      backbone_tree="${2:-}"
      shift 2
      ;;
    --best-model)
      best_model="${2:-}"
      shift 2
      ;;
    --out-tree)
      out_tree="${2:-}"
      shift 2
      ;;
    --docker-image)
      docker_image="${2:-}"
      shift 2
      ;;
    --gpu-id)
      gpu_id="${2:-}"
      gpu_id_specified=1
      shift 2
      ;;
    --gpu-auto)
      gpu_auto=1
      shift
      ;;
    --docker-gpus)
      docker_gpus="${2:-}"
      shift 2
      ;;
    --no-docker)
      use_docker=0
      shift
      ;;
    --local-mlipper)
      local_mlipper="${2:-}"
      shift 2
      ;;
    --no-local-spr)
      local_spr_enabled=0
      shift
      ;;
    --final-optimization)
      final_optimization_enabled=1
      shift
      ;;
    --batch-size)
      batch_size="${2:-}"
      shift 2
      ;;
    --local-spr-radius)
      local_spr_radius="${2:-}"
      shift 2
      ;;
    --local-spr-rounds)
      local_spr_rounds="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

[[ -n "$ref_msa" ]] || die "--ref-msa is required"
[[ -n "$query_msa" ]] || die "--query-msa is required"
[[ -n "$backbone_tree" ]] || die "--backbone-tree is required"
[[ -n "$best_model" ]] || die "--best-model is required"
[[ -n "$out_tree" ]] || die "--out-tree is required"

require_file "$ref_msa"
require_file "$query_msa"
require_file "$backbone_tree"
require_file "$best_model"

[[ "$gpu_id" =~ ^[0-9]+$ ]] || die "--gpu-id must be a non-negative integer"
[[ "$batch_size" =~ ^[0-9]+$ ]] || die "--batch-size must be a non-negative integer"
[[ "$local_spr_radius" =~ ^[0-9]+$ ]] || die "--local-spr-radius must be a non-negative integer"
[[ "$local_spr_rounds" =~ ^[1-9][0-9]*$ ]] || die "--local-spr-rounds must be >= 1"
if [[ "$gpu_auto" -eq 1 && "$gpu_id_specified" -eq 1 ]]; then
  die "--gpu-auto cannot be used together with --gpu-id"
fi
if [[ "$gpu_auto" -eq 1 && -n "$docker_gpus" ]]; then
  die "--gpu-auto cannot be used together with --docker-gpus"
fi

mkdir -p "$(dirname "$out_tree")"

ref_msa="$(abs_existing_path "$ref_msa")"
query_msa="$(abs_existing_path "$query_msa")"
backbone_tree="$(abs_existing_path "$backbone_tree")"
best_model="$(abs_existing_path "$best_model")"
out_tree="$(abs_target_path "$out_tree")"

if [[ "$use_docker" -eq 1 ]]; then
  if [[ "$gpu_auto" -eq 1 ]]; then
    gpu_spec="all"
  else
    gpu_spec="$docker_gpus"
  fi
  if [[ -z "$gpu_spec" ]]; then
    gpu_spec="device=$gpu_id"
  fi

  common_root="$(common_root_for_paths "$ref_msa" "$query_msa" "$backbone_tree" "$best_model" "$out_tree")"
  ref_msa_in_container="$(containerize_path "$ref_msa" "$common_root")"
  query_msa_in_container="$(containerize_path "$query_msa" "$common_root")"
  backbone_tree_in_container="$(containerize_path "$backbone_tree" "$common_root")"
  best_model_in_container="$(containerize_path "$best_model" "$common_root")"
  out_tree_in_container="$(containerize_path "$out_tree" "$common_root")"
else
  local_mlipper="$(abs_target_path "$local_mlipper")"
  ensure_local_binary
fi

if [[ "$use_docker" -eq 1 ]]; then
  mlipper_args=(
    --tree-alignment "$ref_msa_in_container"
    --query-alignment "$query_msa_in_container"
    --tree "$backbone_tree_in_container"
    --best-model "$best_model_in_container"
    --commit-to-tree "$out_tree_in_container"
  )
else
  mlipper_args=(
    --tree-alignment "$ref_msa"
    --query-alignment "$query_msa"
    --tree "$backbone_tree"
    --best-model "$best_model"
    --commit-to-tree "$out_tree"
  )
fi

if [[ "$gpu_auto" -eq 1 ]]; then
  mlipper_args+=(--gpu-auto)
elif [[ "$use_docker" -eq 0 ]]; then
  mlipper_args+=(--gpu-id "$gpu_id")
fi

if [[ "$local_spr_enabled" -eq 1 ]]; then
  mlipper_args+=(
    --batch-insert-size "$batch_size"
    --local-spr-radius "$local_spr_radius"
    --local-spr-rounds "$local_spr_rounds"
  )
else
  mlipper_args+=(--no-local-spr)
fi

if [[ "$final_optimization_enabled" -eq 0 ]]; then
  mlipper_args+=(
    --no-model-optimization
    --no-global-branch-optimization
  )
fi

if [[ "$use_docker" -eq 1 ]]; then
  docker_cmd=(docker run --rm)
  if [[ "$gpu_auto" -eq 1 ]]; then
    docker_cmd+=(--ipc=host)
  fi
  docker_cmd+=(
    --gpus "$gpu_spec"
    --user "$(id -u):$(id -g)"
    -v "$common_root:/workspace/job"
    -w /workspace/job
    --entrypoint /workspace/MLIPPER/MLIPPER
    "$docker_image"
  )
  docker_cmd+=("${mlipper_args[@]}")
else
  local_cmd=("$local_mlipper")
  local_cmd+=("${mlipper_args[@]}")
fi

if [[ "$use_docker" -eq 1 ]]; then
  echo "CMD: $(quote_cmd "${docker_cmd[@]}")" >&2
  "${docker_cmd[@]}"
else
  echo "CMD: $(quote_cmd "${local_cmd[@]}")" >&2
  "${local_cmd[@]}"
fi
