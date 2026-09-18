#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release -DAXCL_ROOT="${AXCL_ROOT:-/usr}"
cmake --build "$ROOT/build" --parallel "${JOBS:-2}"
cmake --install "$ROOT/build" --prefix "$ROOT/build/install" --component Demo
echo "Built runtime: $ROOT/build/install"
