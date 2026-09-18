#!/bin/bash
set -euo pipefail
STREAM=${1:-overview}
case "$STREAM" in
  overview|pcd|vehicle|seg|driving|depth|count) ;;
  *) echo 'Usage: ./preview-local.sh [overview|pcd|vehicle|seg|driving|depth|count]'; exit 2 ;;
esac
if ! systemctl is-active --quiet ax-six-local-preview; then
  echo 'Run ./start.sh first.'
  exit 1
fi
exec vlc --network-caching=500 "http://127.0.0.1:8850/$STREAM.ts"
