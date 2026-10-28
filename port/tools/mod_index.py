#!/usr/bin/env python3
"""The curated mod list's index: mods/index.json (docs/port/notes/mods.md section 6).

  mod_index.py build                     regenerate mods/index.json from mods/*/mod.json
  mod_index.py check                     fail if mods/index.json isn't what build makes (CI)
  mod_index.py verify ID --by NAME [--port-version V] [--date YYYY-MM-DD]
                                         record the maintainer's verification of mod ID
  mod_index.py guard --base-ref REF --author LOGIN [--maintainer IronJade]
                                         fail if the index gained a verification since REF and
                                         LOGIN isn't the maintainer (CI, on pull requests)

Each entry records the mod's manifest fields, the sha256 of every file in its folder and
its verification: verified (true / false) and, when true, verified_by,
verified_port_version and verified_date. A rebuild keeps an entry's verification only while
every file's sha256 is unchanged, so any change to a verified mod makes it unverified until
the maintainer runs `verify` again (after port/tools/mod_verify.py passes).
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # <section>/tools/ -> the repo
REGISTRY = os.path.join(ROOT, "mods")
INDEX = os.path.join(REGISTRY, "index.json")
MAINTAINER = "IronJade"
MANIFEST_FIELDS = ("id", "name", "version", "author", "description", "tags", "min_port_version", "homepage")
VERIFICATION = ("verified_by", "verified_port_version", "verified_date")


def port_version():
    with open(os.path.join(ROOT, "port", "CMakeLists.txt"), encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"project\(ShatteredStars[^)]*VERSION\s+([0-9.]+)\)", text)
    return m.group(1) if m else "0"


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def build(registry=REGISTRY, previous=None):
    """The index for the folders under 'registry', keeping verifications from 'previous'
    whose files are unchanged."""
    before = {e["id"]: e for e in (previous or {}).get("mods", []) if "id" in e}
    mods = []
    for name in sorted(os.listdir(registry)):
        folder = os.path.join(registry, name)
        manifest_path = os.path.join(folder, "mod.json")
        if not os.path.isdir(folder) or not os.path.isfile(manifest_path):
            continue
        try:
            with open(manifest_path, encoding="utf-8-sig") as f:
                m = json.load(f)
        except (OSError, json.JSONDecodeError):
            m = {}
        entry = {"id": m.get("id", name) if isinstance(m, dict) else name}
        for key in MANIFEST_FIELDS[1:]:
            if isinstance(m, dict) and key in m:
                entry[key] = m[key]
        entry["path"] = f"mods/{name}"
        entry["files"] = {f: sha256(os.path.join(folder, f)) for f in sorted(os.listdir(folder))
                          if os.path.isfile(os.path.join(folder, f))}
        old = before.get(entry["id"])
        if old and old.get("verified") and old.get("files") == entry["files"]:
            entry["verified"] = True
            for key in VERIFICATION:
                if key in old:
                    entry[key] = old[key]
        else:
            entry["verified"] = False
        mods.append(entry)
    return {"format": 1, "generated_by": "port/tools/mod_index.py",
            "about": "The curated mod list: only entries with \"verified\": true are on it (mods/README.md).",
            "mods": mods}


def mark_verified(index, mod_id, by, version, date):
    out = json.loads(json.dumps(index))
    for e in out["mods"]:
        if e["id"] == mod_id:
            e["verified"] = True
            e["verified_by"] = by
            e["verified_port_version"] = version
            e["verified_date"] = date
            return out
    raise SystemExit(f"no mod {mod_id} in the index (run build first?)")


def verification_changes(base, head):
    """Ids whose entry in 'head' is verified with fields that 'base' doesn't have: the
    verifications this change adds (dropping one, as a changed file does, is fine)."""
    before = {e.get("id"): e for e in (base or {}).get("mods", [])}
    changed = []
    for e in (head or {}).get("mods", []):
        if not e.get("verified"):
            continue
        old = before.get(e.get("id"), {})
        fields = lambda x: (bool(x.get("verified")), x.get("files")) + tuple(x.get(k) for k in VERIFICATION)  # noqa: E731
        if fields(old) != fields(e):
            changed.append(e.get("id"))
    return changed


def load(path=INDEX):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return None


def save(index, path=INDEX):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(index, f, indent=2, ensure_ascii=False)
        f.write("\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("build")
    sub.add_parser("check")
    v = sub.add_parser("verify")
    v.add_argument("id")
    v.add_argument("--by", required=True)
    v.add_argument("--port-version", default=None)
    v.add_argument("--date", default=None)
    g = sub.add_parser("guard")
    g.add_argument("--base-ref", required=True)
    g.add_argument("--author", required=True)
    g.add_argument("--maintainer", default=MAINTAINER)
    a = ap.parse_args(argv)

    if a.cmd == "build":
        index = build(previous=load())
        save(index)
        print(f"mods/index.json: {len(index['mods'])} mod(s), "
              f"{sum(1 for e in index['mods'] if e['verified'])} verified")
        return 0
    if a.cmd == "check":
        committed = load()
        fresh = build(previous=committed)
        if committed != fresh:
            stale = sorted({e["id"] for e in fresh["mods"]} ^ {e["id"] for e in (committed or {}).get("mods", [])} |
                           {e["id"] for e in fresh["mods"] if e not in (committed or {}).get("mods", [])})
            print("mods/index.json doesn't match mods/: run `python3 port/tools/mod_index.py build` and commit it "
                  f"(differs for: {', '.join(stale) or 'the header'})")
            return 1
        print(f"mods/index.json matches mods/ ({len(fresh['mods'])} mod(s))")
        return 0
    if a.cmd == "verify":
        index = build(previous=load())
        date = a.date or datetime.datetime.now(datetime.timezone.utc).date().isoformat()
        save(mark_verified(index, a.id, a.by, a.port_version or port_version(), date))
        print(f"mods/index.json: {a.id} verified by {a.by} on {date}")
        return 0
    if a.cmd == "guard":
        try:
            text = subprocess.run(["git", "show", f"{a.base_ref}:mods/index.json"], cwd=ROOT, capture_output=True,
                                  text=True, check=True).stdout
            base = json.loads(text)
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            base = None
        changed = verification_changes(base, load())
        if changed and a.author.lower() != a.maintainer.lower():
            print(f"this change marks {', '.join(changed)} verified; only the maintainer ({a.maintainer}) does that, "
                  "after port/tools/mod_verify.py passes with the discs")
            return 1
        print("verification fields: " + (f"changed by the maintainer for {', '.join(changed)}" if changed else "unchanged"))
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
