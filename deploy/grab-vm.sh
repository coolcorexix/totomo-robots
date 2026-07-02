#!/usr/bin/env bash
# grab-vm.sh — retry ARM VM creation until capacity is available.
# Oracle Free Tier ARM in Singapore fills up fast; keep retrying until a slot opens.
# Usage: bash deploy/grab-vm.sh   (leave running overnight if needed)

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
STATE_FILE="$SCRIPT_DIR/.vm-state"

TENANCY="ocid1.tenancy.oc1..aaaaaaaafz2zqnuzxjecdeopjdjm4fcwvio5xdlqb6oxkz6muepbfislc2na"
AD="IQKB:AP-SINGAPORE-1-AD-1"
IMAGE="ocid1.image.oc1.ap-singapore-1.aaaaaaaaihkypxmfxzeyjevj3eutgzlnqjazicn27goya2lpr3mdmg5to2tq"
SUBNET_ID="ocid1.subnet.oc1.ap-singapore-1.aaaaaaaahnb3gmwzlwv6fmxqsyzsf5z6wnod4tpahsct3ty6pee6zd66urbq"
CLOUD_INIT="$SCRIPT_DIR/cloud-init.yaml"
SSH_PUB="$HOME/.ssh/coolcoreexix_rsa.pub"
RETRY_INTERVAL=300  # seconds between attempts (5 min — avoids TooManyRequests)
REFRESH_EVERY=10   # refresh session token every N attempts (~50 min)

echo "🔄 Grabbing ARM VM in Singapore — retrying every ${RETRY_INTERVAL}s until capacity opens..."
echo "   Press Ctrl+C to stop."
echo ""

ATTEMPT=0
while true; do
  ATTEMPT=$((ATTEMPT + 1))

  # Refresh session token periodically (token expires in ~1h)
  if (( ATTEMPT % REFRESH_EVERY == 1 )); then
    oci session refresh --profile nemo > /dev/null 2>&1 && \
      echo "[$(date '+%H:%M:%S')] 🔑 Session token refreshed." || \
      echo "[$(date '+%H:%M:%S')] ⚠️  Token refresh failed — may need to re-authenticate."
  fi

  echo "[$(date '+%H:%M:%S')] Attempt #${ATTEMPT}..."

  OUTPUT=$(oci --profile nemo --auth security_token compute instance launch \
    --compartment-id "$TENANCY" \
    --availability-domain "$AD" \
    --shape "VM.Standard.A1.Flex" \
    --shape-config '{"ocpus":4,"memoryInGBs":24}' \
    --image-id "$IMAGE" \
    --subnet-id "$SUBNET_ID" \
    --assign-public-ip true \
    --display-name "totomo-voice" \
    --ssh-authorized-keys-file "$SSH_PUB" \
    --user-data-file "$CLOUD_INIT" 2>&1) || true

  if echo "$OUTPUT" | grep -q '"id"'; then
    INSTANCE_ID=$(echo "$OUTPUT" | python3 -c "import json,sys; print(json.load(sys.stdin)['data']['id'])")
    echo "✅ VM created! Instance: $INSTANCE_ID"

    echo "Waiting for RUNNING state..."
    while true; do
      LIFECYCLE=$(oci --profile nemo --auth security_token compute instance get \
        --instance-id "$INSTANCE_ID" 2>/dev/null | \
        python3 -c "import json,sys; print(json.load(sys.stdin)['data']['lifecycle-state'])" 2>/dev/null || echo "UNKNOWN")
      echo "  → $LIFECYCLE"
      [ "$LIFECYCLE" = "RUNNING" ] && break
      sleep 10
    done

    PUBLIC_IP=$(oci --profile nemo --auth security_token compute instance list-vnics \
      --instance-id "$INSTANCE_ID" \
      --compartment-id "$TENANCY" 2>/dev/null | \
      python3 -c "import json,sys; print(json.load(sys.stdin)['data'][0]['public-ip'])" 2>/dev/null)

    cat > "$STATE_FILE" <<EOF
INSTANCE_ID=$INSTANCE_ID
COMPARTMENT=$TENANCY
SUBNET=$SUBNET_ID
PUBLIC_IP=$PUBLIC_IP
SSH_KEY=$HOME/.ssh/coolcoreexix_rsa
SSH_USER=ubuntu
EOF

    echo ""
    echo "✅ VM RUNNING!"
    echo "   Public IP : $PUBLIC_IP"
    echo "   SSH       : ssh -i ~/.ssh/coolcoreexix_rsa ubuntu@$PUBLIC_IP"
    echo ""
    echo "Next: wait ~3 min for cloud-init, then run deploy/sync-server.sh"
    exit 0
  elif echo "$OUTPUT" | grep -q "Out of host capacity"; then
    echo "   ⚡ Out of capacity — retrying in ${RETRY_INTERVAL}s..."
  else
    echo "   ⚠️  Unexpected error:"
    echo "$OUTPUT" | head -8
    echo "   Retrying in ${RETRY_INTERVAL}s..."
  fi

  sleep "$RETRY_INTERVAL"
done
