"""Universal Tracker's map tab (docs/ap/apworld.md "Universal Tracker's map tab").

Hybrid, in UT's words: the map tab's JSON is in this APWorld (tracker/: maps, locations named by their
AP location names, the map menu, and map_pages.json), generated with the PopTracker pack by
tools/ap_poptracker.py (`--ut-out apworld/so2/tracker`); the images come from the pack's zip, which
the player points UT at once (host.yaml `so2_options: ut_pack_path`; UT asks with a file dialog the
first time it is empty). So the APWorld carries no pictures, and the pack's maps (placeholders, or a
player's own render copied into the zip) are UT's too.

UT follows the port's map: `map_page_setting_key` is the port's so2_map_{team}_{player}
(docs/ap/data_storage.md), whose map goes through map_page_index to the room's page (or the world
map's); on a world map the same key's x / z place the player marker (location_icon_coords).
Nothing here runs during generation.
"""
import functools
import json
import pkgutil

import settings

TRACKER_FOLDER = "tracker"      # in this package: maps/, locations/, layouts/, map_pages.json


class SO2Settings(settings.Group):
    class UTPackPath(settings.FilePath):
        """The Shattered Stars PopTracker pack (so2_shattered_stars_<version>.zip), for Universal
        Tracker's map tab: UT shows its maps' pictures from it. Empty: UT asks for it once; null: no
        map tab."""
        description = "Shattered Stars PopTracker pack (so2_shattered_stars_*.zip)"
        required = False
        ut_dialog_name = "Select the Shattered Stars PopTracker pack (so2_shattered_stars_*.zip)"

    ut_pack_path: UTPackPath | str = UTPackPath()


@functools.cache
def pages():
    """tracker/map_pages.json: {"pages": {field map: page}, "world": {world map: {page, x0, z0,
    units_per_pixel, width, height}}, "player_icon": the marker's path in the pack}."""
    data = pkgutil.get_data(__name__, f"{TRACKER_FOLDER}/map_pages.json")
    return json.loads(data.decode("utf-8"))


def map_page_index(data):
    """so2_map_{team}_{player} -> the page UT opens: the room's, a world map's or an area's
    overview; -1 (keep the page) for no value or a map with no page."""
    if not isinstance(data, dict) or isinstance(data.get("map"), bool) or not isinstance(data.get("map"), int):
        return -1
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
