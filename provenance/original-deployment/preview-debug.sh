#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
STREAM=${1:-overview}
case "$STREAM" in
  overview|pcd|vehicle|seg|driving|depth|count) ;;
  *) echo 'Usage: ./preview-debug.sh [overview|pcd|vehicle|seg|driving|depth|count]'; exit 2 ;;
esac
if ! systemctl is-active --quiet ax-six-local-preview; then
  echo 'Run ./start.sh first.'
  exit 1
fi
mkdir -p "$ROOT/logs/vlc"
LOG="$ROOT/logs/vlc/$(date +%Y%m%d-%H%M%S)-$STREAM.log"
echo "VLC diagnostic log: $LOG"
exec vlc --no-one-instance --network-caching=1000 -vv --file-logging --logfile="$LOG" "http://127.0.0.1:8850/$STREAM.ts"
