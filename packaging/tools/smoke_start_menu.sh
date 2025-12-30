#!/usr/bin/env bash
set -euo pipefail

echo "=== Smoke Test: Magic Keyboard Integration ==="

extract_primary_exec() {
  # Extract the primary Exec= (from the main [Desktop Entry] section),
  # stopping before any [Desktop Action ...] sections.
  #
  # Outputs the raw Exec= value (may include args), single line.
  local f="$1"
  awk '
    BEGIN { in_actions = 0 }
    /^\[Desktop Action / { in_actions = 1 }
    in_actions == 0 && $0 ~ /^Exec=/ {
      sub(/^Exec=/, "", $0)
      print $0
      exit
    }
  ' "$f"
}

exec_program_only() {
  # Given an Exec line (possibly with args and field codes),
  # extract the program path only.
  local exec_line="$1"

  # Drop common desktop field codes like %U, %u, %F, %f, %i, %c, %k
  exec_line="${exec_line//%U/}"
  exec_line="${exec_line//%u/}"
  exec_line="${exec_line//%F/}"
  exec_line="${exec_line//%f/}"
  exec_line="${exec_line//%i/}"
  exec_line="${exec_line//%c/}"
  exec_line="${exec_line//%k/}"

  # Trim leading/trailing whitespace
  exec_line="$(echo "$exec_line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"

  # If quoted, take the first quoted token; else take first whitespace token.
  if [[ "$exec_line" == \"*\"* ]]; then
    echo "$exec_line" | sed -n 's/^"\([^"]\+\)".*$/\1/p'
  else
    echo "$exec_line" | awk '{print $1}'
  fi
}

check_bin() {
  local name="$1"
  local path="$2"
  echo -n "Checking $name... "
  if [[ -x "$path" ]]; then
    echo "OK ($path)"
  else
    echo "FAIL (missing or not executable: $path)"
    exit 1
  fi
}

check_bin "magickeyboard-ui" "$HOME/.local/bin/magickeyboard-ui"
check_bin "magickeyboardctl" "$HOME/.local/bin/magickeyboardctl"

echo "Checking desktop entries in $HOME/.local/share/applications..."
for f in "$HOME/.local/share/applications"/magickeyboard*.desktop; do
  [[ -e "$f" ]] || continue
  base="$(basename "$f")"
  primary_exec="$(extract_primary_exec "$f")"
  if [[ -z "$primary_exec" ]]; then
    echo "  Found $base... WARN (no primary Exec= found)"
    continue
  fi
  exec_bin="$(exec_program_only "$primary_exec")"
  if [[ -n "$exec_bin" && -x "$exec_bin" ]]; then
    echo "  Found $base... OK (Ex: $exec_bin)"
  else
    echo "  Found $base... WARN (Exec '$exec_bin' not executable/found)"
  fi
done

echo "Checking systemd service..."
if [[ -f "$HOME/.config/systemd/user/magickeyboard-ui.service" ]]; then
  echo "  Found unit in $HOME/.config/systemd/user"
  grep -E '^ExecStart=' "$HOME/.config/systemd/user/magickeyboard-ui.service" || true
elif [[ -f "$HOME/.local/lib/systemd/user/magickeyboard-ui.service" ]]; then
  echo "  Found unit in $HOME/.local/lib/systemd/user"
  grep -E '^ExecStart=' "$HOME/.local/lib/systemd/user/magickeyboard-ui.service" || true
else
  echo "  WARN: no magickeyboard-ui.service found in expected locations"
fi

if systemctl --user status magickeyboard-ui.service >/dev/null 2>&1; then
  echo "  Systemd status: ACTIVE/LOADED"
else
  echo "  WARN: magickeyboard-ui.service not active"
fi

echo "--- Testing Actions ---"
echo "Toggle..."
"$HOME/.local/bin/magickeyboardctl" toggle >/dev/null 2>&1 || true
echo "Show..."
"$HOME/.local/bin/magickeyboardctl" show >/dev/null 2>&1 || true
echo "Hide..."
"$HOME/.local/bin/magickeyboardctl" hide >/dev/null 2>&1 || true

echo "=== Done ==="
