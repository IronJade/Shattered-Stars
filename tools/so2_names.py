#!/usr/bin/env python3
"""Names for the numbers a mod uses: items, maps, shops and enemy types, from the repository's
own tables (no disc needed), and name lookup for tools/so2_mod.py's arguments.

Sources (facts only: IDs and names):
  items    apworld/so2/data.py ITEMS (the game's names; a name two items share carries the
           item number, as "Fanzine (190)") and docs/ap/field.json item_names (as shown)
  maps     docs/modding/reference/map_names.csv (our descriptive names: "area: place") when
           present, else the area banners in docs/ap/field.json (map_names, map_areas),
           else "Map N"
  shops    docs/ap/field.json shops / shop_sites (the maps that open each shop)
  enemies  docs/ap/battles.json enemy_types (the name in each type's stats record)

Lookups are case-insensitive and ignore punctuation and plurals ("blueberries" finds
Blueberry). A number (decimal or 0x hex) is taken as is. A name that fits several entries
raises Ambiguous (with the candidates); one that fits none raises NotFound (with close
spellings as suggestions).
"""

import collections
import csv
import difflib
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP_NAMES_CSV = os.path.join(ROOT, "docs", "modding", "reference", "map_names.csv")
FIELD_JSON = os.path.join(ROOT, "docs", "ap", "field.json")
BATTLES_JSON = os.path.join(ROOT, "docs", "ap", "battles.json")
APWORLD = os.path.join(ROOT, "apworld", "so2")

WORLDS = {1: "Expel", 2: "Energy Nede", 3: "World map 3"}
CHUNKS = {(909, 980): 1, (1048, 1110): 2, (1113, 1114): 3}   # world-map chunk scripts
WORLD_SCRIPT = (0, 1, 2, 3, 826)                                 # one script: the world maps' own


PLURAL = {"item": "items", "map": "maps", "shop": "shops", "enemy": "enemy types"}


def closeness(query, name):
    """How alike a query and a name are (0..1): the whole name, or its best run of as many
    words as the query has ("Arila" against "Arlia Village" scores as against "Arlia")."""
    q, n = query.casefold(), name.casefold()
    best = difflib.SequenceMatcher(None, q, n).ratio()
    words, k = n.split(), max(1, len(q.split()))
    for i in range(0, max(1, len(words) - k + 1)):
        best = max(best, difflib.SequenceMatcher(None, q, " ".join(words[i:i + k])).ratio())
    return best


class LookupFailed(Exception):
    def __init__(self, kind, text, message, candidates=()):
        super().__init__(message)
        self.kind, self.text, self.candidates = kind, text, list(candidates)


class Ambiguous(LookupFailed):
    pass


class NotFound(LookupFailed):
    pass


def loose(text):
    """Lower case, letters and digits only."""
    text = text.replace("’", "'").replace("‘", "'").casefold()
    return re.sub(r"[^0-9a-zͰ-Ͽ]+", "", text)


def singulars(word):
    """Loose keys a plural query may stand for: berries -> berry, boxes -> box, cards -> card."""
    out = []
    if word.endswith("ies") and len(word) > 4:
        out.append(word[:-3] + "y")
    if word.endswith("es") and len(word) > 3:
        out.append(word[:-2])
    if word.endswith("s") and len(word) > 2:
        out.append(word[:-1])
    return out


def number(text):
    """int(text, 0) for a decimal / 0x number (also a negative one), else None."""
    try:
        return int(str(text).strip(), 0)
    except ValueError:
        return None


def _apworld_items():
    try:
        sys.path.insert(0, APWORLD)
        import data as apdata  # noqa: E402  (apworld/so2/data.py: plain tables)
        return dict(apdata.ITEMS)
    except Exception:  # noqa: BLE001 (no APWorld tree: fall back to field.json)
        return {}
    finally:
        if sys.path and sys.path[0] == APWORLD:
            sys.path.pop(0)


def read_map_csv(path=MAP_NAMES_CSV):
    """{map: {area, place, kind, disc, notes}} from map_names.csv, or {} if it isn't there."""
    if not os.path.exists(path):
        return {}
    out = {}
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            m = number(row.get("map", ""))
            if m is None:
                continue
            out[m] = {k: (row.get(k) or "").strip() for k in ("disc", "area", "place", "kind", "notes")}
    return out


class Tables:
    """The name tables. Everything is read from the repository on first use."""

    def __init__(self, field_json=FIELD_JSON, battles_json=BATTLES_JSON, map_csv=MAP_NAMES_CSV):
        field = json.load(open(field_json)) if os.path.exists(field_json) else {}
        battles = json.load(open(battles_json)) if os.path.exists(battles_json) else {}
        self.field = field
        self.battles = battles
        shown = {int(k): v for k, v in field.get("item_names", {}).items()}
        unique = _apworld_items()
        self.item_names = shown or dict(unique)            # as the game shows them
        self.item_unique = unique or dict(shown)           # one name per item
        self.map_csv = read_map_csv(map_csv)
        self.banners = {int(k): v for k, v in field.get("map_names", {}).items()}
        self.areas = {int(k): v for k, v in field.get("map_areas", {}).items()}
        self.map_worlds = {int(k): v for k, v in field.get("map_worlds", {}).items()}
        self.late = set(field.get("late_maps", []))
        self.field_maps = list(field.get("field_maps", []))
        self.shop_maps = {}
        for s in field.get("shop_sites", []):
            self.shop_maps.setdefault(s["shop"], set()).add(s["map"])
        self.shop_ids = sorted({int(k) for k in field.get("shops", {})} | set(self.shop_maps))
        self.enemy_names = {int(k): v["name"] for k, v in battles.get("enemy_types", {}).items()}
        self._maps = {}

    # ---- maps ----

    def map_info(self, m):
        """{name, area, place, kind, discs, source} for map m."""
        if m in self._maps:
            return self._maps[m]
        row = self.map_csv.get(m)
        if row and (row["area"] or row["place"]):
            area, place = row["area"], row["place"]
            name = f"{area}: {place}" if area and place else area or place
            info = {"name": name, "area": area, "place": place, "kind": row["kind"],
                    "discs": "", "source": "map_names.csv"}
        else:
            info = self._fallback(m)
        info["discs"] = self.discs(m)
        self._maps[m] = info
        return info

    def _fallback(self, m):
        kind, discs = "", self._discs(m)
        for (lo, hi), w in CHUNKS.items():
            if lo <= m <= hi:
                return {"name": f"{WORLDS[w]} world map (chunk {m - lo + 1})", "area": WORLDS[w],
                        "place": f"world map chunk {m - lo + 1}", "kind": "world map", "discs": discs,
                        "source": "world map"}
        if m in WORLD_SCRIPT:
            w = WORLDS.get(m)
            name = f"{w} world map" if w else f"World script (map {m})"
            return {"name": name, "area": w or "", "place": "world map" if w else "world script",
                    "kind": "world map", "discs": discs, "source": "world map"}
        banners = self.banners.get(m, [])
        area = self.areas.get(m, "")
        place = " / ".join(banners)
        if banners and area and place != area:
            name = f"{area}: {place}"
        elif banners or area:
            name, place = place or area, place if place != area else ""
        else:
            name = f"Map {m}"
        return {"name": name, "area": area, "place": place, "kind": kind, "discs": discs,
                "source": "banner" if banners else "area" if area else "none"}

    def _discs(self, m):
        """The discs the story visits map m on, from its world (field.json map_worlds: Expel is
        disc 1, and disc 2 again for the maps open in Virtual Expel; Energy Nede is disc 2)."""
        w = self.map_worlds.get(m)
        if w == 1:
            return "1, 2" if m in self.late else "1"
        if w == 2:
            return "2"
        return ""

    def discs(self, m):
        """_discs, or for a map without a world the discs most maps of its area have."""
        d = self._discs(m)
        if d:
            return d
        if getattr(self, "_area_discs", None) is None:
            votes = {}
            for x in self.map_ids():
                a, dx = self._area_of(x), self._discs(x)
                if a and dx:
                    votes.setdefault(a, collections.Counter())[dx] += 1
            self._area_discs = {a: c.most_common(1)[0][0] for a, c in votes.items()}
        return self._area_discs.get(self._area_of(m), "")

    def _area_of(self, m):
        row = self.map_csv.get(m)
        return (row or {}).get("area") or self.areas.get(m, "")

    def map_name(self, m):
        return self.map_info(m)["name"]

    def map_ids(self):
        ids = set(self.field_maps) | set(self.map_csv) | set(self.banners) | set(self.areas)
        return sorted(ids)

    # ---- the others ----

    def item_name(self, item):
        return self.item_names.get(item, f"item {item:#x}")

    def shop_name(self, shop):
        maps = sorted(self.shop_maps.get(shop, ()))
        if not maps:
            return f"Shop {shop}"
        return f"Shop {shop} ({'; '.join(f'map {m}: {self.map_name(m)}' for m in maps)})"

    def enemy_name(self, t):
        return self.enemy_names.get(t, f"enemy type {t}")

    # ---- lookup ----

    def aliases(self, kind):
        """[(alias, id)] every name an entry answers to."""
        out = []
        if kind == "item":
            for i, n in self.item_names.items():
                out.append((n, i))
            for i, n in self.item_unique.items():
                if n != self.item_names.get(i):
                    out.append((n, i))
        elif kind == "map":
            for m in self.map_ids():
                info = self.map_info(m)
                out.append((info["name"], m))
                if info["place"] and info["place"] != info["name"]:
                    out.append((info["place"], m))
                for b in self.banners.get(m, []):
                    if b != info["name"]:
                        out.append((b, m))
        elif kind == "shop":
            for s in self.shop_ids:
                out.append((f"Shop {s}", s))
                for m in sorted(self.shop_maps.get(s, ())):
                    info = self.map_info(m)
                    out.append((info["name"], s))
                    for b in self.banners.get(m, []):
                        out.append((b, s))
        elif kind == "enemy":
            out = [(n, t) for t, n in self.enemy_names.items()]
        return out

    def label(self, kind, ident):
        if kind == "item":
            return f"{ident:#x} {self.item_name(ident)}"
        if kind == "map":
            return f"{ident} {self.map_name(ident)}"
        if kind == "shop":
            return self.shop_name(ident)
        return f"{ident} {self.enemy_name(ident)}"

    def resolve(self, kind, text):
        """The id 'text' names: a number as is, else the one entry whose name fits."""
        n = number(text)
        if n is not None:
            return n
        query = str(text).strip()
        aliases = self.aliases(kind)
        if not loose(query):
            raise NotFound(kind, text, f"no {kind} named {query!r}")

        def unique(ids, how):
            ids = sorted(set(ids))
            if len(ids) == 1:
                return ids[0]
            if len(ids) > 1:
                raise Ambiguous(kind, text, f"{query!r} fits {len(ids)} {PLURAL[kind]} ({how}); use one of these "
                                f"(the number works too):", [self.label(kind, i) for i in ids])
            return None

        folded = query.casefold()
        for test, how in ((lambda a: a.casefold() == folded, "same name"),
                          (lambda a: loose(a) == loose(query), "same name"),
                          (lambda a: loose(a) in singulars(loose(query)), "same name")):
            got = unique([i for a, i in aliases if test(a)], how)
            if got is not None:
                return got
        # A part of a name: "Hearn" or "Arlia: General Store" (area part: place part).
        if ":" in query:
            left, right = (loose(x) for x in query.split(":", 1))
            if kind == "map":
                hits = [m for m in self.map_ids() if left in loose(self.map_info(m)["area"] or self.map_info(m)["name"])
                        and right in loose(self.map_info(m)["place"] or self.map_info(m)["name"])]
            else:
                hits = [i for a, i in aliases if left in loose(a) and right in loose(a)]
            got = unique(hits, "part of the name")
            if got is not None:
                return got
        q = loose(query)
        keys = [q] + singulars(q)
        got = unique([i for a, i in aliases if any(k in loose(a) for k in keys)], "part of the name")
        if got is not None:
            return got
        scored = sorted(((closeness(query, a), a) for a in {a for a, _ in aliases}), reverse=True)
        close = [a for score, a in scored[:8] if score >= 0.6]
        sugg = []
        for a in close:
            ids = sorted({i for b, i in aliases if b == a})
            sugg += [self.label(kind, i) for i in ids if self.label(kind, i) not in sugg]
        sugg = sugg[:8]
        msg = f"no {kind} named {query!r}" + ("; did you mean:" if sugg else f" (try `so2_mod.py find DISC {query}`)")
        raise NotFound(kind, text, msg, sugg)

    def find(self, text, kinds=("map", "item", "shop", "enemy")):
        """[(kind, id, label)] whose names contain 'text' (loose), then close spellings."""
        q = loose(text)
        keys = [q] + singulars(q)
        n = number(text)
        out = []
        for kind in kinds:
            hits = set()
            for a, i in self.aliases(kind):
                if q and any(k in loose(a) for k in keys):
                    hits.add(i)
            if n is not None:
                if kind == "item" and n in self.item_names:
                    hits.add(n)
                elif kind == "map" and n in set(self.map_ids()):
                    hits.add(n)
                elif kind == "shop" and n in self.shop_ids:
                    hits.add(n)
                elif kind == "enemy" and n in self.enemy_names:
                    hits.add(n)
            out += [(kind, i, self.label(kind, i)) for i in sorted(hits)]
        if not out and q:
            for kind in kinds:
                names = {}
                for a, i in self.aliases(kind):
                    names.setdefault(a, i)
                scored = sorted(((closeness(text, a), a) for a in names), reverse=True)
                for score, a in scored[:8]:
                    if score >= 0.6 and all(o[:2] != (kind, names[a]) for o in out):
                        out.append((kind, names[a], self.label(kind, names[a])))
        return out


_TABLES = None


def tables():
    global _TABLES
    if _TABLES is None:
        _TABLES = Tables()
    return _TABLES


def describe_value(v, t=None):
    """A chest's contents: -N is N Fol, else an item (with its name)."""
    if v < 0:
        return f"{-v} Fol"
    t = t or tables()
    return f"item {v:#x} {t.item_name(v)}"
