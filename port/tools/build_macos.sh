#!/bin/bash
# Builds the macOS release (Apple Silicon) and packs it as
# dist/ShatteredStars-<version>-macos-arm64.zip (e.g. dist/ShatteredStars-0.03.00-macos-arm64.zip):
# two apps, "Shattered Stars.app" (the game) and "Shattered Stars Setup.app" (the settings
# window), each holding both programs so each works alone (the game opens setup, setup's Save &
# Play starts the game: launcher/process.h), with the README, this project's own LICENSE and the
# third-party licences. The player's disc images go in ~/Library/Application Support/
# ShatteredStars/discs (an .app's contents stay as signed). The version is read from
# port/CMakeLists.txt's project(... VERSION ...) line (the single source;
# docs/release/release_checklist.md section 1).
#
#   port/tools/build_macos.sh
#
# Needs Xcode (or its command line tools), CMake and Ninja; without Homebrew:
#   pip3 install --user cmake ninja     (then ~/Library/Python/3.9/bin on PATH)
# SDL 3 and Dear ImGui are built from source by the project's CMake (FetchContent, pinned by
# SHA-256) and linked in; the programs need only macOS's own libraries (checked below). The apps
# are signed ad hoc (no Apple Developer ID): the README.txt says how a player opens them.
#
# MACOS_MIN: the oldest macOS the programs run on (default 11.0, the first for Apple Silicon;
# without it Apple clang targets the building Mac's own version).
# OPENSSL_ROOT_DIR: a static arm64 OpenSSL 3 (include/, lib/libssl.a, lib/libcrypto.a,
# LICENSE.txt): wss://, which hosted Archipelago rooms need. Without it the Archipelago client
# speaks ws:// only (a warning says so).
# JOBS: parallel compile jobs (default 8; a lighter build: JOBS=4).
set -eu
cd "$(dirname "$0")/../.."
version=$(sed -n -E 's/^project\(.*VERSION ([0-9]+\.[0-9]+\.[0-9]+)\).*/\1/p' port/CMakeLists.txt)
[ -n "$version" ] || { echo "error: could not read the port version out of port/CMakeLists.txt" >&2; exit 1; }
min=${MACOS_MIN:-11.0}
build=port/build-macos-release
ssl=()
if [ -n "${OPENSSL_ROOT_DIR:-}" ]; then
  r=$(cd "$OPENSSL_ROOT_DIR" && pwd)
  ssl=(-DOPENSSL_ROOT_DIR="$r")
  export OPENSSL_LICENSE_FILE="$r/LICENSE.txt"
fi
# OPENSSL_USE_STATIC_LIBS: never a Homebrew OpenSSL's dylib, which players don't have.
cmake -S port -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET="$min" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DOPENSSL_USE_STATIC_LIBS=TRUE ${ssl[@]+"${ssl[@]}"} \
  > "$build.configure.log" 2>&1 || { cat "$build.configure.log" >&2; exit 1; }
grep -q "OpenSSL not found" "$build.configure.log" && echo "warning: no OpenSSL: Archipelago will speak ws:// only" >&2
cmake --build "$build" -j"${JOBS:-8}" --target shattered-stars shattered-stars-setup

# The programs, stripped, with only macOS's own libraries (/usr/lib, /System): nothing from
# Homebrew or the build.
mkdir -p "$build/stripped"
for p in shattered-stars shattered-stars-setup; do
  cp "$build/$p" "$build/stripped/$p"
  strip -x "$build/stripped/$p"
  if otool -L "$build/stripped/$p" | tail -n +2 | awk '{ print $1 }' | grep -v -E '^(/usr/lib/|/System/Library/)'; then
    echo "error: $p links a library players don't have (above)" >&2
    exit 1
  fi
done
# The icon, from port/res/icon's PNGs (make_icon.py draws up to 256 px).
icons=$build/shattered-stars.iconset
rm -rf "$icons"
mkdir -p "$icons"
i=port/res/icon/shattered-stars
cp "$i-16.png" "$icons/icon_16x16.png"
cp "$i-32.png" "$icons/icon_16x16@2x.png"
cp "$i-32.png" "$icons/icon_32x32.png"
cp "$i-64.png" "$icons/icon_32x32@2x.png"
cp "$i-128.png" "$icons/icon_128x128.png"
cp "$i-256.png" "$icons/icon_128x128@2x.png"
cp "$i-256.png" "$icons/icon_256x256.png"
iconutil -c icns -o "$build/shattered-stars.icns" "$icons"
port/tools/third_party_licenses.sh "$build" "$build/_deps/sdl3-src/LICENSE.txt" > "$build/THIRD-PARTY-LICENSES.txt"

out=dist/ShatteredStars-$version-macos-arm64
rm -rf "$out" "$out.zip"
mkdir -p "$out"
make_app() {  # NAME EXECUTABLE IDENTIFIER
  local app="$out/$1.app" other=shattered-stars-setup
  [ "$2" = shattered-stars-setup ] && other=shattered-stars
  mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
  sed -e "s|@NAME@|$1|g" -e "s|@EXECUTABLE@|$2|g" -e "s|@IDENTIFIER@|$3|g" -e "s|@VERSION@|$version|g" \
    -e "s|@MACOS_MIN@|$min|g" port/res/macos/Info.plist.in > "$app/Contents/Info.plist"
  plutil -lint -s "$app/Contents/Info.plist"
  printf 'APPL????' > "$app/Contents/PkgInfo"
  cp "$build/stripped/shattered-stars" "$build/stripped/shattered-stars-setup" "$app/Contents/MacOS/"
  cp "$build/shattered-stars.icns" LICENSE "$build/THIRD-PARTY-LICENSES.txt" "$app/Contents/Resources/"
  # Apple Silicon runs only signed code. Ad hoc: the second program first, then the app (its
  # main program, Info.plist and resources).
  codesign --force --sign - "$app/Contents/MacOS/$other" 2> /dev/null
  codesign --force --sign - "$app" 2> /dev/null
  codesign --verify --deep --strict "$app"
}
make_app "Shattered Stars" shattered-stars io.github.IronJade.ShatteredStars
make_app "Shattered Stars Setup" shattered-stars-setup io.github.IronJade.ShatteredStars.Setup

cp port/README.md "$out/README.md"
printf '%s\n' \
  "Shattered Stars $version for macOS (Apple Silicon)" \
  "" \
  "Shattered Stars Setup: choose your disc images and settings, then press Save & Play." \
  "Shattered Stars: the game (it opens setup by itself while it has no disc). README.md has more." \
  "Both apps can go in Applications, or anywhere else." \
  "" \
  "Your disc images: choose them in setup (Browse..., or Auto-detect), or put them in" \
  "    ~/Library/Application Support/ShatteredStars/discs" \
  "(setup's Open discs folder button opens it), where the game finds them by itself." \
  "" \
  "Needs: macOS $min or newer on Apple Silicon (M1 or newer)." \
  "Everything else is included: SDL3 is built into the programs." \
  "" \
  "macOS blocks apps downloaded from the internet that Apple hasn't checked (these aren't" \
  "signed with an Apple Developer ID): \"Shattered Stars\" Not Opened. Either, once, in Terminal" \
  "in the folder that holds the two apps:" \
  "    xattr -dr com.apple.quarantine *.app" \
  "or try to open each app once, then System Settings > Privacy & Security > Open Anyway." \
  "" \
  "Full screen: Option+Return or the window's green button (F11 is macOS's Show Desktop key" \
  "unless Fn is held)." \
  "Settings, memory cards and mods/: ~/Library/Application Support/ShatteredStars/" \
  "Licence: MIT (LICENSE); third-party components: THIRD-PARTY-LICENSES.txt." \
  > "$out/README.txt"
cp LICENSE "$build/THIRD-PARTY-LICENSES.txt" "$out/"

# ditto: the zip Finder's Archive Utility makes (permissions and the apps' signatures kept),
# without the build Mac's extended attributes (they'd add ._ files).
(cd dist && ditto -c -k --norsrc --noextattr --noacl --keepParent "ShatteredStars-$version-macos-arm64" \
   "ShatteredStars-$version-macos-arm64.zip")
echo "$out.zip: $(du -h "$out.zip" | cut -f1), macOS $min+ (arm64)"
