// The tracker maps [maprender] (tools/tracker_maps.h, util/deflate.h; docs/ap/tracker_maps.md):
// the deflate stream and the PNGs, the built-in layout, PopTracker's override folder, and
// with the player's disc (SS_DISC1; skipped without it) the backgrounds and the command's
// output: sizes as the layout says, the same bytes twice, nothing else written.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// Decodes an 8-bit RGB PNG (the encoder's own output: one IDAT) into RGBA (red low).
bool decode_png_rgb(const std::vector<u8>& png, int* w, int* h, std::vector<u32>* pixels) {
  if (png.size() < 33 || png[1] != 'P' || png[2] != 'N' || png[3] != 'G') return false;
  std::vector<u8> idat;
  for (size_t at = 8; at + 12 <= png.size();) {
    const u32 len = be32(png.data() + at);
    const std::string type(reinterpret_cast<const char*>(png.data() + at + 4), 4);
    if (type == "IHDR") {
      *w = static_cast<int>(be32(png.data() + at + 8));
      *h = static_cast<int>(be32(png.data() + at + 12));
      if (png[at + 16] != 8 || png[at + 17] != 2) return false;
    } else if (type == "IDAT") {
      idat.insert(idat.end(), png.begin() + static_cast<long>(at + 8), png.begin() + static_cast<long>(at + 8 + len));
    }
    at += 12 + len;
  }
  std::vector<u8> raw;
  if (!inflate_zlib(idat, &raw)) return false;
  const size_t stride = static_cast<size_t>(*w) * 3;
  if (raw.size() != (stride + 1) * *h) return false;
  std::vector<u8> prior(stride, 0), row(stride);
  pixels->assign(static_cast<size_t>(*w) * *h, 0);
  for (int y = 0; y < *h; ++y) {
    const u8 f = raw[y * (stride + 1)];
    const u8* in = raw.data() + y * (stride + 1) + 1;
    for (size_t i = 0; i < stride; ++i) {
      const int a = i >= 3 ? row[i - 3] : 0, b = prior[i], c = i >= 3 ? prior[i - 3] : 0;
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
      (*pixels)[static_cast<size_t>(y) * *w + x] =
          row[x * 3] | row[x * 3 + 1] << 8 | static_cast<u32>(row[x * 3 + 2]) << 16 | 0xFF000000u;
    prior.swap(row);
  }
  return true;
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

fs::path scratch(const char* name) {
  const fs::path p = fs::current_path() / "tracker_maps_test" / name;
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
  options.only = {"renas_room", "fienal", "world_energy"};
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
