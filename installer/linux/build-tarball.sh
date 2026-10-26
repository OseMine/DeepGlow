#!/usr/bin/env bash
# Package DeepGlow 2 for Linux distribution
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_BUNDLE="$ROOT/build/bundle/DeepGlow2.ofx.bundle"
DIST_DIR="$ROOT/installer/dist"

mkdir -p "$DIST_DIR"

if [ ! -d "$BUILD_BUNDLE" ]; then
    echo "Error: Bundle not found at $BUILD_BUNDLE"
    echo "Build first with: cmake --build build --config Release"
    exit 1
fi

VERSION="${VERSION:-1.0.0}"
PKG_NAME="DeepGlow2-Linux-x86_64-$VERSION"
TMPDIR_PKG="$(mktemp -d)"
PKG_ROOT="$TMPDIR_PKG/$PKG_NAME"
mkdir -p "$PKG_ROOT"
cp -r "$BUILD_BUNDLE" "$PKG_ROOT/"
cp "$SCRIPT_DIR/install.sh" "$PKG_ROOT/"
chmod +x "$PKG_ROOT/install.sh"

cat > "$PKG_ROOT/README.txt" << 'EOF'
DeepGlow 2 - OFX Plugin for DaVinci Resolve

Installation:
1. Run ./install.sh (as root for system-wide, or as user for ~/.OFX/Plugins)
2. Restart DaVinci Resolve
3. Look for "Deep Glow 2" in the OpenFX category

Manual installation:
- System-wide: copy DeepGlow2.ofx.bundle to /usr/OFX/Plugins/
- User: copy DeepGlow2.ofx.bundle to ~/.OFX/Plugins/
EOF

cd "$TMPDIR_PKG"
tar -czf "$DIST_DIR/$PKG_NAME.tar.gz" "$PKG_NAME"
rm -rf "$TMPDIR_PKG"

echo "Created: $DIST_DIR/$PKG_NAME.tar.gz"
