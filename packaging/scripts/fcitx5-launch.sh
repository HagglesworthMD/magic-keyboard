#!/usr/bin/env bash
set -euo pipefail

# Ensure a sane PATH for systemd user units (often much smaller than interactive shells)
export PATH="/usr/bin:/bin:/usr/local/bin:${PATH:-}"

# Ensure XDG_RUNTIME_DIR is set (systemd usually sets it, but be defensive)
if [[ -z "${XDG_RUNTIME_DIR:-}" ]]; then
  export XDG_RUNTIME_DIR="/run/user/$(id -u)"
fi

# Resolve fcitx5 binary robustly (avoid hard-coding /usr/bin/fcitx5)
FCITX5_BIN="$(command -v fcitx5 || true)"
if [[ -z "${FCITX5_BIN}" ]]; then
  for p in /usr/bin/fcitx5 /bin/fcitx5 /usr/local/bin/fcitx5; do
    if [[ -x "${p}" ]]; then
      FCITX5_BIN="${p}"
      break
    fi
  done
fi

if [[ -z "${FCITX5_BIN}" ]]; then
  echo "fcitx5-launch.sh: ERROR: cannot find 'fcitx5' in PATH or common locations." >&2
  echo "fcitx5-launch.sh: PATH=${PATH}" >&2
  echo "fcitx5-launch.sh: XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR}" >&2
  exit 127
fi

exec "${FCITX5_BIN}"
