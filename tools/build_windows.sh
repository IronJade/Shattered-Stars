#!/bin/bash
# Cross-builds the Windows release with MinGW-w64 and packs it as dist/ShatteredStars-win64.zip:
# shattered-stars.exe (the game), setup.exe (the settings window), SDL3.dll, the README, the
# third-party licences and an empty discs\ folder for the player's disc images. Needs
# Fedora's mingw64-gcc-c++ and mingw64-SDL3:
#   sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-openssl-static mingw64-zlib-static
# OpenSSL (wss:// for Archipelago servers) and zlib are linked in. Without root, unpack those
# two packages' RPMs (dnf download; rpm2cpio | cpio -idm) and set MINGW_SSL_SYSROOT to the
# unpacked usr/x86_64-w64-mingw32/sys-root/mingw. The release builds in engine/build-win-release
# (engine/build-win, the development build, may lack OpenSSL: ws:// only).
# The C / C++ runtimes are linked in, so the zip holds everything the game needs; the discs
# and memory cards are the player's own.
set -eu
cd "$(dirname "$0")/.."
toolchain=/usr/share/mingw/toolchain-mingw64.cmake
[ -f "$toolchain" ] || { echo "MinGW toolchain not found ($toolchain): install mingw64-gcc-c++" >&2; exit 1; }
build=engine/build-win-release
ssl=()
if [ -n "${MINGW_SSL_SYSROOT:-}" ]; then
  r=$(cd "$MINGW_SSL_SYSROOT" && pwd)
  # CMake's FindOpenSSL has a separate MINGW search path (CMakeLists.txt's
  # OPENSSL_SSL_LIBRARY / OPENSSL_CRYPTO_LIBRARY hints are not it: that branch resolves its
  # own LIB_EAY / SSL_EAY cache variables via find_library(), ignoring whatever
  # OPENSSL_SSL_LIBRARY / OPENSSL_CRYPTO_LIBRARY were pre-seeded to) -- and the MinGW
  # toolchain's CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY means a HINTS-based OPENSSL_ROOT_DIR
  # search would get the sysroot prepended onto our already-absolute path and find nothing,
  # so LIB_EAY / SSL_EAY must be pre-seeded directly (short-circuits the search entirely,
  # the same trick already used below for the ZLIB_LIBRARY / *_INCLUDE_DIR cache variables).
  ssl=(-DOPENSSL_INCLUDE_DIR="$r/include" -DLIB_EAY="$r/lib/libcrypto.a" -DSSL_EAY="$r/lib/libssl.a"
       -DZLIB_INCLUDE_DIR="$r/include" -DZLIB_LIBRARY="$r/lib/libz.a")
fi
cmake -S engine -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release \
  -DOPENSSL_USE_STATIC_LIBS=TRUE -DZLIB_USE_STATIC_LIBS=ON "${ssl[@]}" > "$build.configure.log" 2>&1 \
  || { cat "$build.configure.log" >&2; exit 1; }
grep -q "OpenSSL not found" "$build.configure.log" && echo "warning: no OpenSSL: Archipelago will speak ws:// only" >&2
cmake --build "$build" -j8 --target shattered-stars shattered-stars-setup
out=dist/ShatteredStars-win64
rm -rf "$out" "$out.zip"
mkdir -p "$out/discs"
cp "$build/shattered-stars.exe" "$build/setup.exe" "$build/SDL3.dll" "$out/"
x86_64-w64-mingw32-strip "$out/shattered-stars.exe" "$out/setup.exe"
cp engine/README.md "$out/README.md"
printf '%s\r\n' \
  "Put your Star Ocean: The Second Story disc images in this folder (North American" \
  "release): each disc's .cue file with its .bin, or an .iso / .img image. Subfolders are" \
  "fine. The game finds them here by itself; setup.exe's Auto-detect button looks here too." \
  > "$out/discs/Put your disc images here.txt"
# Third-party licences: SDL, Dear ImGui and, when the Archipelago client is linked in,
# apclientpp and its header-only dependencies, OpenSSL and zlib. Windows line ends.
tools/third_party_licenses.sh "$build" /usr/share/licenses/mingw64-SDL3/LICENSE.txt \
  | sed 's/$/\r/' > "$out/THIRD-PARTY-LICENSES.txt"
(cd dist && zip -qr ShatteredStars-win64.zip ShatteredStars-win64)
echo "dist/ShatteredStars-win64.zip: $(du -h dist/ShatteredStars-win64.zip | cut -f1)"
