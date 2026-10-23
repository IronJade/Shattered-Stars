#!/bin/bash
# Cross-builds the Windows release with MinGW-w64 and packs it as dist/ShatteredStars-win64.zip
# (shattered-stars.exe, SDL3.dll, README). Needs Fedora's mingw64-gcc-c++ and mingw64-SDL3:
#   sudo dnf install mingw64-gcc-c++ mingw64-SDL3
# The C / C++ runtimes are linked in, so the zip holds everything the game needs; the discs
# and memory cards are the player's own.
set -eu
cd "$(dirname "$0")/.."
toolchain=/usr/share/mingw/toolchain-mingw64.cmake
[ -f "$toolchain" ] || { echo "MinGW toolchain not found ($toolchain): install mingw64-gcc-c++" >&2; exit 1; }
cmake -S engine -B engine/build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build engine/build-win -j8 --target shattered-stars
out=dist/ShatteredStars-win64
rm -rf "$out" "$out.zip"
mkdir -p "$out"
cp engine/build-win/shattered-stars.exe engine/build-win/SDL3.dll "$out/"
x86_64-w64-mingw32-strip "$out/shattered-stars.exe"
cp engine/README.md "$out/README.md"
(cd dist && zip -qr ShatteredStars-win64.zip ShatteredStars-win64)
echo "dist/ShatteredStars-win64.zip: $(du -h dist/ShatteredStars-win64.zip | cut -f1)"
