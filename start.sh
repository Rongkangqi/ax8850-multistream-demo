#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
APP=ax8850-multistream
PREVIEW=ax8850-local-preview
command -v ffmpeg >/dev/null || { echo 'Install ffmpeg first.' >&2; exit 1; }
if systemctl is-active --quiet "$APP"; then
  echo "$APP is already running. Stop it before changing the configuration."
  exit 0
fi
# Do not stop another application's streams just to claim its ports.
python3 - <<'PY'
import socket
for host,port in [('0.0.0.0',8554),('127.0.0.1',8850)]:
    with socket.socket() as s:
        s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        try:s.bind((host,port))
        except OSError:raise SystemExit(f'Port {port} is already in use. Stop the existing streaming/preview service first.')
PY
sudo systemd-run --unit="$APP" --collect --property="WorkingDirectory=$ROOT" --property=TimeoutStopSec=30 /bin/bash "$ROOT/run.sh"
sleep 2
systemctl is-active --quiet "$APP" || { sudo journalctl -u "$APP" -n 25 --no-pager; exit 1; }
sudo systemd-run --unit="$PREVIEW" --collect --property="WorkingDirectory=$ROOT" --property=TimeoutStopSec=12 /usr/bin/python3 "$ROOT/tools/local_preview.py"
echo 'LAN: rtsp://<HOST_IP>:8554/overview'
echo 'Board VLC: http://127.0.0.1:8850/overview.ts'
