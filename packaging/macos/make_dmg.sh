#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Packs the built SoundFunnel.app into a compressed disk image.
#   packaging/macos/make_dmg.sh <path/to/SoundFunnel.app> <output.dmg>
set -eu

app="$1"
out="$2"
[ -d "$app" ] || { echo "no app bundle: $app" >&2; exit 1; }

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

cp -R "$app" "$stage/"
ln -s /Applications "$stage/Applications"

# ad-hoc signature (no Apple Developer ID yet): Gatekeeper asks once,
# see README "macOS で初めて開くとき"
codesign --force --deep --sign - "$stage/$(basename "$app")"

rm -f "$out"
hdiutil create -volname SoundFunnel -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$out"
echo "wrote $out"
