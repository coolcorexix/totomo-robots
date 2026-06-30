#!/usr/bin/env bash
# sync-server.sh — push Totomo custom files + .config.yaml to the VM,
# then start the voice server.
# Run after open-port.sh (or any time you update local files).

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/.vm-state"

VOICE_SERVER="$HOME/Documents/nemo-lab.nosync/totomo-voice-server/main/xiaozhi-server"
REMOTE="$SSH_USER@$PUBLIC_IP"
SSH_OPTS="-i $SSH_KEY -o StrictHostKeyChecking=accept-new"
REMOTE_BASE="/opt/totomo/server/main/xiaozhi-server"

# ── Verify .config.yaml exists (has real API keys) ───────────────────────────
CONFIG_FILE="$VOICE_SERVER/data/.config.yaml"
if [ ! -f "$CONFIG_FILE" ]; then
  echo "❌ Missing: $CONFIG_FILE"
  echo "   Copy config.yaml.example → data/.config.yaml and fill in your API keys."
  exit 1
fi

echo "=== Pushing custom ASR providers ==="
scp $SSH_OPTS \
  "$VOICE_SERVER/core/providers/asr/deepgram.py" \
  "$VOICE_SERVER/core/providers/asr/whisper_local.py" \
  "$VOICE_SERVER/core/providers/asr/whispercpp.py" \
  "$REMOTE:$REMOTE_BASE/core/providers/asr/"

echo "=== Pushing LLM provider patch ==="
scp $SSH_OPTS \
  "$VOICE_SERVER/core/providers/llm/ollama/ollama.py" \
  "$REMOTE:$REMOTE_BASE/core/providers/llm/ollama/"

echo "=== Pushing intent plugin ==="
scp $SSH_OPTS \
  "$VOICE_SERVER/plugins_func/functions/alarm_clock.py" \
  "$REMOTE:$REMOTE_BASE/plugins_func/functions/"

echo "=== Pushing listen handler fix ==="
scp $SSH_OPTS \
  "$VOICE_SERVER/core/handle/textHandler/listenMessageHandler.py" \
  "$REMOTE:$REMOTE_BASE/core/handle/textHandler/"

echo "=== Pushing Vietnamese prompt ==="
scp $SSH_OPTS \
  "$VOICE_SERVER/agent-base-prompt-vi.txt" \
  "$REMOTE:$REMOTE_BASE/"

echo "=== Pushing .config.yaml (API keys) ==="
ssh $SSH_OPTS "$REMOTE" "mkdir -p $REMOTE_BASE/data"
scp $SSH_OPTS "$CONFIG_FILE" "$REMOTE:$REMOTE_BASE/data/.config.yaml"

echo "=== Fixing ownership + restarting service ==="
ssh $SSH_OPTS "$REMOTE" "
  sudo chown -R totomo:totomo /opt/totomo &&
  sudo systemctl restart totomo-voice &&
  sleep 2 &&
  sudo systemctl status totomo-voice --no-pager
"

echo ""
echo "✅ Deployed! Voice server running at ws://$PUBLIC_IP:8000"
echo ""
echo "Update ESP32 config.h:"
echo '  #define SERVER_HOST  "'$PUBLIC_IP'"'
echo "  #define SERVER_PORT  8000"
echo ""
echo "Watch logs: ssh -i $SSH_KEY $REMOTE 'sudo journalctl -u totomo-voice -f'"
