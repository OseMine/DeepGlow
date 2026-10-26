#!/usr/bin/env bash
# DeepGlow 2 - Linux Installer (OFX Bundle)
# Installs to: /usr/OFX/Plugins/ or ~/.OFX/Plugins/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE="$(dirname "$SCRIPT_DIR")/../bundle/DeepGlow2.ofx.bundle"

if [ ! -d "$SOURCE" ]; then
    SOURCE="$SCRIPT_DIR/DeepGlow2.ofx.bundle"
fi

if [ ! -d "$SOURCE" ]; then
    echo "Error: DeepGlow2.ofx.bundle not found."
    echo "Expected at: $SOURCE"
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
    DEST="/usr/OFX/Plugins/DeepGlow2.ofx.bundle"
    mkdir -p /usr/OFX/Plugins
    echo "Installing system-wide to: $DEST"
    cp -r "$SOURCE" "$DEST"
    chmod -R 755 "$DEST"
else
    DEST="$HOME/.OFX/Plugins/DeepGlow2.ofx.bundle"
    mkdir -p "$HOME/.OFX/Plugins"
    echo "Installing for current user to: $DEST"
    cp -r "$SOURCE" "$DEST"
    chmod -R 755 "$DEST"
fi

echo "Installation complete!"
echo "You may need to restart DaVinci Resolve for the plugin to appear."
exit 0
