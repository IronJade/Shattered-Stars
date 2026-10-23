#!/bin/bash
# Packages apworld/so2 as a redistributable .apworld and generates its options template YAML,
# for Archipelago phase 5 (docs/ap/release_checklist.md). Builds only; never publishes or
# contacts a network server.
#
#   tools/build_apworld.sh [OUT_DIR]
#
# Writes OUT_DIR/so2.apworld (default dist/) and OUT_DIR/"Star Ocean The Second Story.yaml",
# the per-game options template.
#
# Both need a working Archipelago install + venv with this world already loadable (by
# default the one most of this repo's AP tooling shares, scratchpad/ap, docs/ap/apworld.md
# "Testing": its worlds/so2 is a symlink to apworld/so2, so it always reflects the working
# tree), overridable:
#
#   AP_INSTALL=path/to/Archipelago AP_VENV=path/to/venv tools/build_apworld.sh
#
# The .apworld is built with Archipelago's own "Build APWorlds" Launcher component (docs/
# "apworld specification.md"), not a hand-rolled zip: that component is also what adds the
# zip-only `version` / `compatible_version` manifest fields (apworld/so2/archipelago.json
# deliberately omits them, per the spec, "Do not write these fields yourself"). A hand-rolled
# zip of the folder was tried first and loads with a logged manifest error ("Invalid or
# missing manifest file ... This apworld will stop working with Archipelago 0.7.0") because
# of exactly that missing pair; the component is the only supported way to avoid it. It also
# applies the global .apignore (drops __pycache__, the loose archipelago.json replaced by the
# merged one, VCS files; does not drop test/, which this build keeps in) and lower-cases
# nothing further (the game's module name is already "so2").
set -eu
cd "$(dirname "$0")/.."
ROOT=$PWD
OUT=${1:-$ROOT/dist}
AP_INSTALL=${AP_INSTALL:-$ROOT/scratchpad/ap/Archipelago}
AP_VENV=${AP_VENV:-$ROOT/scratchpad/ap/ap-venv}
PKG=$ROOT/apworld/so2
GAME_NAME="Star Ocean: The Second Story"
APWORLD_NAME=so2
mkdir -p "$OUT"

if [ ! -d "$PKG" ] || [ ! -f "$PKG/archipelago.json" ]; then
  echo "error: $PKG (or its archipelago.json) is missing" >&2
  exit 1
fi
if [ ! -x "$AP_VENV/bin/python" ] || [ ! -f "$AP_INSTALL/Launcher.py" ]; then
  echo "error: no Archipelago install/venv at AP_INSTALL=$AP_INSTALL AP_VENV=$AP_VENV" >&2
  echo "  (run tools/ap_test_server.sh start once to set one up)" >&2
  exit 1
fi

added_symlink=""
if [ ! -e "$AP_INSTALL/worlds/$APWORLD_NAME" ] && [ ! -e "$AP_INSTALL/custom_worlds/$APWORLD_NAME" ]; then
  ln -sfn "$PKG" "$AP_INSTALL/worlds/$APWORLD_NAME"
  added_symlink="$AP_INSTALL/worlds/$APWORLD_NAME"
fi
cleanup() { if [ -n "$added_symlink" ]; then rm -f "$added_symlink"; fi; }
trap cleanup EXIT

export SKIP_REQUIREMENTS_UPDATE=1

# --- 1. The .apworld, via Archipelago's "Build APWorlds" Launcher component --------------
(cd "$AP_INSTALL" && rm -rf build/apworlds && "$AP_VENV/bin/python" Launcher.py "Build APWorlds" -- \
   "$GAME_NAME" --skip_open_folder)
built="$AP_INSTALL/build/apworlds/$APWORLD_NAME.apworld"
if [ ! -f "$built" ]; then
  echo "error: Build APWorlds did not write $built (is '$GAME_NAME' loaded in $AP_INSTALL?)" >&2
  exit 1
fi
cp "$built" "$OUT/$APWORLD_NAME.apworld"
echo "wrote $OUT/$APWORLD_NAME.apworld ($(du -h "$OUT/$APWORLD_NAME.apworld" | cut -f1))"
if unzip -l "$OUT/$APWORLD_NAME.apworld" | grep -qi __pycache__; then
  echo "error: __pycache__ leaked into the zip" >&2
  exit 1
fi
if ! unzip -l "$OUT/$APWORLD_NAME.apworld" | grep -q " $APWORLD_NAME/test/test_world.py"; then
  echo "error: test/ did not make it into the zip" >&2
  exit 1
fi

# --- 2. The options template YAML, via Archipelago's own generator -----------------------
# generate_yaml_templates writes one YAML per world installed (every world in that
# Archipelago checkout, ~100 files); write them to a scratch folder and copy out only ours.
templates=$ROOT/scratchpad/release/build_apworld/templates
rm -rf "$templates"
mkdir -p "$templates"
(cd "$AP_INSTALL" && "$AP_VENV/bin/python" -c "
import Options
Options.generate_yaml_templates('$templates', generate_hidden=True)
")
template=$(find "$templates" -maxdepth 1 -iname '*Second Story*.yaml' | head -1)
if [ -z "$template" ]; then
  echo "error: the template generator did not write a Star Ocean: The Second Story YAML" >&2
  exit 1
fi
cp "$template" "$OUT/"
echo "wrote $OUT/$(basename "$template")"
