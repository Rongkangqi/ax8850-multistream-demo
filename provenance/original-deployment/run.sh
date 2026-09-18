#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
export LD_LIBRARY_PATH="/usr/lib/axcl:/home/baiwen/ax-pipeline/ax_pipeline_axcl-aarch64/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export AXP_AXCL_VNPU_KIND=disable
exec stdbuf -oL -eL ./bin/six_app ./config.json "${1:-0}"
