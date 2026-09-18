#!/bin/bash
set -euo pipefail
if systemctl is-active --quiet ax-six-local-preview; then
  sudo systemctl stop ax-six-local-preview
fi
sudo systemctl stop ax-six-rtsp
