#!/bin/bash
# Builds HOOKSMITH for Intel + Apple Silicon Macs and makes HOOKSMITH-Mac.zip
# (ready to email) in your Downloads folder.  Run:  bash package-mac.sh
set -e
cd "$(dirname "$0")"
CMAKE=cmake; command -v cmake >/dev/null || CMAKE=/Applications/CMake.app/Contents/bin/cmake
$CMAKE -B build-dist -DCMAKE_BUILD_TYPE=Release -DHS_COPY_AFTER_BUILD=OFF \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
$CMAKE --build build-dist --config Release -j8 --target Hooksmith_AU Hooksmith_VST3
A=build-dist/Hooksmith_artefacts/Release
codesign --force --deep --sign - "$A/AU/HOOKSMITH.component"
codesign --force --deep --sign - "$A/VST3/HOOKSMITH.vst3"
OUT=build-dist/HOOKSMITH-Mac
rm -rf "$OUT" && mkdir -p "$OUT"
cp -R "$A/AU/HOOKSMITH.component" "$A/VST3/HOOKSMITH.vst3" "$OUT/"
cp "Install/Install on Mac.txt" "$OUT/"
rm -f ~/Downloads/HOOKSMITH-Mac.zip
ditto -c -k --keepParent "$OUT" ~/Downloads/HOOKSMITH-Mac.zip
lipo -archs "$A/AU/HOOKSMITH.component/Contents/MacOS/HOOKSMITH"
echo "Done: ~/Downloads/HOOKSMITH-Mac.zip"
