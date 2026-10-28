#!/bin/bash
# Builds the macOS release and packs it as dist/ShatteredStars-<version>-macos-arm64.zip
# (e.g. dist/ShatteredStars-0.03.00-macos-arm64.zip): shattered-stars (the game),
# shattered-stars-setup (the settings window), the README, this project's own LICENSE, the
# third-party licences and an empty discs/ folder for the player's disc images. The version is
# read from port/CMakeLists.txt's project(... VERSION ...) line (the single source;
# docs/release/release_checklist.md section 1).
#
#   port/tools/build_macos.sh
#
# Needs Xcode (or its command line tools), CMake and Ninja; without Homebrew:
#   pip3 install --user cmake ninja     (then ~/Library/Python/3.9/bin on PATH)
# SDL 3 and Dear ImGui are built from source by the project's CMake (FetchContent, pinned by
# SHA-256) and linked in; the programs need only macOS's own libraries (checked below).
#
# MACOS_MIN: the oldest macOS the programs run on (default 11.0, the first for Apple Silicon;
# without it Apple clang targets the building Mac's own version). ARCHS: CMake's
# CMAKE_OSX_ARCHITECTURES (default arm64; "arm64;x86_64" for a universal build, which a Mac
# without Rosetta can't test).
# OPENSSL_ROOT_DIR: a static OpenSSL 3 (include/, lib/libssl.a, lib/libcrypto.a, LICENSE.txt)
# for the target architectures: wss://, which hosted Archipelago rooms need. Without it the
# Archipelago client speaks ws:// only (a warning says so).
# JOBS: parallel compile jobs (default 8; a lighter build: JOBS=4).
set -eu
cd "$(dirname "$0")/../.."
version=$(sed -n -E 's/^project\(.*VERSION ([0-9]+\.[0-9]+\.[0-9]+)\).*/\1/p' port/CMakeLists.txt)
[ -n "$version" ] || { echo "error: could not read the port version out of port/CMakeLists.txt" >&2; exit 1; }
min=${MACOS_MIN:-11.0}
archs=${ARCHS:-arm64}
case "$archs" in
  arm64) arch_name=arm64 ;;
  x86_64) arch_name=x86_64 ;;
  *) arch_name=universal ;;
esac
build=port/build-macos-release
ssl=()
if [ -n "${OPENSSL_ROOT_DIR:-}" ]; then
  r=$(cd "$OPENSSL_ROOT_DIR" && pwd)
  ssl=(-DOPENSSL_ROOT_DIR="$r")
  export OPENSSL_LICENSE_FILE="$r/LICENSE.txt"
fi
# OPENSSL_USE_STATIC_LIBS: never a Homebrew OpenSSL's dylib, which players don't have.
cmake -S port -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET="$min" \
  -DCMAKE_OSX_ARCHITECTURES="$archs" -DOPENSSL_USE_STATIC_LIBS=TRUE ${ssl[@]+"${ssl[@]}"} \
  > "$build.configure.log" 2>&1 || { cat "$build.configure.log" >&2; exit 1; }
grep -q "OpenSSL not found" "$build.configure.log" && echo "warning: no OpenSSL: Archipelago will speak ws:// only" >&2
cmake --build "$build" -j"${JOBS:-8}" --target shattered-stars shattered-stars-setup

out=dist/ShatteredStars-$version-macos-$arch_name
rm -rf "$out" "$out.zip"
mkdir -p "$out/discs"
cp "$build/shattered-stars" "$build/shattered-stars-setup" "$out/"
for f in "$out/shattered-stars" "$out/shattered-stars-setup"; do
  strip -x "$f"
  # Apple Silicon runs only signed code; the linker's ad-hoc signature doesn't survive strip.
  codesign --force --sign - "$f" 2> /dev/null
  codesign --verify --strict "$f"
  # Only macOS's own libraries (/usr/lib, /System): nothing from Homebrew or the build.
  if otool -L "$f" | tail -n +2 | awk '{ print $1 }' | grep -v -E '^(/usr/lib/|/System/Library/)'; then
    echo "error: $f links a library players don't have (above)" >&2
    exit 1
  fi
done

cp port/README.md "$out/README.md"
printf '%s\n' \
  "Shattered Stars $version for macOS ($arch_name)" \
  "" \
  "Run ./shattered-stars-setup to choose your disc images and settings, then press Save & Play;" \
  "or run ./shattered-stars, which opens setup by itself while it has no disc. README.md has more." \
  "" \
  "Needs: macOS $min or newer$([ "$arch_name" = arm64 ] && echo " on Apple Silicon (M1 or newer)")." \
  "Everything else is included: SDL3 is built into the programs." \
  "" \
  "macOS blocks programs downloaded from the internet that Apple hasn't checked (these aren't" \
  "signed with an Apple Developer ID): \"shattered-stars\" Not Opened. Either, once, in Terminal" \
  "from this folder:" \
  "    xattr -dr com.apple.quarantine ." \
  "or try to open each program once, then System Settings > Privacy & Security > Open Anyway." \
  "" \
  "Full screen: Option+Return (F11 is macOS's Show Desktop key unless Fn is held)." \
  "Settings, memory cards and mods/: ~/Library/Application Support/ShatteredStars/" \
  "(or \$XDG_CONFIG_HOME/shattered-stars/)." \
  "Licence: MIT (LICENSE); third-party components: THIRD-PARTY-LICENSES.txt." \
  > "$out/README.txt"
printf '%s\n' \
  "Put your Star Ocean: The Second Story disc images in this folder (North American" \
  "release): each disc's .cue file with its .bin, or an .iso / .img image. Subfolders are" \
  "fine. The game finds them here by itself; setup's Auto-detect button looks here too." \
  > "$out/discs/Put your disc images here.txt"

# Third-party licences: SDL, Dear ImGui and, when the Archipelago client is linked in,
# apclientpp and its header-only dependencies, OpenSSL and zlib (zlib.h's own licence comment:
# the SDK's). Shattered Stars' own licence (MIT) sits beside it.
cp LICENSE "$out/LICENSE"
port/tools/third_party_licenses.sh "$build" "$build/_deps/sdl3-src/LICENSE.txt" > "$out/THIRD-PARTY-LICENSES.txt"

# ditto: the zip Finder's Archive Utility makes (permissions kept; the signatures are inside the
# programs), without the build Mac's extended attributes (they'd add ._ files).
(cd dist && ditto -c -k --norsrc --noextattr --noacl --keepParent "ShatteredStars-$version-macos-$arch_name" \
   "ShatteredStars-$version-macos-$arch_name.zip")
echo "$out.zip: $(du -h "$out.zip" | cut -f1), macOS $min+ ($archs)"
