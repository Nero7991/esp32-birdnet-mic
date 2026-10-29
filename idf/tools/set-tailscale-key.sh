#!/usr/bin/env bash
# Store a Tailscale auth key on the mic without the key appearing in argv, output or history.
# Usage: idf/tools/set-tailscale-key.sh <mic-ip-or-host>
set -euo pipefail
host="${1:?usage: $0 <mic-ip-or-host>}"
read -rs -p "Tailscale auth key (input hidden): " key
printf '\n'
printf '%s' "$key" | curl -sS -m 20 -X POST -H 'X-ESP32MIC-CSRF: 1' \
  --data-urlencode auth_key@- "http://$host/api/tailscale"
unset key
printf '\n'
