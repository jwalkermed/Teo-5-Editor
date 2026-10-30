#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 The TEO-5 Editor project
# Installs TEO-5 Editor from this folder:
#   VST3       -> /Library/Audio/Plug-Ins/VST3
#   AU         -> /Library/Audio/Plug-Ins/Components
#   Standalone -> /Applications
# The build isn't notarized, so the macOS download quarantine flag is removed from the installed copies.
# A quarantined VST3 stalls at a Gatekeeper prompt inside the DAW's background scanner, and the DAW then
# leaves it out of its plug-in list. Running this again repairs an existing install.
set -e
cd "$(dirname "$0")"

VST3_DIR="/Library/Audio/Plug-Ins/VST3"
USER_VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="/Library/Audio/Plug-Ins/Components"
USER_AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
APP_DIR="/Applications"

# Use sudo only where a folder isn't writable by this user.
run() { if [ -w "$1" ]; then shift; "$@"; else shift; sudo "$@"; fi; }

install_bundle() { # src destdir
  local src="$1" dest="$2"
  local target="$dest/$(basename "$src")"
  mkdir -p "$dest" 2>/dev/null || sudo mkdir -p "$dest"
  run "$dest" rm -rf "$target"
  run "$dest" cp -R "$src" "$dest/"
  run "$dest" find "$target" -name '._*' -delete
  run "$dest" xattr -dr com.apple.quarantine "$target" 2>/dev/null || true
  run "$dest" codesign --force --deep --sign - "$target" >/dev/null 2>&1 || true
  run "$dest" chmod -R a+rX "$target"
  if codesign --verify --deep --strict "$target" 2>/dev/null; then
    echo "  installed $target"
  else
    echo "  installed $target  (WARNING: code signature invalid; DAWs may reject it)"
  fi
}

# This folder came from a download: clear the flag here too, so nothing below triggers Gatekeeper.
# Some unzip tools also leave "._*" metadata files inside the bundles; they invalidate the code signature.
xattr -dr com.apple.quarantine . 2>/dev/null || true
find . -name '._*' -delete 2>/dev/null || true

echo "Installing TEO-5 Editor..."

NAME="TEO-5 Editor"

# Copies that would clash with the ones installed below (same plug-in IDs): anything in the per-user folders,
# and the bundles named "TEO5Editor" from before 1.0.
remove_old() { # path
  if [ -e "$1" ]; then
    rm -rf "$1" 2>/dev/null || sudo rm -rf "$1"
    echo "  removed old copy $1"
  fi
}
for n in "TEO5Editor" "$NAME"; do
  remove_old "$USER_VST3_DIR/$n.vst3"
  remove_old "$USER_AU_DIR/$n.component"
done
remove_old "$VST3_DIR/TEO5Editor.vst3"
remove_old "$AU_DIR/TEO5Editor.component"
remove_old "$APP_DIR/TEO5Editor.app"

install_bundle "$NAME.vst3" "$VST3_DIR"
install_bundle "$NAME.component" "$AU_DIR"
install_bundle "$NAME.app" "$APP_DIR"

# Make Logic / GarageBand rescan Audio Units.
killall -9 AudioComponentRegistrar 2>/dev/null || true

# Load the installed VST3 the way a DAW's scanner does (gives up after 30 s).
echo
if [ -x ./TEO5PluginCheck ]; then
  perl -e 'alarm shift; exec @ARGV' 30 ./TEO5PluginCheck "$VST3_DIR/$NAME.vst3" \
    || echo "FAIL: the VST3 did not load (exit $?). Send this window's text to the developer."
fi

echo
echo "Done. Now make your DAW rescan:"
echo "  Ableton Live: Settings > Plug-Ins > turn on 'Use VST3 Plug-In System Folders',"
echo "                then hold Option and click 'Rescan'. Look under Plug-Ins > TEO-5 Editor."
echo "  Bitwig:       Settings > Locations > Plug-in Locations: make sure /Library/Audio/Plug-Ins/VST3"
echo "                is listed, then Settings > Plug-ins and click 'Rescan'; if TEO-5 Editor is listed"
echo "                as failed/blocked there, re-enable it."
