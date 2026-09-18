#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
SECONDS_TO_RECORD=${1:-60}
[[ "$SECONDS_TO_RECORD" =~ ^[1-9][0-9]*$ ]] || { echo 'Duration must be a positive integer (seconds).'; exit 2; }
mkdir -p recordings
OUT="recordings/overview-$(date +%Y%m%d-%H%M%S).mp4"
ffmpeg -hide_banner -nostdin -rtsp_transport tcp -timeout 8000000 -analyzeduration 1000000 -probesize 1000000 -i rtsp://127.0.0.1:8554/overview -map 0:v:0 -an -c:v copy -t "$SECONDS_TO_RECORD" -movflags +faststart "$OUT"
printf 'Saved: %s/%s\n' "$PWD" "$OUT"
