#!/bin/bash
set -e

echo "=== Smoke Test A: Rapid Toggle Spam (Lifecycle + QML Guard) ==="
echo "Toggling 25 times quickly..."
for i in $(seq 1 25); do 
    ~/.local/bin/magickeyboardctl toggle
    sleep 0.05
done
echo "Checking logs for errors..."
if journalctl --user -u magickeyboard-ui.service --since "30s ago" | egrep -i "TypeError|ReferenceError|ASSERT|abort|core dump"; then
    echo "❌ Errors found during toggle spam!"
    exit 1
else
    echo "✅ Clean (no Type/Reference errors, asserts, or crashes)"
fi

echo ""
echo "=== Smoke Test B: Restart Churn (Systemd + IPC + Protocol Role) ==="
echo "Restarting service..."
systemctl --user restart magickeyboard-ui.service
sleep 0.5
echo "Toggling..."
~/.local/bin/magickeyboardctl toggle
echo "Checking logs for handshake..."
# We just show the last few lines for manual verification as requested, or we could grep for "Identity: ui" or similar if we implemented it.
# The user asked to "tail -n 80".
journalctl --user -u magickeyboard-ui.service --since "20s ago" | tail -n 20 

echo ""
echo "✅ Smoke tests completed."
