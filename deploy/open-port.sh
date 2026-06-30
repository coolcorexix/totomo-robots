#!/usr/bin/env bash
# open-port.sh — add TCP port 8000 ingress to the OCI Security List.
# Run after create-vm.sh.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/.vm-state"

echo "=== Finding security list for the VM's subnet ==="
VCN_ID=$(oci network subnet get --subnet-id "$SUBNET" \
  --query 'data."vcn-id"' --raw-output)

SEC_LIST_ID=$(oci network security-list list \
  --compartment-id "$COMPARTMENT" \
  --vcn-id "$VCN_ID" \
  --query 'data[0].id' --raw-output)
echo "    Security list: $SEC_LIST_ID"

echo "=== Reading current ingress rules ==="
CURRENT=$(oci network security-list get \
  --security-list-id "$SEC_LIST_ID" \
  --query 'data."ingress-security-rules"' --raw-output)

echo "=== Adding TCP 8000 ingress rule ==="
UPDATED=$(python3 - <<'PYEOF'
import json, sys

current = json.loads("""$CURRENT""")

port_rule = {
    "protocol": "6",
    "source": "0.0.0.0/0",
    "isStateless": False,
    "tcpOptions": {
        "destinationPortRange": {"min": 8000, "max": 8000}
    }
}

# Skip if already present
already = any(
    r.get("tcpOptions", {}).get("destinationPortRange", {}).get("min") == 8000
    for r in current
)
if already:
    print(json.dumps(current))
    sys.stderr.write("Port 8000 rule already exists — skipping.\n")
else:
    current.append(port_rule)
    print(json.dumps(current))
PYEOF
)

oci network security-list update \
  --security-list-id "$SEC_LIST_ID" \
  --ingress-security-rules "$UPDATED" \
  --force

echo "✅ Port 8000 open in OCI Security List."
echo "   UFW inside the VM was already configured by cloud-init."
