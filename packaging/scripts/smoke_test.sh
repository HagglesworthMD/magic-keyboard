#!/usr/bin/env bash
set -euo pipefail

UNIT="magickeyboard-ui.service"
CTL="$HOME/.local/bin/magickeyboardctl"
SOCKET="/run/user/${UID}/magic-keyboard.sock"

echo "== Smoke tests: $(date) =="

# Pre-flight: ensure engine socket exists
if [[ ! -S "$SOCKET" ]]; then
  echo "⚠️  Socket not found: $SOCKET"
  echo "   The fcitx5 engine must be running for smoke tests."
  echo ""
  echo "   To start it:"
  echo "     pkill -u \$USER fcitx5 2>/dev/null || true"
  echo "     fcitx5 -d"
  echo "     sleep 2"
  echo ""
  # Wait up to 5s in case it's just starting
  echo "   Waiting up to 5s for socket..."
  for i in {1..10}; do
    if [[ -S "$SOCKET" ]]; then
      echo "   ✅ Socket appeared after $((i/2))s"
      break
    fi
    sleep 0.5
  done
  if [[ ! -S "$SOCKET" ]]; then
    echo "❌ Socket still missing after 5s. Please start fcitx5 first."
    exit 1
  fi
fi

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
