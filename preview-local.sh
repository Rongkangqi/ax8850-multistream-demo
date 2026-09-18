#!/bin/bash
set -euo pipefail
STREAM=${1:-overview}
case "$STREAM" in overview|pcd|vehicle|seg|driving|depth|count) ;; *) echo 'Unknown stream name.'; exit 2;; esac
exec vlc --network-caching=1000 "http://127.0.0.1:8850/$STREAM.ts"
