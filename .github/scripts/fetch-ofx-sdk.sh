#!/usr/bin/env bash
# Fetches the OpenFX SDK that CI builds the plugin against.
#
# The OFX_Release_1_4_TAG Support library is pre-C++11 (std::auto_ptr, dynamic
# exception specifications) and no longer compiles as C++17 with current MSVC,
# Apple clang or gcc, so it cannot be used for a C++17 plugin build. Upstream
# master has modernised it, which is what DaVinci Resolve's own SDK ships too.
#
# Pinning a commit keeps CI reproducible; bump OFX_SDK_COMMIT to pick up fixes.
set -euo pipefail

OFX_SDK_REPO="https://github.com/AcademySoftwareFoundation/openfx.git"
# openfx master, 2026-10-15 ("Fix typo in error message (#285)")
OFX_SDK_COMMIT="c3f1c2dcf4f51762b794c4fc21181b4a501be1bf"

dest="${1:-openfx-sdk}"

rm -rf "$dest"
git init --quiet "$dest"
git -C "$dest" remote add origin "$OFX_SDK_REPO"
# --depth 1 on an explicit commit: GitHub allows fetching reachable SHAs.
git -C "$dest" fetch --quiet --depth 1 origin "$OFX_SDK_COMMIT"
git -C "$dest" checkout --quiet --detach FETCH_HEAD

echo "OpenFX SDK: $(git -C "$dest" log -1 --format='%h %cs %s')"
