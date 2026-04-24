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

echo "==> installing binaries to $INSTALL_DIR/"
mkdir -p "$INSTALL_DIR"
cp "$BINARY" "$INSTALL_DIR/"
cp game-wrap "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR/game-wrap"

if ! groups | grep -qw input; then
    echo "==> adding $USER to input group (required for /dev/uinput and evdev access)"
    sudo usermod -aG input "$USER"
    echo "    NOTE: log out and back in for the group change to take effect"
fi

echo "==> installing systemd user service (not enabled by default)"
mkdir -p "$SERVICE_DIR"
cp "$SERVICE" "$SERVICE_DIR/"
systemctl --user daemon-reload

echo ""
read -rp "Enable afk-guard to start automatically on login? [y/N] " autostart
if [[ "$autostart" =~ ^[Yy]$ ]]; then
    systemctl --user enable "$SERVICE"
    systemctl --user start "$SERVICE" || echo "    (start may fail until after re-login if group was just added)"
    echo "    autostart enabled"
else
    echo "    skipped — run manually with: afk-guard"
fi

echo ""
read -rp "Show instructions to add afk-guard to a Steam game launch option? [y/N] " steam
if [[ "$steam" =~ ^[Yy]$ ]]; then
    echo ""
    echo "  Steam per-game setup:"
    echo "  ─────────────────────"
    echo "  1. Open Steam → right-click game → Properties → General"
    echo "  2. Paste this into the Launch Options field:"
    echo ""
    echo "       game-wrap %command%"
    echo ""
    echo "  afk-guard will start with the game and exit when you close it."
    echo "  If you use this, leave autostart disabled to avoid duplicate instances."
fi

echo ""
echo "done."
echo "  run now  : afk-guard"
echo "  status   : systemctl --user status $SERVICE"
echo "  logs     : journalctl --user -u $SERVICE -f"
echo "  stop svc : systemctl --user stop $SERVICE"
