#!/usr/bin/env bash
# create-vm.sh — create the Always Free ARM VM on Oracle Cloud.
# Run ONCE after `oci --profile nemo --auth security_token setup config` is done.
# Saves VM details to .vm-state for use by other scripts.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
STATE_FILE="$SCRIPT_DIR/.vm-state"
SSH_KEY="$HOME/.ssh/coolcoreexix_rsa.pub"
DISPLAY_NAME="totomo-voice"

echo "=== [1/5] Resolving tenancy and compartment ==="
TENANCY=$(oci --profile nemo --auth security_token iam account get --query 'data."tenancy-id"' --raw-output)
COMPARTMENT="$TENANCY"   # root compartment = tenancy for new accounts
echo "    Tenancy: $TENANCY"

echo "=== [2/5] Finding availability domain ==="
AD=$(oci --profile nemo --auth security_token iam availability-domain list \
  --compartment-id "$COMPARTMENT" \
  --query 'data[0].name' --raw-output)
echo "    AD: $AD"

echo "=== [3/5] Finding Ubuntu 22.04 ARM image ==="
IMAGE=$(oci --profile nemo --auth security_token compute image list \
  --compartment-id "$COMPARTMENT" \
  --operating-system "Canonical Ubuntu" \
  --operating-system-version "22.04" \
  --shape "VM.Standard.A1.Flex" \
  --sort-by TIMECREATED --sort-order DESC \
  --query 'data[0].id' --raw-output)
echo "    Image: $IMAGE"

echo "=== [4/5] Finding default subnet ==="
SUBNET=$(oci --profile nemo --auth security_token network subnet list \
  --compartment-id "$COMPARTMENT" \
  --query 'data[0].id' --raw-output)
echo "    Subnet: $SUBNET"

echo "=== [5/5] Launching VM (4 OCPU, 24 GB RAM) ==="
INSTANCE_ID=$(oci --profile nemo --auth security_token compute instance launch \
  --compartment-id "$COMPARTMENT" \
  --availability-domain "$AD" \
  --shape "VM.Standard.A1.Flex" \
  --shape-config '{"ocpus":4,"memoryInGBs":24}' \
  --image-id "$IMAGE" \
  --subnet-id "$SUBNET" \
  --assign-public-ip true \
  --display-name "$DISPLAY_NAME" \
  --ssh-authorized-keys-file "$SSH_KEY" \
  --user-data-file "$SCRIPT_DIR/cloud-init.yaml" \
  --query 'data.id' --raw-output)
echo "    Instance ID: $INSTANCE_ID"

echo ""
echo "Waiting for VM to reach RUNNING state..."
while true; do
  LIFECYCLE=$(oci --profile nemo --auth security_token compute instance get --instance-id "$INSTANCE_ID" \
    --query 'data."lifecycle-state"' --raw-output)
  echo "  → $LIFECYCLE"
  [ "$LIFECYCLE" = "RUNNING" ] && break
  sleep 10
done

PUBLIC_IP=$(oci --profile nemo --auth security_token compute instance list-vnics \
  --instance-id "$INSTANCE_ID" \
  --compartment-id "$COMPARTMENT" \
  --query 'data[0]."public-ip"' --raw-output)

# Save state for other scripts
cat > "$STATE_FILE" <<EOF
INSTANCE_ID=$INSTANCE_ID
COMPARTMENT=$COMPARTMENT
SUBNET=$SUBNET
PUBLIC_IP=$PUBLIC_IP
SSH_KEY=$HOME/.ssh/coolcoreexix_rsa
SSH_USER=ubuntu
EOF

echo ""
echo "✅ VM is RUNNING!"
echo "   Public IP : $PUBLIC_IP"
echo "   SSH       : ssh -i ~/.ssh/coolcoreexix_rsa ubuntu@$PUBLIC_IP"
echo ""
echo "Next steps:"
echo "  1. Wait ~3 min for cloud-init to finish (install packages)"
echo "  2. Run:  deploy/open-port.sh     ← open port 8000 in OCI firewall"
echo "  3. Run:  deploy/sync-server.sh   ← push custom files + .config.yaml"
