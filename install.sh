#!/usr/bin/env bash
set -e

BINARY="afk-guard"
INSTALL_DIR="$HOME/.local/bin"
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE="afk-guard.service"

need() { command -v "$1" >/dev/null 2>&1 || { echo "error: $1 not found — install it and retry"; exit 1; }; }
need gcc
need make

echo "==> building..."
make

echo "==> installing binary to $INSTALL_DIR/"
mkdir -p "$INSTALL_DIR"
cp "$BINARY" "$INSTALL_DIR/"

if ! groups | grep -qw input; then
    echo "==> adding $USER to input group (required for /dev/uinput and evdev access)"
    sudo usermod -aG input "$USER"
    echo "    NOTE: log out and back in for the group change to take effect"
    echo "    after re-login, re-run: systemctl --user start $SERVICE"
fi

echo "==> installing systemd user service"
mkdir -p "$SERVICE_DIR"
cp "$SERVICE" "$SERVICE_DIR/"
systemctl --user daemon-reload
systemctl --user enable "$SERVICE"
systemctl --user start  "$SERVICE" || echo "    (start may fail until after re-login if group was just added)"

echo ""
echo "done."
echo "  status : systemctl --user status $SERVICE"
echo "  logs   : journalctl --user -u $SERVICE -f"
echo "  stop   : systemctl --user stop $SERVICE"
