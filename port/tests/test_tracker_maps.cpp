// The tracker maps [maprender] (tools/tracker_maps.h, util/deflate.h; docs/trackers/tracker_maps.md):
// the deflate stream and the PNGs, the built-in layout, PopTracker's override folder, and
// with the player's disc (SS_DISC1; skipped without it) the backgrounds and the command's
// output: sizes as the layout says, the same bytes twice, nothing else written. [mapdoors]
// Format 1.1's rooms: the door markers, their ids, letters and boxes, the groups, and two
// rendered rooms. [utzip] The Universal Tracker zip: util::ZipWriter's archives (structure,
// CRCs, the same bytes twice, nothing left by a refused or abandoned one), the zip's place, the
// boards and marker the port draws, and with the disc the zip of a render (its entries are the
// rendered files, nothing outside the targets, a stop leaves no zip).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "game/ap/json.h"
#include "mods/zip.h"
#include "test.h"
#include "tools/tracker_maps.h"
#include "util/deflate.h"
#include "util/zip_writer.h"

using namespace ss;
namespace fs = std::filesystem;

namespace {

u32 be32(const u8* p) { return static_cast<u32>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

bool inflate_zlib(const std::vector<u8>& z, std::vector<u8>* out) {
  if (z.size() < 6 || z[0] != 0x78) return false;
  std::string error;
  if (!mods::inflate(z.data() + 2, z.size() - 6, out, 256u << 20, &error)) {
    std::fprintf(stderr, "  inflate: %s\n", error.c_str());
    return false;
  }
  u32 a = 1, b = 0;
  for (u8 c : *out) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  return be32(z.data() + z.size() - 4) == (b << 16 | a);
}

// Decodes an 8-bit RGB or RGBA PNG (the encoder's own output: one IDAT) into RGBA (red low;
// alpha 0xFF for RGB). 'channels' gets 3 or 4.
bool decode_png(const std::vector<u8>& png, int* w, int* h, std::vector<u32>* pixels, int* channels) {
  if (png.size() < 33 || png[1] != 'P' || png[2] != 'N' || png[3] != 'G') return false;
  std::vector<u8> idat;
  int bpp = 0;
  for (size_t at = 8; at + 12 <= png.size();) {
    const u32 len = be32(png.data() + at);
    const std::string type(reinterpret_cast<const char*>(png.data() + at + 4), 4);
    if (type == "IHDR") {
      *w = static_cast<int>(be32(png.data() + at + 8));
      *h = static_cast<int>(be32(png.data() + at + 12));
      if (png[at + 16] != 8 || (png[at + 17] != 2 && png[at + 17] != 6)) return false;
      bpp = png[at + 17] == 6 ? 4 : 3;
    } else if (type == "IDAT") {
      idat.insert(idat.end(), png.begin() + static_cast<long>(at + 8), png.begin() + static_cast<long>(at + 8 + len));
    }
    at += 12 + len;
  }
  if (bpp == 0) return false;
  *channels = bpp;
  std::vector<u8> raw;
  if (!inflate_zlib(idat, &raw)) return false;
  const size_t n = static_cast<size_t>(bpp);
  const size_t stride = static_cast<size_t>(*w) * n;
  if (raw.size() != (stride + 1) * *h) return false;
  std::vector<u8> prior(stride, 0), row(stride);
  pixels->assign(static_cast<size_t>(*w) * *h, 0);
  for (int y = 0; y < *h; ++y) {
    const u8 f = raw[y * (stride + 1)];
    const u8* in = raw.data() + y * (stride + 1) + 1;
    for (size_t i = 0; i < stride; ++i) {
      const int a = i >= n ? row[i - n] : 0, b = prior[i], c = i >= n ? prior[i - n] : 0;
      int pred = 0;
      if (f == 1) pred = a;
      if (f == 2) pred = b;
      if (f == 3) pred = (a + b) / 2;
      if (f == 4) {
        const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
        pred = pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
      }
      row[i] = static_cast<u8>(in[i] + pred);
    }
    for (int x = 0; x < *w; ++x)
      (*pixels)[static_cast<size_t>(y) * *w + x] = row[x * n] | row[x * n + 1] << 8 |
                                                   static_cast<u32>(row[x * n + 2]) << 16 |
                                                   (n == 4 ? static_cast<u32>(row[x * n + 3]) << 24 : 0xFF000000u);
    prior.swap(row);
  }
  return true;
}

// The same, only an RGB PNG.
bool decode_png_rgb(const std::vector<u8>& png, int* w, int* h, std::vector<u32>* pixels) {
  int channels = 0;
  return decode_png(png, w, h, pixels, &channels) && channels == 3;
}

std::vector<u8> read_file(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<u8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

game::ap::Json layout() {
  game::ap::Json j;
  std::string error;
  CHECK(game::ap::Json::parse(std::string(tools::tracker_layout_json()), &j, &error));
  return j;
}

// A temp directory of its own per case, directly under the system temp dir (not nested under a
// shared parent this file creates), so each test's own 'fs::remove_all' at the end removes the
// whole thing and no 'tracker_maps_test/' folder is left in the directory ss_game_tests runs
// from (as b82b176 did for test_tracker_job.cpp).
fs::path scratch(const char* name) {
  const fs::path p = fs::temp_directory_path() / (std::string("ss_tracker_maps_test_") + name);
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p);
  return p;
}

struct OpenDisc {
  std::unique_ptr<disc::DiscImage> disc;
  disc::Archive archive;
  bool ok = false;
};

OpenDisc open_disc1() {
  OpenDisc d;
  const char* path = std::getenv("SS_DISC1");
  if (!path || !*path) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return d;
  }
  std::string error;
  d.disc = disc::DiscImage::open(path, &error);
  d.ok = d.disc && d.archive.open(*d.disc, &error);
  if (!d.ok) std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
  return d;
}

u16 le16(const u8* p) { return static_cast<u16>(p[0] | p[1] << 8); }
u32 le32(const u8* p) { return static_cast<u32>(le16(p)) | static_cast<u32>(le16(p + 2)) << 16; }

// A .zip as util::ZipWriter writes it, read field by field (not through mods::ZipReader):
// stored entries back to back from offset 0, each with the fixed date and no extra field, then
// the central directory naming the same, then a 22-byte end record. Empty on any difference.
std::vector<std::pair<std::string, std::vector<u8>>> parse_written_zip(const std::vector<u8>& z) {
  std::vector<std::pair<std::string, std::vector<u8>>> out;
  std::vector<u32> crcs, offsets;
  size_t at = 0;
  auto bad = [&](const char* why) {
    std::fprintf(stderr, "  zip: %s at %zu\n", why, at);
    out.clear();
    return out;
  };
  while (at + 4 <= z.size() && le32(z.data() + at) == 0x04034b50) {
    if (at + 30 > z.size()) return bad("short local header");
    const u8* h = z.data() + at;
    const u32 crc = le32(h + 14), csize = le32(h + 18), size = le32(h + 22);
    const u16 name_len = le16(h + 26), extra = le16(h + 28);
    if (le16(h + 4) != 20 || (le16(h + 6) & ~0x800u) != 0 || le16(h + 8) != 0) return bad("version / flags / method");
    if (le16(h + 10) != util::ZipWriter::kDosTime || le16(h + 12) != util::ZipWriter::kDosDate) return bad("date");
    if (csize != size || extra != 0 || at + 30 + name_len + size > z.size()) return bad("sizes");
    std::string name(reinterpret_cast<const char*>(h + 30), name_len);
    std::vector<u8> data(h + 30 + name_len, h + 30 + name_len + size);
    if (util::crc32(data.data(), data.size()) != crc || mods::crc32(data.data(), data.size()) != crc) return bad("crc");
    offsets.push_back(static_cast<u32>(at));
    crcs.push_back(crc);
    out.push_back({std::move(name), std::move(data)});
    at += 30 + name_len + size;
  }
  const size_t cd_start = at;
  for (size_t k = 0; k < out.size(); ++k) {
    if (at + 46 > z.size() || le32(z.data() + at) != 0x02014b50) return bad("central entry");
    const u8* c = z.data() + at;
    const u16 name_len = le16(c + 28);
    if (le16(c + 4) != 20 || le16(c + 6) != 20 || le16(c + 10) != 0 || le16(c + 12) != util::ZipWriter::kDosTime ||
        le16(c + 14) != util::ZipWriter::kDosDate || le32(c + 16) != crcs[k] || le32(c + 20) != out[k].second.size() ||
        le32(c + 24) != out[k].second.size() || le16(c + 30) != 0 || le16(c + 32) != 0 || le32(c + 38) != 0 ||
        le32(c + 42) != offsets[k] || std::string(reinterpret_cast<const char*>(c + 46), name_len) != out[k].first)
      return bad("central entry fields");
    at += 46 + name_len;
  }
  if (at + 22 != z.size() || le32(z.data() + at) != 0x06054b50) return bad("end record");
  const u8* e = z.data() + at;
  if (le16(e + 8) != out.size() || le16(e + 10) != out.size() || le32(e + 12) != at - cd_start ||
      le32(e + 16) != cd_start || le16(e + 20) != 0)
    return bad("end record fields");
  return out;
}

// Every file under 'root', relative to it ('/' separated).
std::set<std::string> files_under(const fs::path& root) {
  std::set<std::string> out;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_regular_file()) out.insert(fs::relative(it->path(), root).generic_string());
  return out;
}

}  // namespace

TEST(tracker_deflate_round_trip) {
  std::vector<std::vector<u8>> inputs;
  inputs.push_back({});
  inputs.push_back({42});
  inputs.push_back(std::vector<u8>(1000, 0));
  std::vector<u8> text;
  for (int i = 0; i < 5000; ++i) text.push_back(static_cast<u8>("the quick brown fox "[i % 20]));
  inputs.push_back(text);
  std::vector<u8> noise(300000);
  u32 seed = 12345;
  for (u8& b : noise) {
    seed = seed * 1103515245u + 12345u;
    b = static_cast<u8>(seed >> 16);
  }
  inputs.push_back(noise);
  std::vector<u8> mixed;  // runs, noise and long matches, past several 64k-token blocks
  for (int i = 0; i < 400000; ++i) mixed.push_back(static_cast<u8>((i / 7) % 3 == 0 ? noise[i % 1000] : i % 251));
  inputs.push_back(mixed);
  for (const auto& in : inputs) {
    const std::vector<u8> z = util::zlib_compress(in);
    CHECK(z == util::zlib_compress(in));  // deterministic
    std::vector<u8> out;
    CHECK(inflate_zlib(z, &out));
    CHECK(out == in);
  }
  CHECK(util::zlib_compress(std::vector<u8>(100000, 7)).size() < 1000);
}

TEST(tracker_png_decodes) {
  const int w = 37, h = 23;
  std::vector<u32> px(w * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) px[y * w + x] = (x * 7) | (y * 11) << 8 | static_cast<u32>((x * y) & 0xFF) << 16 | 0xFF000000u;
  const std::vector<u8> png = util::encode_png_rgb(px, w, h);
  CHECK(png == util::encode_png_rgb(px, w, h));
  int dw = 0, dh = 0;
  std::vector<u32> back;
  CHECK(decode_png_rgb(png, &dw, &dh, &back));
  CHECK_EQ(dw, w);
  CHECK_EQ(dh, h);
  CHECK(back == px);
  // RGBA [utzip]: the alpha survives (the player marker's clear background).
  for (size_t i = 0; i < px.size(); ++i) px[i] = (px[i] & 0xFFFFFFu) | static_cast<u32>((i * 37) & 0xFF) << 24;
  const std::vector<u8> rgba = util::encode_png_rgba(px, w, h);
  CHECK(rgba == util::encode_png_rgba(px, w, h));
  int channels = 0;
  CHECK(decode_png(rgba, &dw, &dh, &back, &channels));
  CHECK_EQ(channels, 4);
  CHECK(back == px);
}

TEST(tracker_layout_consistent) {
  const game::ap::Json j = layout();
  CHECK_EQ(j["format"].as_int(), 1);
  CHECK(j["pack_uid"].as_string() == "so2_shattered_stars");
  std::set<std::string> paths;
  struct Rect {
    int x, y, w, h;
  };
  std::vector<std::pair<std::string, std::pair<int, Rect>>> rects;  // image, map, rect
  const auto& images = j["images"];
  CHECK(images.size() > 2);
  for (size_t i = 0; i < images.size(); ++i) {
    const auto& img = images[i];
    const std::string path = img["path"].as_string();
    CHECK(path.rfind("images/maps/", 0) == 0 && path.size() > 16 && path.substr(path.size() - 4) == ".png");
    CHECK(paths.insert(path).second);
    const int W = static_cast<int>(img["width"].as_int()), H = static_cast<int>(img["height"].as_int());
    CHECK(W > 0 && H > 0 && W <= 2048 && H <= 2048);
    std::vector<Rect> mine;
    for (size_t k = 0; k < img["maps"].size(); ++k) {
      const auto& m = img["maps"][k];
      const Rect r{static_cast<int>(m["x"].as_int()), static_cast<int>(m["y"].as_int()), static_cast<int>(m["w"].as_int()),
                   static_cast<int>(m["h"].as_int())};
      CHECK(r.x >= 0 && r.y >= 0 && r.w > 0 && r.h > 0 && r.x + r.w <= W && r.y + r.h <= H);
      CHECK(m["label_y"].as_int() >= 0 && m["label_y"].as_int() + j["style"]["label_height"].as_int() == r.y);
      CHECK(m["bg_w"].as_int() >= r.w && m["bg_h"].as_int() >= r.h);
      for (const Rect& o : mine) CHECK(r.x + r.w <= o.x || o.x + o.w <= r.x || r.y + r.h <= o.y || o.y + o.h <= r.y);
      mine.push_back(r);
      rects.push_back({path, {static_cast<int>(m["map"].as_int()), r}});
    }
    for (size_t k = 0; k < img["dots"].size(); ++k) {
      const auto& d = img["dots"][k];
      CHECK(d["x"].as_int() >= 0 && d["x"].as_int() < W && d["y"].as_int() >= 0 && d["y"].as_int() < H);
    }
  }
  int checks = 0;
  for (const auto& [name, c] : j["checks"].items()) {
    ++checks;
    bool inside = false;
    for (const auto& [path, mr] : rects) {
      const Rect& r = mr.second;
      if (path == c["image"].as_string() && mr.first == c["map"].as_int() && c["x"].as_int() >= r.x &&
          c["x"].as_int() < r.x + r.w && c["y"].as_int() >= r.y && c["y"].as_int() < r.y + r.h)
        inside = true;
    }
    if (!inside) std::fprintf(stderr, "  dot outside its map: %s\n", name.c_str());
    CHECK(inside);
  }
  CHECK(checks > 400);
}

// Format 1.1: the rooms, their door markers and groups (tracker_maps.md "Rooms").
TEST(tracker_layout_rooms) {
  const game::ap::Json j = layout();
  CHECK(j["format_minor"].as_int() >= 1);
  const auto& rooms = j["rooms"];
  CHECK(rooms.size() > 700);
  struct Box {
    s64 x, y, w, h;
  };
  auto box_of = [](const game::ap::Json& b) { return Box{b[0].as_int(), b[1].as_int(), b[2].as_int(), b[3].as_int()}; };
  auto apart = [](const Box& a, const Box& b) { return a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y; };
  std::set<std::string> paths;
  std::map<s64, std::set<s64>> doorways;  // map -> its doorway ids
  std::map<s64, const game::ap::Json*> by_map;
  for (size_t i = 0; i < rooms.size(); ++i) {
    const auto& r = rooms[i];
    const s64 map = r["map"].as_int();
    by_map[map] = &r;
    const std::string path = r["path"].as_string();
    CHECK(path == "images/rooms/" + std::to_string(map) + ".png");
    CHECK(paths.insert(path).second);
    const s64 W = r["width"].as_int(), H = r["height"].as_int(), fs_ = r["font_scale"].as_int(), top = r["bg_y"].as_int();
    CHECK(W >= 64 && H > top && W <= 4096 && H <= 4096);  // backgrounds are 240 to 1440 px wide
    CHECK(fs_ >= 1 && fs_ <= 4 && top == 10 * fs_ && r["location_size"].as_int() == 12 * fs_);
    const Box strip = box_of(r["label_box"]);
    CHECK(strip.x == 0 && strip.y == 0 && strip.w == W && strip.h == top);
    CHECK(r["label"].as_string().rfind(std::to_string(map) + " ", 0) == 0);
    std::vector<Box> boxes;
    std::map<s64, std::string> letter_of;
    std::set<std::string> letters;
    for (size_t k = 0; k < r["doors"].size(); ++k) {
      const auto& d = r["doors"][k];
      const Box b = box_of(d["box"]);
      CHECK(b.x >= 0 && b.y >= top && b.x + b.w <= W && b.y + b.h <= H);  // on the background, under the strip
      for (const Box& o : boxes) CHECK(apart(b, o));
      boxes.push_back(b);
      CHECK(d["x"].as_int() >= 0 && d["x"].as_int() < W && d["y"].as_int() >= top && d["y"].as_int() < H);
      const s64 id = d["id"].as_int();
      CHECK(id / 65536 == map);  // a doorway id is one of the map's door ids
      const std::string letter = d["letter"].as_string();
      if (letter_of.count(id)) CHECK(letter_of[id] == letter);  // one letter per doorway
      else CHECK(letters.insert(letter).second);
      letter_of[id] = letter;
      doorways[map].insert(id);
      CHECK(d["to"].size() > 0);
      s64 lowest = d["to"][0]["door"].as_int();
      for (size_t t = 0; t < d["to"].size(); ++t) lowest = std::min(lowest, d["to"][t]["door"].as_int());
      CHECK_EQ(lowest, id);  // the doorway id is its lowest door id
    }
  }
  // Every door's way back names a doorway of the target room.
  int backs = 0;
  for (size_t i = 0; i < rooms.size(); ++i)
    for (size_t k = 0; k < rooms[i]["doors"].size(); ++k)
      for (size_t t = 0; t < rooms[i]["doors"][k]["to"].size(); ++t) {
        const auto& to = rooms[i]["doors"][k]["to"][t];
        if (!to.has("back")) continue;
        ++backs;
        CHECK(doorways[to["map"].as_int()].count(to["back"].as_int()) == 1);
      }
  CHECK(backs > 1500);
  // Each room in exactly one group, under its own area.
  std::map<s64, int> grouped;
  for (size_t g = 0; g < j["room_groups"].size(); ++g) {
    const auto& group = j["room_groups"][g];
    for (size_t k = 0; k < group["rooms"].size(); ++k) {
      const s64 m = group["rooms"][k].as_int();
      ++grouped[m];
      CHECK(by_map.count(m) == 1 && (*by_map[m])["area"].as_string() == group["area"].as_string());
    }
  }
  CHECK_EQ(grouped.size(), rooms.size());
  for (const auto& [m, n] : grouped) CHECK_EQ(n, 1);
  // Every check also on its map's room, on the background.
  for (const auto& [name, c] : j["checks"].items()) {
    const auto& room = c["room"];
    CHECK(room["image"].as_string() == "images/rooms/" + std::to_string(c["map"].as_int()) + ".png");
    const auto* r = by_map.count(c["map"].as_int()) ? by_map[c["map"].as_int()] : nullptr;
    CHECK(r != nullptr);
    if (r)
      CHECK(room["x"].as_int() >= 0 && room["x"].as_int() < (*r)["width"].as_int() &&
            room["y"].as_int() >= (*r)["bg_y"].as_int() && room["y"].as_int() < (*r)["height"].as_int());
  }
}

TEST(tracker_override_dir) {
  const fs::path root = scratch("override");
  const fs::path home = root / "home", docs = root / "home" / "Documents";
  fs::create_directories(docs);
  const fs::path tail = fs::path("PopTracker") / "user-override" / "so2_shattered_stars";
  // No PopTracker folder yet: under Documents.
  CHECK(tools::poptracker_override_dir(home, docs) == docs / tail);
  // PopTracker's own user folder exists: its user-override.
  fs::create_directories(home / "PopTracker");
  CHECK(tools::poptracker_override_dir(home, docs) == home / tail);
  // An existing pack override wins, in PopTracker's order (home first).
  fs::create_directories(docs / tail);
  CHECK(tools::poptracker_override_dir(home, docs) == docs / tail);
  fs::create_directories(home / tail);
  CHECK(tools::poptracker_override_dir(home, docs) == home / tail);
  // Documents unknown: the home one.
  CHECK(tools::poptracker_override_dir(home, fs::path()) == home / tail);
  fs::remove_all(root);
}

TEST(tracker_disc_backgrounds) {
  auto d = open_disc1();
  if (!d.ok) return;
  tools::Background a, b;
  std::string error;
  CHECK(tools::render_field_background(d.archive, 25, &a, &error));
  CHECK_EQ(a.width, 320);
  CHECK_EQ(a.height, 240);
  CHECK(tools::render_field_background(d.archive, 25, &b, &error));
  CHECK(a.pixels == b.pixels);
  // A parallax map (Heraldry Forest), put together from screens: covered, not one colour.
  CHECK(tools::render_field_background(d.archive, 459, &a, &error));
  std::set<u32> colours(a.pixels.begin(), a.pixels.end());
  CHECK(colours.size() > 100);
}

TEST(tracker_disc_command) {
  auto d = open_disc1();
  if (!d.ok) return;
  const fs::path one = scratch("one"), two = scratch("two");
  tools::TrackerRenderOptions options;
  options.only = {"renas_room", "fienal", "world_energy", "rooms/38.", "rooms/18."};
  CHECK_EQ(tools::render_tracker_maps(d.archive, one / "out", options), 0);
  CHECK_EQ(tools::render_tracker_maps(d.archive, two / "out", options), 0);
  const game::ap::Json j = layout();
  std::set<fs::path> expected;
  for (size_t i = 0; i < j["images"].size(); ++i) {
    const auto& img = j["images"][i];
    const std::string path = img["path"].as_string();
    if (path.find("renas_room") == std::string::npos && path.find("fienal") == std::string::npos &&
        path.find("world_energy") == std::string::npos)
      continue;
    expected.insert(fs::path(path));
    const std::vector<u8> a = read_file(one / "out" / path), b = read_file(two / "out" / path);
    CHECK(!a.empty());
    CHECK(a == b);  // the same bytes twice
    int w = 0, h = 0;
    std::vector<u32> px;
    CHECK(decode_png_rgb(a, &w, &h, &px));
    CHECK_EQ(w, img["width"].as_int());
    CHECK_EQ(h, img["height"].as_int());
  }
  CHECK_EQ(static_cast<int>(expected.size()), 3);
  // Two rooms (Rena's Room, the fountain square): sizes, the same bytes twice, a letter box
  // inside its border in the door colour, the strip in the background colour.
  int rooms = 0;
  for (size_t i = 0; i < j["rooms"].size(); ++i) {
    const auto& room = j["rooms"][i];
    const std::string path = room["path"].as_string();
    if (path != "images/rooms/38.png" && path != "images/rooms/18.png") continue;
    ++rooms;
    expected.insert(fs::path(path));
    const std::vector<u8> a = read_file(one / "out" / path), b = read_file(two / "out" / path);
    CHECK(!a.empty());
    CHECK(a == b);
    int w = 0, h = 0;
    std::vector<u32> px;
    CHECK(decode_png_rgb(a, &w, &h, &px));
    CHECK_EQ(w, room["width"].as_int());
    CHECK_EQ(h, room["height"].as_int());
    if (w != room["width"].as_int() || h != room["height"].as_int()) continue;
    const int scale = static_cast<int>(room["font_scale"].as_int());
    auto at = [&](s64 x, s64 y) { return px[static_cast<size_t>(y) * w + static_cast<size_t>(x)] & 0xFFFFFFu; };
    CHECK_EQ(at(w - 1, 0), 0x221C1Cu);  // style.background #1c1c22 (red in the low byte)
    CHECK(room["doors"].size() > 0);
    for (size_t k = 0; k < room["doors"].size(); ++k) {
      const auto& box = room["doors"][k]["box"];
      CHECK_EQ(at(box[0].as_int() + scale, box[1].as_int() + scale), 0x3CD2FFu);  // style.door.fill #ffd23c
      CHECK_EQ(at(box[0].as_int(), box[1].as_int()), 0x1A1414u);                 // its outline #14141a
    }
  }
  CHECK_EQ(rooms, 2);
  // Nothing but those PNGs (and their folders) under the target, nothing beside it.
  for (const fs::path& root : {one, two}) {
    std::set<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(root))
      if (e.is_regular_file()) files.insert(fs::relative(e.path(), root / "out"));
    CHECK(files == expected);
  }
  fs::remove_all(one);
  fs::remove_all(two);
}

// [utzip] util::ZipWriter: stored entries, the fixed date, sorted as given, CRCs (util::crc32
// against mods::crc32), read back by mods::ZipReader; the same entries give the same bytes;
// finish() replaces an existing file; refused names, abandon() and an unfinished writer leave
// nothing under either name.
TEST(tracker_zip_writer) {
  const fs::path dir = scratch("zipwriter");
  std::vector<u8> blob(70000);
  for (size_t i = 0; i < blob.size(); ++i) blob[i] = static_cast<u8>(i * 2654435761u >> 13);
  const std::vector<std::pair<std::string, std::vector<u8>>> entries = {
      {"a.txt", {'h', 'i'}}, {"empty", {}}, {"images/maps/x.png", blob}, {"images/rooms/\xc3\xa9t\xc3\xa9.png", {1, 2, 3}}};
  auto write = [&](const fs::path& path) {
    util::ZipWriter w;
    std::string error;
    bool ok = w.open(path, &error);
    for (const auto& [name, data] : entries) ok = ok && w.add(name, data, &error);
    CHECK(fs::exists(w.part_path()));
    CHECK(!fs::exists(path) || path.filename() == "again.zip");  // nothing under the real name before finish()
    ok = ok && w.finish(&error);
    if (!ok) std::fprintf(stderr, "  zip writer: %s\n", error.c_str());
    return ok;
  };
  CHECK(write(dir / "one.zip"));
  CHECK(write(dir / "two.zip"));
  const std::vector<u8> one = read_file(dir / "one.zip");
  CHECK(!one.empty() && one == read_file(dir / "two.zip"));  // the same bytes twice
  CHECK(parse_written_zip(one) == entries);
  // UTF-8 flag only on the non-ASCII name (local header flags at +6 of the fourth entry).
  mods::ZipReader reader;
  std::string error;
  CHECK(reader.open_file(dir / "one.zip", &error));
  CHECK_EQ(reader.entries().size(), entries.size());
  for (size_t k = 0; k < reader.entries().size() && k < entries.size(); ++k) {
    const mods::ZipEntry& e = reader.entries()[k];
    CHECK(e.name == entries[k].first && e.method == 0 && !e.directory);
    std::vector<u8> data;
    CHECK(reader.read(e, &data, &error, 1 << 20));  // checks the CRC
    CHECK(data == entries[k].second);
    CHECK_EQ(le16(one.data() + e.local_offset + 6), k == 3 ? 0x800 : 0);
  }
  // finish() over an existing file replaces it.
  { std::ofstream(dir / "again.zip") << "old"; }
  CHECK(write(dir / "again.zip"));
  CHECK(read_file(dir / "again.zip") == one);
  // Refused names: false, and nothing left (the .part removed).
  for (const char* name : {"", "/abs.png", "../up.png", "a/../b.png", "a//b.png", "a\\b.png", "c:x.png", "dir/", "./a.png",
                           "a\nb"}) {
    util::ZipWriter w;
    CHECK(w.open(dir / "bad.zip", &error));
    CHECK(!w.add(name, entries[0].second, &error));
    CHECK(!w.is_open());
  }
  // abandon(), and a writer dropped without finish(): nothing under either name.
  {
    util::ZipWriter w;
    CHECK(w.open(dir / "dropped.zip", &error) && w.add("a.txt", entries[0].second, &error));
    util::ZipWriter v;
    CHECK(v.open(dir / "abandoned.zip", &error) && v.add("a.txt", entries[0].second, &error));
    v.abandon();
    CHECK(!fs::exists(dir / "abandoned.zip.part"));
    CHECK(!v.finish(&error));
  }
  CHECK(files_under(dir) == (std::set<std::string>{"again.zip", "one.zip", "two.zip"}));
  // A folder that doesn't exist: open() fails and says so.
  util::ZipWriter w;
  CHECK(!w.open(dir / "no" / "such" / "x.zip", &error) && !error.empty());
  fs::remove_all(dir);
}

// [utzip] Where the zip goes by default: beside the image folder, in its parent.
TEST(tracker_ut_zip_path) {
  const fs::path root = scratch("utzippath");
  const fs::path over = root / "PopTracker" / "user-override" / "so2_shattered_stars";
  const fs::path want = root / "PopTracker" / "user-override" / "so2_shattered_stars_ut.zip";
  CHECK(tools::tracker_ut_zip_path(over) == want);
  CHECK(tools::tracker_ut_zip_path(over / "") == want);  // a trailing separator
  CHECK(tools::tracker_ut_zip_path(over / "." ) == want);
  CHECK(tools::tracker_ut_zip_path(fs::path("relative") / "out") ==
        (fs::current_path() / "relative" / "so2_shattered_stars_ut.zip").lexically_normal());
  CHECK(tools::tracker_ut_zip_path(over).filename() == tools::kTrackerUtZipName);
  CHECK(files_under(root).empty());  // only a name: nothing is made
  fs::remove_all(root);
}

// [utzip] The zip's files that are no render (trackers/poptracker/boards.json): each board at the
// pack's size, every cell at the pack's box (the cell colour where UT puts its square, the
// outline on the box's edge), the background between cells; the player marker 64x64 RGBA,
// clear in the corners, red in the ring, white in the centre. The same bytes twice.
TEST(tracker_ut_extras) {
  game::ap::Json boards;
  std::string error;
  CHECK(game::ap::Json::parse(std::string(tools::tracker_boards_json()), &boards, &error));
  CHECK(boards["pack_uid"].as_string() == "so2_shattered_stars");
  std::vector<std::pair<std::string, std::vector<u8>>> extras, again;
  CHECK(tools::render_tracker_ut_extras(&extras, &error));
  CHECK(tools::render_tracker_ut_extras(&again, &error));
  CHECK(extras == again);
  CHECK_EQ(extras.size(), boards["boards"].size() + 1);
  CHECK(std::is_sorted(extras.begin(), extras.end()));
  std::map<std::string, const std::vector<u8>*> by_path;
  for (const auto& [path, png] : extras) by_path[path] = &png;
  const game::ap::Json& style = boards["style"];
  auto colour = [&](const char* key) {
    const std::string hex = style[key].as_string();
    const u32 v = static_cast<u32>(std::strtoul(hex.c_str() + 1, nullptr, 16));
    return (v >> 16 & 0xFF) | (v & 0xFF00) | (v & 0xFF) << 16;  // red in the low byte
  };
  int cells = 0;
  for (size_t i = 0; i < boards["boards"].size(); ++i) {
    const auto& b = boards["boards"][i];
    const std::string path = b["path"].as_string();
    CHECK(path.rfind("images/boards/", 0) == 0);
    CHECK(by_path.count(path) == 1);
    if (!by_path.count(path)) continue;
    int w = 0, h = 0;
    std::vector<u32> px;
    CHECK(decode_png_rgb(*by_path[path], &w, &h, &px));
    CHECK_EQ(w, b["width"].as_int());
    CHECK_EQ(h, b["height"].as_int());
    if (w != b["width"].as_int() || h != b["height"].as_int()) continue;
    auto at = [&](s64 x, s64 y) { return px[static_cast<size_t>(y) * w + static_cast<size_t>(x)] & 0xFFFFFFu; };
    CHECK_EQ(at(w - 1, h - 1), colour("background"));
    for (size_t k = 0; k < b["cells"].size(); ++k, ++cells) {
      const auto& c = b["cells"][k];
      const s64 x = c[0].as_int(), y = c[1].as_int(), cw = c[2].as_int(), ch = c[3].as_int();
      CHECK(x >= 0 && y >= 0 && x + cw <= w && y + ch <= h);
      CHECK_EQ(at(x + 24, y + ch / 2), colour("cell"));    // under UT's square
      CHECK_EQ(at(x + cw / 2, y), colour("outline"));       // the top edge
      CHECK_EQ(at(x + cw / 2, y + ch - 1), colour("outline"));
      CHECK_EQ(at(x + cw + 1 < w ? x + cw + 1 : x - 2, y + ch / 2), colour("background"));  // the gap
      bool ink = false;  // the label's text right of the square
      for (s64 yy = y + 4; yy < y + ch - 4 && !ink; ++yy)
        for (s64 xx = x + style["label_x"].as_int(); xx < x + cw - 10 && !ink; ++xx) ink = at(xx, yy) == colour("text");
      CHECK(ink);
    }
  }
  CHECK(cells > 100);
  const auto& icon = boards["player_icon"];
  CHECK(icon["path"].as_string() == "images/status/you.png");
  CHECK(by_path.count("images/status/you.png") == 1);
  if (by_path.count("images/status/you.png")) {
    int w = 0, h = 0, channels = 0;
    std::vector<u32> px;
    CHECK(decode_png(*by_path["images/status/you.png"], &w, &h, &px, &channels));
    CHECK(w == 64 && h == 64 && channels == 4);
    if (w == 64 && h == 64) {
      CHECK_EQ(px[0] >> 24, 0u);                        // a clear corner
      CHECK_EQ(px[32 * 64 + 32], 0xFFFFFFFFu);          // the white centre
      CHECK_EQ(px[32 * 64 + 32 - 14], 0xFF2A30E0u);     // the red ring (#e0302a)
      CHECK_EQ(px[32 * 64 + 32 - 23], 0xFFFFFFFFu);     // the white rim
    }
  }
}

// [utzip] With the disc: a render's zip at its default place (beside DIR) holds every rendered
// image, byte for byte, and the extras, sorted; a second render's zip is the same bytes; nothing
// is written outside DIR and the zip; the progress calls in order; a stop before the zip, or in
// it, writes no zip and leaves an earlier one as it was; --no-ut-zip's empty path writes none.
TEST(tracker_disc_ut_zip) {
  auto d = open_disc1();
  if (!d.ok) return;
  const fs::path one = scratch("utzip_one"), two = scratch("utzip_two"), stop = scratch("utzip_stop");
  tools::TrackerRenderOptions options;
  options.only = {"renas_room", "rooms/18.", "rooms/38."};
  std::vector<tools::TrackerRenderProgress> seen;
  options.progress = [&](const tools::TrackerRenderProgress& p) {
    seen.push_back(p);
    return true;
  };
  options.ut_zip = tools::tracker_ut_zip_path(one / "out");
  CHECK(options.ut_zip == one / tools::kTrackerUtZipName);
  CHECK_EQ(tools::render_tracker_maps(d.archive, one / "out", options), 0);
  options.ut_zip = tools::tracker_ut_zip_path(two / "out");
  CHECK_EQ(tools::render_tracker_maps(d.archive, two / "out", options), 0);
  const std::vector<u8> zip = read_file(one / tools::kTrackerUtZipName);
  CHECK(!zip.empty() && zip == read_file(two / tools::kTrackerUtZipName));  // the same bytes twice
  std::vector<std::pair<std::string, std::vector<u8>>> extras;
  std::string error;
  CHECK(tools::render_tracker_ut_extras(&extras, &error));
  const std::set<std::string> rendered = files_under(one / "out");
  CHECK_EQ(rendered.size(), 3u);
  const auto entries = parse_written_zip(zip);
  CHECK_EQ(entries.size(), rendered.size() + extras.size());
  std::set<std::string> names;
  for (size_t k = 0; k < entries.size(); ++k) {
    const auto& [name, data] = entries[k];
    names.insert(name);
    if (k > 0) CHECK(entries[k - 1].first < name);  // sorted
    if (rendered.count(name)) CHECK(data == read_file(one / "out" / name));  // the render, byte for byte
    else CHECK(std::find(extras.begin(), extras.end(), entries[k]) != extras.end());
  }
  for (const std::string& r : rendered) CHECK(names.count(r) == 1);
  // Nothing outside the targets: the images under out/, the zip beside it, no .part.
  for (const fs::path& root : {one, two}) {
    std::set<std::string> want = {std::string(tools::kTrackerUtZipName)};
    for (const std::string& r : rendered) want.insert("out/" + r);
    CHECK(files_under(root) == want);
  }
  // The progress: 3 images, then the zip's files (path: the zip), its "in place" call, the end.
  const size_t n = entries.size();
  CHECK_EQ(seen.size(), 2 * (3 + n + 1 + 1));
  if (seen.size() >= 3 + n + 2) {
    for (size_t k = 0; k < 3; ++k) CHECK(seen[k].kind != "zip" && seen[k].done == static_cast<int>(k) && seen[k].total == 3);
    for (size_t k = 0; k <= n; ++k) {
      const auto& p = seen[3 + k];
      CHECK(p.kind == "zip" && p.done == static_cast<int>(k) && p.total == static_cast<int>(n) && p.failed == 0);
      CHECK(fs::path(p.path) == one / tools::kTrackerUtZipName);
      CHECK(p.name == (k < n ? entries[k].first : std::string()));
    }
    const auto& last = seen[3 + n + 1];
    CHECK(last.path.empty() && last.kind.empty() && last.done == 3 && last.total == 3);
  }
  // Stops: an earlier zip stays as it was; no zip is made where there was none; no .part.
  const fs::path old_zip = stop / tools::kTrackerUtZipName;
  { std::ofstream(old_zip, std::ios::binary) << "an earlier run's zip"; }
  for (int at : {-1, 0, 2}) {  // before the zip (the second image), at its first file, at its third
    int calls = 0;
    options.ut_zip = old_zip;
    options.progress = [&](const tools::TrackerRenderProgress& p) {
      ++calls;
      return at < 0 ? calls < 2 : !(p.kind == "zip" && p.done == at);
    };
    CHECK_EQ(tools::render_tracker_maps(d.archive, stop / "out", options), tools::kTrackerRenderStopped);
    CHECK(read_file(old_zip) == std::vector<u8>({'a', 'n', ' ', 'e', 'a', 'r', 'l', 'i', 'e', 'r', ' ', 'r', 'u', 'n',
                                                 '\'', 's', ' ', 'z', 'i', 'p'}));
    options.ut_zip = stop / "new" / "fresh.zip";
    CHECK_EQ(tools::render_tracker_maps(d.archive, stop / "out", options), tools::kTrackerRenderStopped);
    CHECK(!fs::exists(stop / "new" / "fresh.zip"));
  }
  for (const std::string& f : files_under(stop)) CHECK(f.size() < 5 || f.substr(f.size() - 5) != ".part");
  // No zip asked (--no-ut-zip): none written.
  options.ut_zip.clear();
  options.progress = nullptr;
  fs::remove_all(stop);
  fs::create_directories(stop);
  CHECK_EQ(tools::render_tracker_maps(d.archive, stop / "out", options), 0);
  CHECK_EQ(files_under(stop).size(), 3u);
  fs::remove_all(one);
  fs::remove_all(two);
  fs::remove_all(stop);
}
