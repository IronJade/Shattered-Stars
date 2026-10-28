#!/bin/bash
# Builds the Linux release and packs it as dist/ShatteredStars-<version>-linux-x86_64.tar.gz
# (e.g. dist/ShatteredStars-0.01.00-linux-x86_64.tar.gz): shattered-stars (the game),
# shattered-stars-setup (the settings window), the README, this project's own LICENSE, the
# third-party licences, an empty discs/ folder for the player's disc images, and the optional
# desktop entry (install-desktop-entry.sh, with desktop/: .desktop files, the icon). The version
# is read from port/CMakeLists.txt's project(... VERSION ...) line (the single source;
# docs/release/release_checklist.md section 1), so it never needs updating here by hand.
#
#   port/tools/build_linux.sh --container   the release: built in a Debian 12 container
#                                      (port/tools/linux-build/Containerfile, podman), so it runs
#                                      on any distribution with glibc 2.36 or newer
#   port/tools/build_linux.sh               built on this machine (needs this machine's glibc)
#
# --container: SDL 3 and Dear ImGui are built from source inside the container by the
# project's CMake (FetchContent, pinned by SHA-256), SDL statically with its display and
# sound backends, which it loads at run time; libstdc++ / libgcc are linked in. The programs
# need only glibc. The repository is mounted read-only; the build goes to
# port/build-linux-container. Nothing is pushed anywhere.
#
# Host build: needs SDL3 development files and the static C++ library
#   sudo dnf install SDL3-devel libstdc++-static
# (without root: unpack libstdc++-static's RPM and set LIBSTDCXX_STATIC_DIR to the folder
# holding its libstdc++.a). The system's SDL3 is copied next to the programs (RUNPATH
# $ORIGIN); the glibc floor is this machine's (Fedora 44: 2.43).
#
# JOBS: parallel compile jobs (default 8; a lighter build: JOBS=4).
set -eu
cd "$(dirname "$0")/../.."
repo=$PWD
mode=host
[ "${1:-}" = --container ] && mode=container
version=$(sed -n 's/^project(.*VERSION \([0-9]\+\.[0-9]\+\.[0-9]\+\)).*/\1/p' port/CMakeLists.txt)
[ -n "$version" ] || { echo "error: could not read the port version out of port/CMakeLists.txt" >&2; exit 1; }

if [ "$mode" = container ]; then
  image=localhost/shattered-stars-linux-build:bookworm
  podman build -q -t "$image" -f port/tools/linux-build/Containerfile port/tools/linux-build > /dev/null
  build=port/build-linux-container
  mkdir -p "$build"
  # label=disable: the read-only mount needs no SELinux relabelling of the repository.
  podman run --rm --security-opt label=disable -v "$repo":/src:ro -v "$repo/$build":/build -w /src "$image" \
    bash -c 'cmake -S port -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
               -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
               -DOPENSSL_USE_STATIC_LIBS=TRUE -DZLIB_USE_STATIC_LIBS=ON > /dev/null &&
             cmake --build /build -j'"${JOBS:-8}"' --target shattered-stars shattered-stars-setup'
else
  build=port/build-linux
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
  cmake -S port -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="$link" -DCMAKE_BUILD_RPATH='$ORIGIN' > /dev/null
  cmake --build "$build" -j"${JOBS:-8}" --target shattered-stars shattered-stars-setup
fi

out=dist/ShatteredStars-$version-linux-x86_64
rm -rf "$out" "$out.tar.gz"
mkdir -p "$out/discs"
cp "$build/shattered-stars" "$build/shattered-stars-setup" "$out/"
strip "$out/shattered-stars" "$out/shattered-stars-setup"
files=("$out/shattered-stars" "$out/shattered-stars-setup")

# The desktop entry, optional (README.md, "Linux: the application menu and the taskbar"):
# install-desktop-entry.sh adds desktop/'s two .desktop files (port/res/linux) and the icon
# (port/res/icon, drawn by port/tools/make_icon.py) in hicolor sizes to the player's ~/.local/share.
id=io.github.IronJade.ShatteredStars
mkdir -p "$out/desktop/icons/hicolor/scalable/apps"
cp "port/res/linux/$id.desktop" "port/res/linux/$id.Setup.desktop" "$out/desktop/"
for size in 16 24 32 48 64 128 256; do
  mkdir -p "$out/desktop/icons/hicolor/${size}x$size/apps"
  cp "port/res/icon/shattered-stars-$size.png" "$out/desktop/icons/hicolor/${size}x$size/apps/$id.png"
done
cp port/res/icon/shattered-stars.svg "$out/desktop/icons/hicolor/scalable/apps/$id.svg"
cp port/res/linux/install-desktop-entry.sh "$out/"
chmod 644 "$out"/desktop/*.desktop
chmod 755 "$out/install-desktop-entry.sh"

if [ "$mode" = host ]; then
  # The SDL3 the programs were linked with.
  sdl=$(ldd "$build/shattered-stars" | awk '$1 == "libSDL3.so.0" { print $3 }')
  [ -f "$sdl" ] || { echo "libSDL3.so.0 not found for $build/shattered-stars" >&2; exit 1; }
  cp -L "$sdl" "$out/libSDL3.so.0"
  strip --strip-unneeded "$out/libSDL3.so.0"
  files+=("$out/libSDL3.so.0")
  sdl_note="SDL3 is libSDL3.so.0 beside the programs."
  sdl_licence=/usr/share/licenses/SDL3/LICENSE.txt
else
  sdl_note="SDL3 is built into the programs."
  sdl_licence=$build/_deps/sdl3-src/LICENSE.txt
fi

# The newest glibc symbol any of them needs: the oldest system the package runs on.
glibc=$(objdump -T "${files[@]}" | grep -o 'GLIBC_[0-9.]*' | sort -t. -k1,1 -k2,2n -k3,3n -u | tail -1 | cut -d_ -f2)

cp port/README.md "$out/README.md"
printf '%s\n' \
  "Shattered Stars $version for Linux (x86-64)" \
  "" \
  "Run ./shattered-stars-setup to choose your disc images and settings, then press Save & Play;" \
  "or run ./shattered-stars, which opens setup by itself while it has no disc. README.md has more." \
  "" \
  "Needs: glibc $glibc or newer (check with: ldd --version), and a desktop with OpenGL 3.3." \
  "Everything else is included: $sdl_note" \
  "Settings, memory cards and mods/: ~/.config/shattered-stars/ (or \$XDG_CONFIG_HOME)." \
  "Licence: MIT (LICENSE); third-party components: THIRD-PARTY-LICENSES.txt." \
  "" \
  "Optional: ./install-desktop-entry.sh adds both programs to your application menu with" \
  "their icon (GNOME and KDE Plasma on Wayland need it to show the icon in the taskbar);" \
  "./install-desktop-entry.sh --uninstall removes them again." \
  > "$out/README.txt"
printf '%s\n' \
  "Put your Star Ocean: The Second Story disc images in this folder (North American" \
  "release): each disc's .cue file with its .bin, or an .iso / .img image. Subfolders are" \
  "fine. The game finds them here by itself; setup's Auto-detect button looks here too." \
  > "$out/discs/Put your disc images here.txt"

# Third-party licences: SDL, Dear ImGui and, when the Archipelago client is linked in,
# apclientpp and its header-only dependencies, OpenSSL and zlib. Shattered Stars' own
# licence (MIT) sits beside it.
cp LICENSE "$out/LICENSE"
port/tools/third_party_licenses.sh "$build" "$sdl_licence" > "$out/THIRD-PARTY-LICENSES.txt"

tar -C dist -czf "$out.tar.gz" "ShatteredStars-$version-linux-x86_64"
echo "$out.tar.gz: $(du -h "$out.tar.gz" | cut -f1), needs glibc $glibc+ ($mode build)"
