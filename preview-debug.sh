#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
STREAM=${1:-overview}
case "$STREAM" in overview|pcd|vehicle|seg|driving|depth|count) ;; *) echo 'Unknown stream name.'; exit 2;; esac
mkdir -p "$ROOT/logs/vlc"
LOG="$ROOT/logs/vlc/$(date +%Y%m%d-%H%M%S)-$STREAM.log"
echo "VLC log: $LOG"
exec vlc --no-one-instance --network-caching=1000 -vv --file-logging --logfile="$LOG" "http://127.0.0.1:8850/$STREAM.ts"
