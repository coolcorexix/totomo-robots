#!/usr/bin/env bash
# Oracle Cloud Free Tier — totomo-voice-server setup
# Run once on a fresh Ubuntu 22.04 ARM VM (Ampere A1).
#
# Before running:
#   1. chmod +x oracle-setup.sh
#   2. Fill in SERVER_REPO and BRANCH below
#   3. Have your .config.yaml ready to scp after setup

set -euo pipefail

SERVER_REPO="https://github.com/xinnan-tech/xiaozhi-esp32-server.git"
BRANCH="main"
INSTALL_DIR="/opt/totomo-voice"
SERVICE_USER="totomo"

echo "=== [1/6] System packages ==="
sudo apt-get update -qq
sudo apt-get install -y python3.11 python3.11-venv python3-pip \
     ffmpeg git curl unzip build-essential libopus-dev

echo "=== [2/6] Create service user ==="
sudo useradd -r -s /bin/false -d "$INSTALL_DIR" "$SERVICE_USER" 2>/dev/null || true

echo "=== [3/6] Clone server ==="
sudo mkdir -p "$INSTALL_DIR"
sudo chown "$SERVICE_USER:$SERVICE_USER" "$INSTALL_DIR"
sudo -u "$SERVICE_USER" git clone "$SERVER_REPO" "$INSTALL_DIR/server" 2>/dev/null \
    || (cd "$INSTALL_DIR/server" && sudo -u "$SERVICE_USER" git pull)

echo "=== [4/6] Copy Totomo custom files ==="
# Copy our additions on top of the upstream clone.
# Run this script from the totomo-voice-server checkout root, e.g.:
#   scp -r main/xiaozhi-server/core/providers/asr/deepgram.py  ubuntu@<ip>:...
# The files below must already be present in $INSTALL_DIR/server:
XIAO="$INSTALL_DIR/server/main/xiaozhi-server"
echo "    → Expecting custom files already scp'd into $XIAO"
echo "       alarm_clock.py, deepgram.py, whisper_local.py, whispercpp.py"
echo "       agent-base-prompt-vi.txt, listenMessageHandler.py"

echo "=== [5/6] Python venv + dependencies ==="
sudo -u "$SERVICE_USER" python3.11 -m venv "$INSTALL_DIR/venv"
# Strip vosk (no arm64 wheel) and install the rest
grep -v "vosk" "$XIAO/requirements.txt" | \
    sudo -u "$SERVICE_USER" "$INSTALL_DIR/venv/bin/pip" install -r /dev/stdin -q

echo "=== [6/6] systemd service ==="
sudo cp "$(dirname "$0")/totomo-voice.service" /etc/systemd/system/
sudo sed -i "s|__INSTALL_DIR__|$INSTALL_DIR|g" /etc/systemd/system/totomo-voice.service
sudo systemctl daemon-reload
sudo systemctl enable totomo-voice
sudo systemctl start totomo-voice

echo ""
echo "✅ Done. Next steps:"
echo "   1. scp your .config.yaml to $XIAO/data/.config.yaml"
echo "   2. sudo systemctl restart totomo-voice"
echo "   3. sudo journalctl -u totomo-voice -f   # watch logs"
echo "   4. Open port 8000 in Oracle Security List (TCP ingress)"
echo "   5. Update config.h on ESP32:  SERVER_HOST = \"<this-vm-public-ip>\""
