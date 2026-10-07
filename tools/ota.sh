#!/bin/bash
# Püsivara uuendus üle WiFi: tools/ota.sh [IP]  (vaikimisi /tmp/shock_ip või 4.3.2.1)
cd "$(dirname "$0")/.."
IP=${1:-$(cat /tmp/shock_ip 2>/dev/null || echo 4.3.2.1)}
BIN=/home/alvar/.cache/shock_test/build/shock/firmware.bin
echo "Saadan $(stat -c %s $BIN) baiti -> http://$IP/api/ota"
curl -sS -m 180 --data-binary @$BIN -H "Content-Type: application/octet-stream" http://$IP/api/ota; echo
echo "Ootan taaskäivitust ..."
sleep 8
for i in $(seq 1 30); do
  s=$(curl -s -m 2 http://$IP/api/status) && { echo "$s"; exit 0; }
  sleep 2
done
echo "Seade ei vasta"; exit 1
