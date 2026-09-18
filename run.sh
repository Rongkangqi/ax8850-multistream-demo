#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$ROOT"
CONFIG=${1:-configs/six.json}
DURATION=${2:-0}
if [[ -n "${AX8850_RUNTIME:-}" ]]; then
  RUNTIME=$AX8850_RUNTIME
elif [[ -x "$ROOT/build/install/bin/six_app" ]]; then
  RUNTIME=$ROOT/build/install
else
  RUNTIME=$ROOT/prebuilt/linux-$(uname -m)
fi
[[ -f "$RUNTIME/bin/six_app" ]] || { echo 'No matching runtime. Run bash build.sh first.' >&2; exit 1; }
mkdir -p run
exec 9>run/application.lock
flock -n 9 || { echo 'This repository already has a running instance.' >&2; exit 1; }
python3 tools/prepare_config.py --config "$CONFIG" --runtime "$RUNTIME" --output run/config.json
export LD_LIBRARY_PATH="$RUNTIME/lib:${AXCL_LIB_DIR:-/usr/lib/axcl}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export AXP_AXCL_VNPU_KIND=disable
exec stdbuf -oL -eL "$RUNTIME/bin/six_app" "$ROOT/run/config.json" "$DURATION"
