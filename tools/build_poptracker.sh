#!/bin/bash
# The PopTracker pack (docs/ap/poptracker.md "Building"): generate it, zip it, hash it, write its
# versions.json entry, and check it.
#
#   tools/build_poptracker.sh build [--tag TAG] [--changelog LINE]...
#       dist/poptracker/so2_shattered_stars/            the pack folder (tools/ap_poptracker.py)
#       dist/poptracker/so2_shattered_stars_X.Y.Z.zip   the release asset (reproducible bytes)
#       dist/poptracker/so2_shattered_stars_X.Y.Z.zip.sha256
#       dist/poptracker/versions.json                   tools/poptracker/versions.json (when there
#                                                       is one) with this version's entry first
#       dist/poptracker/ut_name_mapping.json            for Universal Tracker's map tab (B6)
#       dist/poptracker/boards.json                     the boards' cells, which the port draws into its
#                                                       Universal Tracker zip (the pack's tests check that
#                                                       tools/poptracker/boards.json, compiled into the
#                                                       port, is this copy: tools/ap_poptracker.py
#                                                       --boards-out tools/poptracker/boards.json)
#   tools/build_poptracker.sh check
#       PopTracker's pack checker (PopTracker/pack-checker v1.7.0, the one pack-checker-action runs,
#       strict schema) on the zip and the folder, versions.json against the zip, then
#       tools/ap_poptracker_test.py on the folder.
#   tools/build_poptracker.sh all [build options]      build, then check
#
# TAG (default poptracker-vX.Y.Z, the pack's own release; the user, 2026-10-27) is the GitHub release
# the zip is attached to: the download URL is
# https://github.com/IronJade/Shattered-Stars/releases/download/TAG/so2_shattered_stars_X.Y.Z.zip.
# Nothing here publishes anything: committing versions.json and attaching the zip are release steps
# (docs/ap/poptracker.md "Distribution").
#
# Needs the scratch Archipelago of tools/ap_test_server.sh (scratchpad/ap: Archipelago 0.6.8 with
# worlds/so2 linked to apworld/so2, its venv with Pillow and lupa). `check` clones the pack checker
# into scratchpad/packgen/ and makes a venv for it there the first time.
set -eu
cd "$(dirname "$0")/.."
repo=$PWD
ap=$repo/scratchpad/ap
py=$ap/ap-venv/bin/python
out=$repo/dist/poptracker
uid=so2_shattered_stars
version=$(sed -n 's/^PACK_VERSION = "\(.*\)"$/\1/p' tools/ap_poptracker.py)
zip=$out/${uid}_${version}.zip
scratch=$repo/scratchpad/packgen
export SKIP_REQUIREMENTS_UPDATE=1

build() {
  local tag=poptracker-v$version
  local changelog=()
  while [ $# -gt 0 ]; do
    case "$1" in
      --tag) tag=$2; shift 2 ;;
      --changelog) changelog+=("$2"); shift 2 ;;
      *) echo "build: unknown option $1" >&2; exit 2 ;;
    esac
  done
  [ ${#changelog[@]} -gt 0 ] || changelog=("Pack $version for so2.apworld (Star Ocean: The Second Story on Shattered Stars).")
  mkdir -p "$out"
  rm -f "$zip" "$zip.sha256"
  prlimit --as=$((8 << 30)) "$py" tools/ap_poptracker.py --out "$out/$uid" --zip "$zip"
  (cd "$out" && sha256sum "$(basename "$zip")" > "$(basename "$zip").sha256")
  local sha
  sha=$(cut -d' ' -f1 "$zip.sha256")
  "$py" - "$version" "https://github.com/IronJade/Shattered-Stars/releases/download/$tag/$(basename "$zip")" \
      "$sha" "$repo/tools/poptracker/versions.json" "$out/versions.json" "${changelog[@]}" <<'EOF'
import json, os, sys
version, url, sha, committed, dest, *changelog = sys.argv[1:]
data = {"versions": []}
if os.path.exists(committed):
    with open(committed, encoding="utf-8") as f:
        data = json.load(f)
entry = {"package_version": version, "download_url": url, "sha256": sha, "changelog": changelog}
data["versions"] = [entry] + [v for v in data["versions"] if v["package_version"] != version]
with open(dest, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=4, ensure_ascii=False)
    f.write("\n")
print(f"versions.json: {version} {sha} {url}")
EOF
}

checker_setup() {
  if [ ! -f "$scratch/pack-checker/pack_checker.py" ]; then
    mkdir -p "$scratch"
    git clone -q --branch v1.7.0 --depth 1 --recurse-submodules https://github.com/PopTracker/pack-checker.git \
      "$scratch/pack-checker"
  fi
  if [ ! -x "$scratch/venv/bin/python" ]; then
    python3 -m venv "$scratch/venv"
    "$scratch/venv/bin/pip" install -q -r "$scratch/pack-checker/requirements.txt"
  fi
}

check() {
  [ -f "$zip" ] || { echo "no $zip: run build first" >&2; exit 1; }
  checker_setup
  echo "== pack checker (strict): zip"
  "$scratch/venv/bin/python" "$scratch/pack-checker/pack_checker.py" -b --strict "$zip"
  echo "== pack checker (strict): folder"
  "$scratch/venv/bin/python" "$scratch/pack-checker/pack_checker.py" -b --strict "$out/$uid"
  echo "== versions.json"
  "$py" - "$out/versions.json" "$zip" "$version" <<'EOF'
import hashlib, json, sys, zipfile
path, zip_path, version = sys.argv[1:]
with open(path, encoding="utf-8") as f:
    data = json.load(f)
first = data["versions"][0]
assert set(first) == {"package_version", "download_url", "sha256", "changelog"}, first
assert first["package_version"] == version, first
assert first["download_url"].startswith("https://") and first["download_url"].endswith(zip_path.rsplit("/", 1)[1])
with open(zip_path, "rb") as f:
    assert first["sha256"] == hashlib.sha256(f.read()).hexdigest(), "sha256 differs from the zip"
assert isinstance(first["changelog"], list) and all(isinstance(c, str) for c in first["changelog"])
with zipfile.ZipFile(zip_path) as z:
    manifest = json.loads(z.read("manifest.json"))
assert manifest["package_version"] == version and manifest["package_uid"] == "so2_shattered_stars"
print(f"ok: {version} {first['sha256']}")
EOF
  echo "== tools/ap_poptracker_test.py"
  prlimit --as=$((8 << 30)) "$py" tools/ap_poptracker_test.py --pack "$out/$uid"
}

case "${1:-}" in
  build) shift; build "$@" ;;
  check) check ;;
  all) shift; build "$@"; check ;;
  *) echo "usage: $0 build [--tag TAG] [--changelog LINE]... | check | all [build options]" >&2; exit 2 ;;
esac
