#!/usr/bin/env bash
set -euo pipefail

UNIT="magickeyboard-ui.service"
CTL="$HOME/.local/bin/magickeyboardctl"

echo "== Smoke tests: $(date) =="

echo "-- Test A: rapid toggle spam"
for i in $(seq 1 25); do
  "$CTL" toggle
  sleep 0.05
done

echo "-- Checking logs for critical errors (last 30s)"
if journalctl --user -u "$UNIT" --since "30s ago" | egrep -i "TypeError|ReferenceError|ASSERT|abort|core dump"; then
  echo "❌ Test A FAILED: critical errors found."
  journalctl --user -u "$UNIT" --since "30s ago" | tail -n 120
  exit 1
else
  echo "✅ Test A PASSED"
fi

echo "-- Test B: restart churn + toggle"
systemctl --user restart "$UNIT"
sleep 0.2
"$CTL" toggle

echo "-- Recent logs (last 20s)"
journalctl --user -u "$UNIT" --since "20s ago" | tail -n 120

echo "✅ Smoke tests completed"
