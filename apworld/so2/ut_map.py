"""Universal Tracker's map tab (docs/apworld/apworld.md "Universal Tracker's map tab").

Hybrid, in UT's words: the map tab's JSON is in this APWorld (tracker/: maps, locations named by their
AP location names, the map menu, and map_pages.json), generated with the PopTracker pack by
trackers/tools/ap_poptracker.py (`--ut-out apworld/so2/tracker`); the images come from a zip the player points
UT at once (host.yaml `so2_options: ut_pack_path`; UT asks with a file dialog the first time it is
empty): either the port's so2_shattered_stars_ut.zip (the player's own maps, rendered from their disc
by `--render-tracker-maps` or setup's button, docs/trackers/tracker_maps.md "The Universal Tracker zip") or
the PopTracker pack's so2_shattered_stars_<version>.zip (its placeholders). Both hold every picture the
map tab shows at the same paths, so the APWorld carries no pictures.

UT follows the port's map: `map_page_setting_key` is the port's so2_map_{team}_{player}
(docs/apworld/data_storage.md), whose map goes through map_page_index to the room's page (or the world
map's); on a world map the same key's x / z place the player marker (location_icon_coords).
With no value UT opens Expel's world map (map_page_index): at connect UT lays out its first page
before it has read the key, and UT 0.3.6 needs one laid out by then (see map_page_index).
Nothing here runs during generation.
"""
import functools
import json
import pkgutil

import settings

TRACKER_FOLDER = "tracker"      # in this package: maps/, locations/, layouts/, map_pages.json


class SO2Settings(settings.Group):
    class UTPackPath(settings.FilePath):
        """The pictures for Universal Tracker's map tab, one of two zips: so2_shattered_stars_ut.zip,
        your own maps (Shattered Stars writes it beside the maps it renders for PopTracker:
        --render-tracker-maps or setup's "Render tracker maps for PopTracker"), or the PopTracker pack
        so2_shattered_stars_<version>.zip (placeholder maps). Empty: UT asks for it once; null: no
        map tab."""
        description = "Shattered Stars maps for Universal Tracker (so2_shattered_stars_ut.zip or so2_shattered_stars_*.zip)"
        required = False
        ut_dialog_name = ("Select the Shattered Stars maps: so2_shattered_stars_ut.zip (your own, rendered by "
                          "Shattered Stars) or the PopTracker pack so2_shattered_stars_*.zip")

    ut_pack_path: UTPackPath | str = UTPackPath()


@functools.cache
def pages():
    """tracker/map_pages.json: {"pages": {field map: page}, "world": {world map: {page, x0, z0,
    units_per_pixel, width, height}}, "player_icon": the marker's path in the pack}."""
    data = pkgutil.get_data(__name__, f"{TRACKER_FOLDER}/map_pages.json")
    return json.loads(data.decode("utf-8"))


def default_page():
    """The page for no value: Expel's world map (where the marker shows if the game is there)."""
    return pages()["world"]["1"]["page"]


def map_page_index(data):
    """so2_map_{team}_{player} -> the page UT opens: the room's, a world map's or an area's
    overview; Expel's world map for no value (no map number: UT hasn't read the key yet, or the
    port has never written it); -1 (keep the page) for a map with no page.

    No value must open a page, never -1. UT 0.3.6 (worlds/tracker/TrackerClient.py) lays a page out
    only in load_map, and a connection's first load_map (load_pack's, from on_package's Connected)
    runs before UT has asked for the key: enable_maps Gets it afterwards. The Retrieved that answers
    moves the marker (update_location_icon_coords) without loading a page, and the map tab's
    VisualTracker has no location_icons until a page is laid out (load_coords): with -1 here UT
    dropped the connection at every first connect with an AttributeError
    (trackers/tools/ap_ut_connect.py replays it). The page follows the game from its next write."""
    if not isinstance(data, dict) or isinstance(data.get("map"), bool) or not isinstance(data.get("map"), int):
        return default_page()
    return pages()["pages"].get(str(data["map"]), -1)


def location_icon_coords(index, data):
    """The player marker on a world map page: the port's x / z there (tracker_maps.md "World
    images": pixel = (x - x0) / units, (z0 - z) / units); None elsewhere (a field map has no
    position in the key)."""
    if not isinstance(data, dict):
        return None
    world = pages()["world"].get(str(data.get("map")))
    x, z = data.get("x"), data.get("z")
    if world is None or index != world["page"] or not isinstance(x, int) or not isinstance(z, int):
        return None
    px = (x - world["x0"]) // world["units_per_pixel"]
    py = (world["z0"] - z) // world["units_per_pixel"]
    if not (0 <= px < world["width"] and 0 <= py < world["height"]):
        return None
    return px, py, pages()["player_icon"]


tracker_world = {
    "map_page_folder": TRACKER_FOLDER,
    "external_pack_key": "ut_pack_path",
    "map_page_maps": "maps/maps.json",
    "map_page_locations": "locations/locations.json",
    "map_page_layouts": "layouts/maps.json",
    "map_page_setting_key": "so2_map_{team}_{player}",
    "map_page_index": map_page_index,
    "location_setting_key": "so2_map_{team}_{player}",
    "location_icon_coords": location_icon_coords,
}
