#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
if ! systemctl is-active --quiet ax-six-rtsp; then
  sudo systemctl stop ax-pipeline-pcd 2>/dev/null || true
  sudo systemd-run --unit=ax-six-rtsp --collect --property=WorkingDirectory="$ROOT" --property=TimeoutStopSec=30 /bin/bash "$ROOT/run.sh"
fi
if ! systemctl is-active --quiet ax-six-local-preview; then
  sudo systemd-run --unit=ax-six-local-preview --collect --property=WorkingDirectory="$ROOT" --property=User=baiwen --property=TimeoutStopSec=12 /usr/bin/python3 "$ROOT/local-preview.py"
fi
echo 'LAN RTSP: rtsp://192.168.1.44:8554/overview'
echo 'Board VLC: http://127.0.0.1:8850/overview.ts'
echo 'Run ./preview-local.sh to open VLC on the board desktop.'
