#!/usr/bin/env python3
"""Merge proposed catalog rows into docs/port/catalog/functions.csv.

merge_catalog.py PROPOSAL.csv [PROPOSAL.csv ...] [--matches title_field_matches.csv]

Each proposal has the catalog's columns (module,address,name,signature,summary,
confidence,evidence,port). A proposed row replaces the catalog row with the same
module and address, except that an existing port path is kept and a row already
marked 'confirmed' is only replaced by another 'confirmed' row.

--matches takes the output of tools/match_functions.py (title address, field address,
size, title name): a field function that is the relocated twin of a catalogued title
function gets the title's name and port path, and its evidence notes the match.
"""
import argparse
import csv
import io

CATALOG = "docs/port/catalog/functions.csv"
RANK = {"guess": 0, "likely": 1, "confirmed": 2}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("proposals", nargs="+")
    ap.add_argument("--matches")
    args = ap.parse_args()
    rows = list(csv.reader(open(CATALOG, newline="")))
    head, body = rows[0], rows[1:]
    index = {(r[0], int(r[1], 16)): i for i, r in enumerate(body)}
    added = replaced = kept = 0
    for path in args.proposals:
        for r in csv.DictReader(open(path, newline="")):
            row = [r[k].strip() for k in head]
            key = (row[0], int(row[1], 16))
            row[1] = f"0x{key[1]:08x}"
            if key in index:
                old = body[index[key]]
                if RANK.get(old[5], 0) == 2 and RANK.get(row[5], 0) < 2:
                    kept += 1
                    continue
                if old[7] and not row[7]:
                    row[7] = old[7]
                body[index[key]] = row
                replaced += 1
            else:
                body.append(row)
                index[key] = len(body) - 1
                added += 1
    shared = 0
    if args.matches:
        title = {int(r[1], 16): r for r in body if r[0] == "title"}
        for line in open(args.matches):
            if line.startswith("#"):
                continue
            ta, fa, _, _ = line.strip().split(",", 3)
            t = title.get(int(ta, 16))
            if not t:
                continue
            key = ("field", int(fa, 16))
            note = f"same code as title {t[1]} ({t[2]})"
            if key in index:
                row = body[index[key]]
                if row[2] != t[2]:
                    row[6] = (row[6] + "; " if row[6] else "") + f"agent name {row[2]}; " + note
                    row[3] = t[3]
                    row[2] = t[2]
                elif note not in row[6]:
                    row[6] = (row[6] + "; " if row[6] else "") + note
                if t[7] and not row[7]:
                    row[7] = t[7]
            else:
                body.append(["field", f"0x{key[1]:08x}", t[2], t[3], t[4], t[5], note, t[7]])
                index[key] = len(body) - 1
            shared += 1
    body.sort(key=lambda r: (r[0] != "boot", r[0], int(r[1], 16)))
    out = io.StringIO()
    w = csv.writer(out, lineterminator="\n")
    w.writerow(head)
    w.writerows(body)
    open(CATALOG, "w", newline="").write(out.getvalue())
    print(f"added {added}, replaced {replaced}, kept {kept} confirmed rows, {shared} shared with the title; {len(body)} rows")


if __name__ == "__main__":
    main()
