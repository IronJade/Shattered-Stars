#!/bin/bash
# Prints THIRD-PARTY-LICENSES.txt for a release (port/tools/build_windows.sh, port/tools/build_linux.sh):
# the licences of what the programs include. Usage: third_party_licenses.sh BUILD_DIR SDL_LICENCE
#   SDL 3 (zlib), Dear ImGui (MIT, setup), and the Archipelago client: apclientpp (MIT),
#   wswrap (MIT), websocketpp (BSD-3-Clause), asio (Boost 1.0), nlohmann/json (MIT),
#   OpenSSL (Apache 2.0), zlib (zlib).
set -u
build=$1
sdl=$2
section() {  # TITLE FILE...: the first file that exists
  local title=$1
  shift
  echo "=== $title ==="
  for f in "$@"; do
    if [ -f "$f" ]; then
      cat "$f"
      echo
      return
    fi
  done
  echo "(licence text not found at build time)"
  echo
}
zlib_section() {  # no fixed licence file ships on every distribution: fall back to the
                   # licence comment at the top of the zlib.h this build actually used
                   # (CMakeCache's ZLIB_INCLUDE_DIR), else zlib.h on the $PATH's sysroots.
  echo "=== zlib ==="
  local f candidates=(/usr/share/licenses/zlib-ng-compat/LICENSE.md /usr/share/licenses/zlib/LICENSE \
    /usr/share/doc/zlib1g/copyright)
  for f in "${candidates[@]}"; do
    if [ -f "$f" ]; then
      cat "$f"
      echo
      return
    fi
  done
  local inc header
  inc=$(grep -m1 '^ZLIB_INCLUDE_DIR:PATH=' "$build/CMakeCache.txt" 2>/dev/null | cut -d= -f2-)
  for header in "$inc/zlib.h" /usr/include/zlib.h /usr/x86_64-w64-mingw32/sys-root/mingw/include/zlib.h; do
    if [ -n "$header" ] && [ -f "$header" ] && grep -q "jzlib\|Jean-loup Gailly" "$header" 2>/dev/null; then
      # The licence is the first /* ... */ comment holding the copyright line.
      awk '/Copyright.*Gailly/{p=1} p{print} p && /^\*\//{exit}' "$header"
      echo
      return
    fi
  done
  echo "(no packaged licence file; the zlib licence needs no written acknowledgment in the binary)"
  echo
}
echo "Shattered Stars includes the following third-party software."
echo
section "SDL 3" "$sdl"
section "Dear ImGui (in setup)" "$build/_deps/imgui-src/LICENSE.txt"
if [ -d "$build/_deps/apclientpp-src" ]; then
  section "apclientpp (Archipelago client)" "$build/_deps/apclientpp-src/LICENSE"
  section "wswrap" "$build/_deps/wswrap-src/LICENSE"
  section "WebSocket++" "$build/_deps/websocketpp-src/COPYING"
  section "Asio" "$build/_deps/asio-src/asio/LICENSE_1_0.txt" "$build/_deps/asio-src/asio/COPYING"
  section "JSON for Modern C++ (nlohmann/json)" "$build/_deps/nlohmann_json-src/LICENSE.MIT"
  section "OpenSSL (Apache License 2.0)" /usr/share/licenses/openssl-libs/LICENSE.txt /usr/share/doc/libssl3/copyright \
    /usr/share/licenses/openssl/LICENSE.txt
  zlib_section
fi
