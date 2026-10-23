#!/bin/bash
# Cross-builds the Windows release with MinGW-w64 and packs it as dist/ShatteredStars-win64.zip:
# shattered-stars.exe (the game), setup.exe (the settings window), SDL3.dll, the README, the
# third-party licences and an empty discs\ folder for the player's disc images. Needs
# Fedora's mingw64-gcc-c++ and mingw64-SDL3:
#   sudo dnf install mingw64-gcc-c++ mingw64-SDL3
# The C / C++ runtimes are linked in, so the zip holds everything the game needs; the discs
# and memory cards are the player's own.
set -eu
cd "$(dirname "$0")/.."
toolchain=/usr/share/mingw/toolchain-mingw64.cmake
[ -f "$toolchain" ] || { echo "MinGW toolchain not found ($toolchain): install mingw64-gcc-c++" >&2; exit 1; }
cmake -S engine -B engine/build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build engine/build-win -j8 --target shattered-stars shattered-stars-setup
out=dist/ShatteredStars-win64
rm -rf "$out" "$out.zip"
mkdir -p "$out/discs"
cp engine/build-win/shattered-stars.exe engine/build-win/setup.exe engine/build-win/SDL3.dll "$out/"
x86_64-w64-mingw32-strip "$out/shattered-stars.exe" "$out/setup.exe"
cp engine/README.md "$out/README.md"
printf '%s\r\n' \
  "Put your Star Ocean: The Second Story disc images in this folder (North American" \
  "release): each disc's .cue file with its .bin, or an .iso / .img image. Subfolders are" \
  "fine. The game finds them here by itself; setup.exe's Auto-detect button looks here too." \
  > "$out/discs/Put your disc images here.txt"
# Third-party licences (SDL: zlib; Dear ImGui: MIT), with Windows line ends.
{
  echo "Shattered Stars includes SDL 3 (SDL3.dll) and Dear ImGui (in setup.exe)."
  echo
  echo "=== SDL 3 ==="
  cat /usr/x86_64-w64-mingw32/sys-root/mingw/share/licenses/SDL3/LICENSE.txt /usr/share/licenses/mingw64-SDL3/LICENSE.txt \
    2>/dev/null | head -n 400 || true
  echo
  echo "=== Dear ImGui ==="
  cat engine/build-win/_deps/imgui-src/LICENSE.txt
} | sed 's/$/\r/' > "$out/THIRD-PARTY-LICENSES.txt"
(cd dist && zip -qr ShatteredStars-win64.zip ShatteredStars-win64)
echo "dist/ShatteredStars-win64.zip: $(du -h dist/ShatteredStars-win64.zip | cut -f1)"
