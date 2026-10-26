#!/bin/bash
# DeepGlow 2 - macOS Installer (OFX Bundle)
# Installs to: /Library/OFX/Plugins/

set -e

SOURCE="$(cd "$(dirname "$0")/../.." && pwd)/bundle/DeepGlow2.ofx.bundle"
if [ ! -d "$SOURCE" ]; then
    SOURCE="$(dirname "$0")/DeepGlow2.ofx.bundle"
fi

DEST="/Library/OFX/Plugins/DeepGlow2.ofx.bundle"

if [ ! -d "$SOURCE" ]; then
    echo "Error: DeepGlow2.ofx.bundle not found."
    echo "Expected at: $SOURCE"
    exit 1
fi

echo "DeepGlow 2 Installer"
echo "Installing to: $DEST"
echo

if [ -d "$DEST" ]; then
    echo "Removing existing installation..."
    sudo rm -rf "$DEST"
fi

echo "Copying bundle (requires sudo)..."
sudo mkdir -p /Library/OFX/Plugins
sudo cp -R "$SOURCE" "$DEST"
sudo chmod -R 755 "$DEST"

echo
echo "Installation complete!"
echo "You may need to restart DaVinci Resolve for the plugin to appear."
exit 0
