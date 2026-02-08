#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"

BIN_DIR="$PREFIX/bin"
ICON_DIR="$PREFIX/share/icons/hicolor/scalable/apps"
DESKTOP_DIR="$PREFIX/share/applications"

# Check that the binary exists
if [ ! -f "$SCRIPT_DIR/build/whisper-agent" ]; then
    echo "Error: build/whisper-agent not found."
    echo "Build first:  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
    exit 1
fi

echo "Installing to $PREFIX ..."

mkdir -p "$BIN_DIR" "$ICON_DIR" "$DESKTOP_DIR"

# Binary
install -m 755 "$SCRIPT_DIR/build/whisper-agent" "$BIN_DIR/whisper-agent"

# Icon
install -m 644 "$SCRIPT_DIR/assets/whisper-agent.svg" "$ICON_DIR/whisper-agent.svg"

# Desktop entry (patch Exec/Icon to use full paths)
sed \
    -e "s|^Exec=.*|Exec=$BIN_DIR/whisper-agent|" \
    -e "s|^Icon=.*|Icon=$ICON_DIR/whisper-agent.svg|" \
    "$SCRIPT_DIR/assets/whisper-agent.desktop" \
    > "$DESKTOP_DIR/whisper-agent.desktop"
chmod 644 "$DESKTOP_DIR/whisper-agent.desktop"

# Update icon cache (best-effort)
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
fi

# Update desktop database (best-effort)
if command -v update-desktop-database &>/dev/null; then
    update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
fi

echo "Done. Installed:"
echo "  Binary:   $BIN_DIR/whisper-agent"
echo "  Icon:     $ICON_DIR/whisper-agent.svg"
echo "  Shortcut: $DESKTOP_DIR/whisper-agent.desktop"
echo ""
echo "Make sure $BIN_DIR is in your PATH."
