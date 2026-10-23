#!/bin/bash
# Builds the Linux release and packs it as dist/ShatteredStars-linux-x86_64.tar.gz:
# shattered-stars (the game), shattered-stars-setup (the settings window), libSDL3.so.0,
# the README, the third-party licences and an empty discs/ folder for the player's images.
#
# The C / C++ runtimes' C++ half (libstdc++, libgcc) is linked in; SDL3 is the system's,
# copied next to the programs, which find it there first (RUNPATH $ORIGIN). SDL loads the
# display and sound libraries (X11 / Wayland, PipeWire / PulseAudio / ALSA) at run time, so
# the package needs nothing but glibc: the newest version any of the three files asks for,
# printed at the end and written in the package's README.txt. That is the build system's
# own (Fedora 44: 2.43, from its SDL3); build on an older distribution to reach older
# systems. Needs SDL3 development files and the static C++ library:
#   sudo dnf install SDL3-devel libstdc++-static
# Without root, unpack libstdc++-static's RPM (dnf download libstdc++-static; rpm2cpio |
# cpio -idm) and point LIBSTDCXX_STATIC_DIR at the folder holding its libstdc++.a.
set -eu
cd "$(dirname "$0")/.."
build=engine/build-linux
link="-static-libstdc++ -static-libgcc"
if ! echo 'int main() {}' | c++ -x c++ - $link -o /dev/null 2>/dev/null; then
  dir=${LIBSTDCXX_STATIC_DIR:-}
  if [ -n "$dir" ] && [ -f "$dir/libstdc++.a" ]; then
    link="$link -L$(cd "$dir" && pwd)"
  else
    echo "No static libstdc++: sudo dnf install libstdc++-static, or set LIBSTDCXX_STATIC_DIR" >&2
    exit 1
  fi
fi
cmake -S engine -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXE_LINKER_FLAGS="$link" -DCMAKE_BUILD_RPATH='$ORIGIN' > /dev/null
cmake --build "$build" -j8 --target shattered-stars shattered-stars-setup

out=dist/ShatteredStars-linux-x86_64
rm -rf "$out" "$out.tar.gz"
mkdir -p "$out/discs"
cp "$build/shattered-stars" "$build/shattered-stars-setup" "$out/"
strip "$out/shattered-stars" "$out/shattered-stars-setup"

# The SDL3 the programs were linked with.
sdl=$(ldd "$build/shattered-stars" | awk '$1 == "libSDL3.so.0" { print $3 }')
[ -f "$sdl" ] || { echo "libSDL3.so.0 not found for $build/shattered-stars" >&2; exit 1; }
cp -L "$sdl" "$out/libSDL3.so.0"
strip --strip-unneeded "$out/libSDL3.so.0"

# The newest glibc symbol any of them needs: the oldest system the package runs on.
glibc=$(objdump -T "$out/shattered-stars" "$out/shattered-stars-setup" "$out/libSDL3.so.0" |
        grep -o 'GLIBC_[0-9.]*' | sort -t. -k1,1 -k2,2n -k3,3n -u | tail -1 | cut -d_ -f2)

cp engine/README.md "$out/README.md"
printf '%s\n' \
  "Shattered Stars for Linux (x86-64)" \
  "" \
  "Run ./shattered-stars-setup to choose your disc images and settings, then press Save & Play;" \
  "or run ./shattered-stars, which opens setup by itself while it has no disc. README.md has more." \
  "" \
  "Needs: glibc $glibc or newer, and a desktop with OpenGL 3.3. Everything else is included:" \
  "SDL3 is libSDL3.so.0 beside the programs. (glibc $glibc is the version of the system this" \
  "package was built on; built on an older distribution, it runs on older systems too.)" \
  "Settings, memory cards and mods/: ~/.config/shattered-stars/ (or \$XDG_CONFIG_HOME)." \
  > "$out/README.txt"
printf '%s\n' \
  "Put your Star Ocean: The Second Story disc images in this folder (North American" \
  "release): each disc's .cue file with its .bin, or an .iso / .img image. Subfolders are" \
  "fine. The game finds them here by itself; setup's Auto-detect button looks here too." \
  > "$out/discs/Put your disc images here.txt"

# Third-party licences (SDL: zlib; Dear ImGui: MIT).
{
  echo "Shattered Stars includes SDL 3 (libSDL3.so.0) and Dear ImGui (in shattered-stars-setup)."
  echo
  echo "=== SDL 3 ==="
  cat /usr/share/licenses/SDL3/LICENSE.txt 2>/dev/null || echo "zlib licence: https://www.libsdl.org/license.php"
  echo
  echo "=== Dear ImGui ==="
  cat "$build/_deps/imgui-src/LICENSE.txt"
} > "$out/THIRD-PARTY-LICENSES.txt"

tar -C dist -czf "$out.tar.gz" ShatteredStars-linux-x86_64
echo "$out.tar.gz: $(du -h "$out.tar.gz" | cut -f1), needs glibc $glibc+"
