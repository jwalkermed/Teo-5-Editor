#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 The TEO-5 Editor project
# Builds the Release plugins (universal: Apple Silicon + Intel) and packages them with the installer into dist/.
# Usage: packaging/make-zip.sh
set -e
cd "$(dirname "$0")/.."

cmake --build build --config Release --target TEO5Editor_VST3 TEO5Editor_AU TEO5Editor_Standalone TEO5PluginCheck
packaging/make-manual.sh

ART=build/TEO5Editor_artefacts/Release
STAGE=dist/TEO5Editor
VERSION=$(grep -m1 -oE 'project\(TEO5Editor VERSION [0-9.]+' CMakeLists.txt | awk '{print $3}')
ZIP="dist/TEO-5-Editor-${VERSION}-macOS-Universal.zip"
NAME="TEO-5 Editor"

rm -rf dist && mkdir -p "$STAGE"
cp -R "$ART/VST3/$NAME.vst3" "$ART/AU/$NAME.component" "$ART/Standalone/$NAME.app" "$STAGE/"
cp build/TEO5PluginCheck_artefacts/Release/TEO5PluginCheck "$STAGE/"
for b in "$STAGE/$NAME".* "$STAGE/TEO5PluginCheck"; do
  codesign --force --deep --sign - "$b"   # ad-hoc signature (not notarized)
done
cp packaging/install.command packaging/README.txt THIRD-PARTY-NOTICES.md CHANGELOG.md "$STAGE/"
cp LICENSE "$STAGE/LICENSE.txt"
cp build/manual/TEO-5-Editor-Users-Guide.pdf "$STAGE/"
chmod +x "$STAGE/install.command"
# No resource forks / extended attributes: some unzip tools turn them into "._*" files inside the bundles,
# which breaks the code signature and makes DAWs reject the VST3.
xattr -cr "$STAGE" 2>/dev/null || true
(cd dist && ditto -c -k --norsrc --noextattr --noacl --keepParent TEO5Editor "$(basename "$ZIP")")
echo "Created $ZIP"
