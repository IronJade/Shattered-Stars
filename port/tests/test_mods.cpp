// Mod packages (mods/catalog.h, docs/port/notes/mods.md): manifests, scanning the mods folder,
// the load order and its defaults (loose patches on, packages off), conflict analysis,
// .zip reading (stored and deflated, in memory or from the file), installing / removing
// packages, and Archipelago's output archives (launcher/ap_seed.h). The last test
// runs the game (SS_DISC1 and the game built next to the tests) to check that the engine
// applies the enabled patches in the configured order. Scratch files go in
// test_mods_scratch/ next to the test program; the player's settings are never read.

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "launcher/ap_seed.h"
#include "launcher/config.h"
#include "launcher/process.h"
#include "launcher/settings.h"
#include "mods/catalog.h"
#include "mods/patch.h"
#include "mods/zip.h"
#include "psx/memory_card.h"
#include "test.h"
#include "util/sha256.h"
#include "game/ap/ap.h"

using namespace ss;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
  const char* base = SDL_GetBasePath();
  const fs::path dir = fs::path(base ? base : ".") / "test_mods_scratch" / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

void write_file(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << text;
}

bool contains(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

std::string manifest(const std::string& id, const std::string& patches, const std::string& extra = "") {
  return "{\n  \"format\": 1,\n  \"id\": \"" + id + "\",\n  \"name\": \"Mod " + id +
         "\",\n  \"version\": \"1.2.0\",\n  \"author\": \"Tester\",\n  \"description\": \"A test mod.\",\n"
         "  \"min_port_version\": \"0.01.00\",\n  \"patches\": [" +
         patches + "]" + extra + "\n}\n";
}

// A minimal .zip writer for the tests: stored entries, or deflated ones from given bytes.
struct ZipFile {
  std::string name;
  std::string data;        // the uncompressed bytes
  std::vector<u8> deflated;  // empty: stored
};

void put16(std::string& s, u32 v) {
  s += static_cast<char>(v & 0xFF);
  s += static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& s, u32 v) {
  put16(s, v & 0xFFFF);
  put16(s, v >> 16);
}

std::string make_zip(const std::vector<ZipFile>& files) {
  std::string out, central;
  for (const ZipFile& f : files) {
    const u32 crc = mods::crc32(reinterpret_cast<const u8*>(f.data.data()), f.data.size());
    const bool deflate = !f.deflated.empty();
    const std::string body = deflate ? std::string(f.deflated.begin(), f.deflated.end()) : f.data;
    const u32 offset = static_cast<u32>(out.size());
    put32(out, 0x04034B50);
    put16(out, 20);
    put16(out, 0);
    put16(out, deflate ? 8 : 0);
    put32(out, 0);
    put32(out, crc);
    put32(out, static_cast<u32>(body.size()));
    put32(out, static_cast<u32>(f.data.size()));
    put16(out, static_cast<u32>(f.name.size()));
    put16(out, 0);
    out += f.name + body;
    put32(central, 0x02014B50);
    put16(central, 20);
    put16(central, 20);
    put16(central, 0);
    put16(central, deflate ? 8 : 0);
    put32(central, 0);
    put32(central, crc);
    put32(central, static_cast<u32>(body.size()));
    put32(central, static_cast<u32>(f.data.size()));
    put16(central, static_cast<u32>(f.name.size()));
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put32(central, 0);
    put32(central, offset);
    central += f.name;
  }
  const u32 cd_offset = static_cast<u32>(out.size());
  out += central;
  put32(out, 0x06054B50);
  put16(out, 0);
  put16(out, 0);
  put16(out, static_cast<u32>(files.size()));
  put16(out, static_cast<u32>(files.size()));
  put32(out, static_cast<u32>(central.size()));
  put32(out, cd_offset);
  put16(out, 0);
  return out;
}

// Raw deflate streams made by zlib (level 9): one fixed-Huffman block, one dynamic.
const std::vector<u8> kFixedDeflate = {0x2b, 0x4e, 0x2e, 0xca, 0x2c, 0x28, 0x51, 0x30, 0xb4, 0x50, 0x30,
                                       0xa8, 0xb0, 0x30, 0x32, 0x06, 0x92, 0x86, 0x46, 0x06, 0x06, 0x06,
                                       0x46, 0x89, 0xa6, 0x70, 0x66, 0x92, 0x01, 0x17, 0x00};
const char kFixedText[] = "script 18 0x823 0x120002a5 0x120002b0\n";
const std::vector<u8> kDynamicDeflate = {
    0x75, 0xd5, 0x41, 0x6e, 0xdd, 0x30, 0x0c, 0x45, 0xd1, 0x79, 0x56, 0xf1, 0x80, 0x6e, 0xc0, 0x24, 0x25, 0x91, 0x5a,
    0x4e, 0x1a, 0x7c, 0xa0, 0x7f, 0x90, 0x22, 0x08, 0x92, 0xfd, 0x57, 0x8d, 0x49, 0xce, 0xde, 0xcc, 0x86, 0x2f, 0x64,
    0x18, 0x47, 0x94, 0x9f, 0x5f, 0x8f, 0x77, 0x08, 0x3e, 0x3e, 0x9f, 0x6f, 0x0f, 0xc8, 0xe5, 0x98, 0x02, 0xfc, 0xc2,
    0xdb, 0x9f, 0xc7, 0xeb, 0xc7, 0xe3, 0x13, 0xcf, 0xff, 0x8f, 0xff, 0x7e, 0xbf, 0xff, 0x3e, 0xd7, 0xf2, 0xf2, 0x73,
    0xa7, 0x15, 0xcb, 0xc0, 0x54, 0x16, 0xeb, 0x1d, 0x5b, 0xc5, 0x2a, 0x98, 0xc6, 0x62, 0xbb, 0xe3, 0xd1, 0x71, 0x60,
    0x0e, 0x16, 0x8f, 0x3b, 0x9e, 0x15, 0xdb, 0xc4, 0x9c, 0x2c, 0x9e, 0x77, 0xbc, 0x2a, 0x1e, 0x8a, 0xb9, 0x58, 0xbc,
    0xee, 0xd8, 0x3b, 0xde, 0x98, 0xce, 0x62, 0xbf, 0xe3, 0xa8, 0xf8, 0x2c, 0x3b, 0x83, 0xc5, 0x71, 0xc7, 0xbb, 0xe2,
    0x65, 0x98, 0x9b, 0xc5, 0xfb, 0x8e, 0xe5, 0xaa, 0xda, 0x2f, 0xac, 0x8b, 0xaa, 0x5c, 0x99, 0x37, 0xa2, 0x3b, 0x16,
    0x47, 0x4c, 0x45, 0x69, 0xc6, 0x18, 0x58, 0x94, 0x51, 0xd2, 0x51, 0x1a, 0x72, 0x0b, 0x16, 0x85, 0x94, 0x94, 0x94,
    0xa6, 0xdc, 0x81, 0x45, 0x29, 0x25, 0x2d, 0xa5, 0x30, 0xf5, 0x9a, 0x58, 0x14, 0x53, 0x52, 0x53, 0x8a, 0x53, 0xcf,
    0x57, 0x2c, 0xca, 0x29, 0xe9, 0x29, 0xde, 0xf9, 0xc6, 0xa2, 0xa0, 0x92, 0xa2, 0x52, 0xa4, 0xaa, 0x0b, 0x8b, 0x92,
    0x4a, 0x9a, 0x4a, 0xa1, 0xaa, 0x19, 0x16, 0x45, 0x95, 0x54, 0xd5, 0x52, 0xd5, 0x71, 0xc1, 0xa9, 0xaa, 0xa6, 0xaa,
    0x4a, 0xe7, 0x0e, 0xa7, 0xaa, 0x5a, 0xb3, 0x59, 0xaa, 0x7a, 0xe6, 0xc7, 0xf9, 0x70, 0xa6, 0xaa, 0x96, 0xaa, 0x9e,
    0x0d, 0xe3, 0x54, 0x55, 0x53, 0x55, 0x47, 0xe7, 0x01, 0xa7, 0xaa, 0x9a, 0xaa, 0xda, 0xaa, 0x3e, 0xe1, 0x54, 0x55,
    0x53, 0x55, 0x5b, 0x35, 0x14, 0x4e, 0x55, 0x35, 0x55, 0xb5, 0x55, 0x63, 0xc3, 0xa9, 0xaa, 0xa6, 0xaa, 0xb6, 0xea,
    0x5e, 0x70, 0xaa, 0xaa, 0xa9, 0xaa, 0xa5, 0x6a, 0x97, 0xc1, 0xa9, 0xaa, 0xa6, 0xaa, 0x95, 0xaa, 0x9d, 0xa9, 0x0d,
    0xaa, 0x6a, 0xa9, 0x6a, 0xd2, 0xb9, 0x23, 0xa8, 0xaa, 0xa5, 0xaa, 0x95, 0xaa, 0x1d, 0x81, 0xa0, 0xaa, 0x56, 0x67,
    0x6e, 0xa9, 0xda, 0x79, 0x4f, 0xf0, 0x43, 0x37, 0x55, 0x6d, 0x74, 0x1e, 0x08, 0xaa, 0x6a, 0xa9, 0x6a, 0xa5, 0x6a,
    0x63, 0x22, 0xa8, 0xaa, 0xa5, 0xaa, 0x95, 0xaa, 0x9d, 0x7f, 0x45, 0x50, 0x55, 0x4b, 0x55, 0xf3, 0xce, 0x37, 0x82,
    0xaa, 0x5a, 0xaa, 0x5a, 0xa9, 0xda, 0x39, 0x06, 0x82, 0xaa, 0x5a, 0xaa, 0x5a, 0xab, 0x9e, 0xcd, 0x1e, 0x54, 0xd5,
    0xf6, 0xcb, 0x3f};

std::string dynamic_text() {
  std::string out;
  for (int i = 1; i < 40; ++i) {
    char line[96];
    std::snprintf(line, sizeof line, "item %d price %d %d  # cheaper item number %d\n", i, 100 + i * 7, 50 + i, i);
    out += line;
  }
  return out;
}

std::vector<mods::Line> lines_of(const std::vector<std::pair<int, std::string>>& text) {
  std::vector<mods::Line> lines;
  int n = 0;
  for (const auto& [source, t] : text) mods::add_line(t, "l" + std::to_string(++n), source, &lines, nullptr);
  return lines;
}

}  // namespace

TEST(mods_patch_grammar) {
  mods::PatchLine p;
  CHECK(mods::parse_patch_line("item 0x15 price 0xc8 4321", &p));
  CHECK(p.kind == mods::Kind::kItem);
  CHECK_EQ(p.target, 0x15);
  CHECK_EQ(p.where, 0u);
  CHECK_EQ(p.size, 4);
  CHECK_EQ(p.value, 4321u);
  CHECK(mods::parse_patch_line("enemy 4 0x37 15 255", &p));  // drop1_odds, 1 byte by default
  CHECK_EQ(p.size, 1);
  CHECK(mods::describe_place(p) == "enemy 4 drop1_odds");
  CHECK(mods::parse_patch_line("enemy 4 hp 0x10 0x123456789", &p));  // masked to the field
  CHECK_EQ(p.value, 0x23456789u);
  CHECK(mods::parse_patch_line("shop 1 add 0 5", &p) == false);
  CHECK(mods::parse_patch_line("item 0 price 1 2", &p) == false);   // items start at 1
  CHECK(mods::parse_patch_line("item 5 0x2f:2 1 2", &p) == false);  // past the record
  CHECK(mods::parse_patch_line("door 18 1 2 3", &p) == false);
  CHECK(mods::parse_patch_line("script 18 5 1 2 3", &p) == false);
  std::string line = "script 18 0x823 0x120002a5 0x120002b0  # a chest\r";
  CHECK(mods::strip_comment(line));
  CHECK(mods::parse_patch_line(line, &p));
  CHECK(mods::describe_place(p) == "map 18 word 0x823");
  std::string blank = "   # only a comment";
  CHECK(!mods::strip_comment(blank));
}

TEST(mods_manifest_parsing) {
  mods::Manifest m;
  std::string error;
  std::vector<std::string> warnings;
  CHECK(mods::parse_manifest(manifest("arlia-chest", "\"a.patch\", \"b.patch\"",
                                      ",\n  \"tags\": [\"chests\"], \"homepage\": \"https://x.y\", \"later\": 1"),
                             &m, &error, &warnings));
  CHECK(m.id == "arlia-chest");
  CHECK(m.version == "1.2.0");
  CHECK_EQ(m.patches.size(), 2u);
  CHECK(m.patches[1] == "b.patch");
  CHECK_EQ(m.tags.size(), 1u);
  CHECK_EQ(warnings.size(), 1u);  // the unknown key
  CHECK(!warnings.empty() && contains(warnings[0], "later"));

  // Escapes, a byte-order mark, declared risks.
  CHECK(mods::parse_manifest("\xEF\xBB\xBF{\"format\":1,\"id\":\"x\",\"name\":\"Caf\\u00e9 \\ud83d\\ude00\","
                             "\"version\":\"2\",\"author\":\"a\",\"description\":\"d\",\"min_port_version\":\"0.1\","
                             "\"patches\":[\"p.patch\"],\"declared_risks\":[{\"rule\":\"price-zero\",\"reason\":\"free\","
                             "\"lines\":[\"p.patch:1\"]}]}",
                             &m, &error));
  CHECK(m.name == "Caf\xC3\xA9 \xF0\x9F\x98\x80");
  CHECK_EQ(m.declared_risks.size(), 1u);
  CHECK(!m.declared_risks.empty() && m.declared_risks[0].lines.size() == 1);

  auto refuses = [&](const std::string& text, const char* why) {
    mods::Manifest bad;
    std::string e;
    const bool ok = mods::parse_manifest(text, &bad, &e);
    CHECK(!ok);
    if (!contains(e, why)) std::fprintf(stderr, "  refusal '%s' lacks '%s'\n", e.c_str(), why);
    CHECK(contains(e, why));
  };
  refuses("{\"format\": 1, \"id\": \"x\",}", "isn't valid JSON");
  refuses("[1, 2]", "JSON object");
  refuses(manifest("Bad-Id", "\"a.patch\""), "isn't valid");
  refuses(manifest("x", "\"a.patch\"", ", \"format\": 2"), "newer version");
  refuses(manifest("x", ""), "\"patches\"");
  refuses(manifest("x", "\"sub/a.patch\""), "no folders");
  refuses(manifest("x", "\"a.txt\""), ".patch");
  refuses(manifest("x", "\"a.patch\", \"a.patch\""), "twice");
  refuses("{\"format\":1,\"id\":\"x\",\"name\":\"n\",\"version\":\"1.x\",\"patches\":[\"a.patch\"]}", "version");
  refuses("{\"format\":1,\"id\":\"x\",\"name\":\"n\",\n\"version\":\"1\",\n\"patches\":[\"a.patch\"\n", "line 4");
}

TEST(mods_versions) {
  CHECK_EQ(mods::compare_versions("0.01.00", "0.1"), 0);
  CHECK_EQ(mods::compare_versions("0.2", "0.01.05"), 1);
  CHECK_EQ(mods::compare_versions("1.0.9", "1.0.10"), -1);
  CHECK(mods::valid_version("0.01.00"));
  CHECK(mods::valid_version("3"));
  CHECK(!mods::valid_version("1..2"));
  CHECK(!mods::valid_version("1.2.3.4.5"));
  CHECK(!mods::valid_version("v1"));
  CHECK(mods::valid_id("arlia-chest.v2_b"));
  CHECK(!mods::valid_id("-x"));
  CHECK(!mods::valid_id("(loose)"));
  CHECK(!mods::valid_id(""));
}

TEST(mods_scan_and_loose_fallback) {
  const fs::path dir = scratch("scan");
  // Only loose files: one entry, enabled by default, files in name order (as before packages).
  write_file(dir / "20-b.patch", "item 5 price 10 20\n");
  write_file(dir / "10-a.patch", "# comment\nscript 18 0x10 1 2\nnot a patch line\n");
  write_file(dir / "notes.txt", "ignored\n");
  mods::Catalog c = mods::scan(dir, "0.01.00");
  CHECK(c.folder_exists);
  CHECK_EQ(c.entries.size(), 1u);
  CHECK(!c.has_packages());
  CHECK(c.entries[0].loose && c.entries[0].id == mods::kLooseId);
  CHECK_EQ(c.entries[0].files.size(), 2u);
  CHECK(c.entries[0].files[0].filename() == "10-a.patch");
  CHECK_EQ(c.entries[0].counts[0], 1);  // script
  CHECK_EQ(c.entries[0].counts[1], 1);  // item
  CHECK_EQ(c.entries[0].bad_lines, 1);
  std::vector<mods::Slot> order = mods::resolve_order(c, "");
  CHECK_EQ(order.size(), 1u);
  CHECK(order[0].enabled);
  std::vector<mods::Line> lines;
  std::vector<mods::BadLine> bad;
  mods::read_lines(c, order, &lines, &bad);
  CHECK_EQ(lines.size(), 2u);
  CHECK(lines[0].label == "10-a.patch:2");
  CHECK(lines[1].label == "20-b.patch:1");
  CHECK_EQ(bad.size(), 1u);
  CHECK(!bad.empty() && bad[0].label == "10-a.patch:3");

  // Packages: good, folder named differently, broken, missing file, too new, duplicate id.
  write_file(dir / "chests" / "mod.json", manifest("chests", "\"one.patch\", \"two.patch\""));
  write_file(dir / "chests" / "one.patch", "script 18 0x20 1 2\n");
  write_file(dir / "chests" / "two.patch", "shop 1 0 0x2d8 0x15\n");
  write_file(dir / "chests" / "README.md", "Chests, better.\n");
  write_file(dir / "chests" / "stray.patch", "item 5 price 10 30\n");
  write_file(dir / "zz-named-oddly" / "mod.json", manifest("enemies", "\"e.patch\""));
  write_file(dir / "zz-named-oddly" / "e.patch", "enemy 4 hp 0x96 1\n");
  write_file(dir / "broken" / "mod.json", "{ not json");
  write_file(dir / "missing" / "mod.json", manifest("missing", "\"gone.patch\""));
  write_file(dir / "future" / "mod.json",
             "{\"format\":1,\"id\":\"future\",\"name\":\"F\",\"version\":\"1\",\"author\":\"a\",\"description\":\"d\","
             "\"min_port_version\":\"9.0\",\"patches\":[\"f.patch\"]}");
  write_file(dir / "future" / "f.patch", "item 5 price 10 40\n");
  write_file(dir / "twin" / "mod.json", manifest("chests", "\"t.patch\""));
  write_file(dir / "twin" / "t.patch", "item 5 price 10 50\n");
  write_file(dir / "not-a-package" / "x.patch", "item 5 price 10 60\n");  // no mod.json: not scanned
  c = mods::scan(dir, "0.01.00");
  CHECK(c.has_packages());
  CHECK_EQ(c.entries.size(), 7u);  // loose + broken, chests, future, missing, twin, zz-named-oddly
  const int chests = c.find("chests"), enemies = c.find("enemies"), broken = c.find("broken"),
            missing = c.find("missing"), future = c.find("future");
  CHECK(chests > 0 && enemies > 0 && broken > 0 && missing > 0 && future > 0);
  if (chests > 0 && enemies > 0 && broken > 0 && missing > 0 && future > 0) {
    const mods::Entry& e = c.entries[static_cast<size_t>(chests)];
    CHECK(e.usable());
    CHECK_EQ(e.files.size(), 2u);
    CHECK(e.readme == "Chests, better.\n");
    CHECK_EQ(e.warnings.size(), 1u);  // stray.patch isn't listed
    CHECK(c.entries[static_cast<size_t>(enemies)].usable());
    CHECK(!c.entries[static_cast<size_t>(enemies)].warnings.empty());  // the folder isn't named after the id
    CHECK(contains(c.entries[static_cast<size_t>(broken)].error, "valid JSON"));
    CHECK(contains(c.entries[static_cast<size_t>(missing)].error, "gone.patch"));
    CHECK(contains(c.entries[static_cast<size_t>(future)].error, "needs port version 9.0"));
  }
  // The second package with id "chests" (folder twin) can't load.
  int twins = 0;
  for (const mods::Entry& e : c.entries)
    if (e.id == "chests") twins += e.usable() ? 1 : 100;
  CHECK_EQ(twins, 101);
}

TEST(mods_load_order) {
  const fs::path dir = scratch("order");
  write_file(dir / "a.patch", "item 5 price 10 20\n");
  for (const char* id : {"beta", "alpha", "gamma"}) {
    write_file(dir / id / "mod.json", manifest(id, "\"p.patch\""));
    write_file(dir / id / "p.patch", std::string("item 6 price 10 ") + (id[0] == 'a' ? "1" : id[0] == 'b' ? "2" : "3") + "\n");
  }
  const mods::Catalog c = mods::scan(dir, "0.01.00");
  auto ids = [&](const std::vector<mods::Slot>& order) {
    std::string s;
    for (const mods::Slot& slot : order) s += (slot.enabled ? "+" : "-") + c.entries[static_cast<size_t>(slot.entry)].id + " ";
    return s;
  };
  // No key: loose on at the front, packages off by id.
  CHECK(ids(mods::resolve_order(c, "")) == "+(loose) -alpha -beta -gamma ");
  // A key: its order; unknown ids and repeats dropped; unlisted entries get their defaults.
  const auto order = mods::resolve_order(c, " gamma , -(loose),nobody, alpha,gamma");
  CHECK(ids(order) == "+gamma -(loose) +alpha -beta ");
  CHECK(mods::format_order(c, order) == "gamma,-(loose),alpha,-beta");
  CHECK(mods::resolve_order(c, mods::format_order(c, order)) == order);
  CHECK(ids(mods::resolve_order(c, "beta")) == "+(loose) +beta -alpha -gamma ");
  // The enabled lines follow the order; disabled entries add nothing.
  std::vector<mods::Line> lines;
  mods::read_lines(c, order, &lines, nullptr);
  CHECK_EQ(lines.size(), 2u);
  CHECK(lines.size() == 2 && lines[0].label == "gamma/p.patch:1" && lines[1].label == "alpha/p.patch:1");

  // Through config.ini (launcher/settings.h): an empty order removes the key.
  launcher::Config config;
  launcher::write_mod_order(config, "gamma,-alpha");
  CHECK(launcher::read_mod_order(config) == "gamma,-alpha");
  launcher::write_mod_order(config, "");
  CHECK(!config.has("mod_order"));
}

TEST(mods_conflicts) {
  using mods::Relation;
  // Sources: -1 the seed, 0..3 entries in load order.
  const auto lines = lines_of({
      {0, "item 5 price 100 200"},    // 0: A
      {1, "item 5 price 100 300"},    // 1: B overrides A
      {2, "item 5 price 300 350"},    // 2: C builds on B
      {0, "shop 1 0 0x2d8 0x15"},     // 3
      {1, "shop 1 0 0x2d8 0x15"},     // 4: the same change
      {0, "shop 1 7 0 0x20"},         // 5: appends never conflict
      {1, "shop 1 7 0 0x21"},         // 6
      {-1, "script 18 0x20 1 2"},     // 7: the seed
      {0, "script 18 0x20 1 3"},      // 8: the seed wins
      {0, "enemy 4 hp 150 1"},        // 9
      {1, "enemy 4 0:2 150 2"},       // 10: overlaps
      {2, "enemy 4 exp 10 20"},       // 11
      {3, "enemy 4 exp 11 30"},       // 12: different EXPECTED
      {2, "item 9 atk 1 2"},          // 13
      {2, "item 9 atk 1 3"},          // 14: within one entry
  });
  CHECK_EQ(lines.size(), 15u);
  const mods::Analysis an = mods::analyse(lines);
  auto relation = [&](size_t a, size_t b) {
    for (const mods::Conflict& c : an.conflicts)
      if (c.first == a && c.second == b) return static_cast<int>(c.relation);
    return -1;
  };
  CHECK_EQ(relation(0, 1), static_cast<int>(Relation::kOverride));
  CHECK_EQ(relation(1, 2), static_cast<int>(Relation::kBuildsOn));
  CHECK_EQ(relation(3, 4), static_cast<int>(Relation::kSame));
  CHECK_EQ(relation(5, 6), -1);
  CHECK_EQ(relation(7, 8), static_cast<int>(Relation::kSeedWins));
  CHECK_EQ(relation(9, 10), static_cast<int>(Relation::kOverlap));
  CHECK_EQ(relation(11, 12), static_cast<int>(Relation::kMismatch));
  CHECK_EQ(relation(13, 14), static_cast<int>(Relation::kWithin));
  CHECK_EQ(an.overridden_by[0], 1);
  CHECK_EQ(an.overridden_by[1], -1);
  CHECK_EQ(an.redundant_with[4], 3);
  CHECK_EQ(an.overridden_by[7], -1);  // the seed is never overridden
  CHECK_EQ(an.same_place[1].size(), 2u);

  const auto name = [](int source) { return source < 0 ? std::string("the seed") : "Mod" + std::to_string(source); };
  for (const mods::Conflict& c : an.conflicts) {
    const std::string text = mods::explain(c, lines, name);
    CHECK(!text.empty());
    if (c.relation == Relation::kOverride) {
      CHECK(contains(text, "Mod0 and Mod1 both change item 5 price"));
      CHECK(contains(text, "Mod1 is later in the load order"));
    }
    if (c.relation == Relation::kSeedWins) CHECK(contains(text, "seed wins"));
  }
  int warnings = 0;
  for (const mods::Conflict& c : an.conflicts) warnings += c.is_warning() ? 1 : 0;
  // Override, seed, overlap, mismatch, within; A against C is left out (A is overridden).
  CHECK_EQ(warnings, 5);
  CHECK_EQ(relation(0, 2), -1);
}

TEST(mods_zip_reader) {
  std::vector<u8> out;
  std::string error;
  CHECK(mods::inflate(kFixedDeflate.data(), kFixedDeflate.size(), &out, 1 << 20, &error));
  CHECK(std::string(out.begin(), out.end()) == kFixedText);
  CHECK(mods::inflate(kDynamicDeflate.data(), kDynamicDeflate.size(), &out, 1 << 20, &error));
  CHECK(std::string(out.begin(), out.end()) == dynamic_text());
  CHECK(!mods::inflate(kDynamicDeflate.data(), kDynamicDeflate.size() / 2, &out, 1 << 20, &error));  // cut short
  CHECK(!mods::inflate(kDynamicDeflate.data(), kDynamicDeflate.size(), &out, 100, &error));          // too large
  CHECK_EQ(mods::crc32(reinterpret_cast<const u8*>(kFixedText), sizeof kFixedText - 1), 0x51af99fbu);

  const std::string zip = make_zip({{"a/stored.txt", "hello", {}}, {"a/fixed.patch", kFixedText, kFixedDeflate}});
  mods::ZipReader z;
  CHECK(z.open_memory(std::vector<u8>(zip.begin(), zip.end()), &error));
  CHECK_EQ(z.entries().size(), 2u);
  if (z.entries().size() == 2) {
    CHECK(z.read(z.entries()[0], &out, &error));
    CHECK(std::string(out.begin(), out.end()) == "hello");
    CHECK(z.read(z.entries()[1], &out, &error));
    CHECK(std::string(out.begin(), out.end()) == kFixedText);
  }
  // A damaged entry fails its checksum.
  std::string damaged = zip;
  damaged[damaged.find("hello")] = 'j';
  CHECK(z.open_memory(std::vector<u8>(damaged.begin(), damaged.end()), &error));
  CHECK(!z.read(z.entries()[0], &out, &error));
  CHECK(contains(error, "checksum"));
  CHECK(!z.open_memory(std::vector<u8>{'P', 'K', 3, 4}, &error));
}

// The file-backed reader (open_file): the directory first, each entry when it is read.
TEST(mods_zip_reader_from_file) {
  const fs::path dir = scratch("zipfile");
  std::string error;
  std::vector<u8> out;
  const std::string zip = make_zip({{"a/stored.txt", "hello", {}}, {"a/fixed.patch", kFixedText, kFixedDeflate}});
  write_file(dir / "a.zip", zip);
  mods::ZipReader z;
  CHECK(z.open_file(dir / "a.zip", &error));
  CHECK_EQ(z.entries().size(), 2u);
  if (z.entries().size() == 2) {
    CHECK(z.read(z.entries()[1], &out, &error));
    CHECK(std::string(out.begin(), out.end()) == kFixedText);
    CHECK(z.read(z.entries()[0], &out, &error));
    CHECK(std::string(out.begin(), out.end()) == "hello");
  }
  // The file cut short after its directory was read: the entry's data is missing.
  write_file(dir / "cut.zip", zip);
  CHECK(z.open_file(dir / "cut.zip", &error));
  write_file(dir / "cut.zip", zip.substr(0, 40));
  CHECK(!z.read(z.entries()[0], &out, &error));
  CHECK(!z.open_file(dir / "cut.zip", &error));
  CHECK(!z.open_file(dir / "missing.zip", &error));
  write_file(dir / "not.zip", "PK\x03\x04 not really");
  CHECK(!z.open_file(dir / "not.zip", &error));
  CHECK(contains(error, "not a .zip"));
}

// Archipelago's output archive (AP_<seed>.zip): every player's file, the spoiler log and the
// server's file; the Star Ocean seeds are found by their extension, one chosen by name.
TEST(launcher_archipelago_seed_archives) {
  const fs::path dir = scratch("apzip");
  std::string error, text, chosen;
  std::vector<std::string> names;
  const std::string claude = R"({"format": "apso2", "slot_name": "Claude"})";
  const std::string rena = R"({"format": "apso2", "slot_name": "Rena"})";
  write_file(dir / "AP_1.zip", make_zip({{"AP_1_Spoiler.txt", "spoiler", {}},
                                         {"AP_1.archipelago", "server", {}},
                                         {"AP_1_P1_Claude.apso2", claude, {}},
                                         {"AP_1_P2_Link.aplttp", "patch", {}},
                                         {"AP_1_P3_Rena.APSO2", rena, {}}}));
  write_file(dir / "AP_2.ZIP", make_zip({{"AP_2_Spoiler.txt", "spoiler", {}}, {"AP_2_P1_Claude.apso2", claude, {}}}));
  write_file(dir / "AP_3.zip", make_zip({{"AP_3_P1_Link.aplttp", "patch", {}}}));
  write_file(dir / "loose.apso2", rena);

  CHECK(launcher::is_seed_archive(dir / "AP_2.ZIP"));
  CHECK(!launcher::is_seed_archive(dir / "loose.apso2"));
  CHECK(launcher::list_archive_seeds(dir / "AP_1.zip", &names, &error));
  CHECK_EQ(names.size(), 2u);
  CHECK(names.size() == 2 && names[0] == "AP_1_P1_Claude.apso2" && names[1] == "AP_1_P3_Rena.APSO2");

  // Several seeds: one must be named (by its name in the archive or its file name).
  CHECK(!launcher::read_seed_text(dir / "AP_1.zip", "", &text, &chosen, &error));
  CHECK(contains(error, "2 Star Ocean seeds") && contains(error, "AP_1_P3_Rena.APSO2"));
  CHECK(launcher::read_seed_text(dir / "AP_1.zip", "AP_1_P3_Rena.APSO2", &text, &chosen, &error));
  CHECK(text == rena && chosen == "AP_1_P3_Rena.APSO2");
  CHECK(launcher::read_seed_text(dir / "AP_1.zip", "elsewhere/AP_1_P1_Claude.apso2", &text, &chosen, &error));
  CHECK(text == claude && chosen == "AP_1_P1_Claude.apso2");
  CHECK(!launcher::read_seed_text(dir / "AP_1.zip", "AP_1_P9_Nobody.apso2", &text, &chosen, &error));
  CHECK(contains(error, "no seed AP_1_P9_Nobody.apso2"));
  // One seed: taken without a name. None: said so. A loose file: read as it is.
  CHECK(launcher::read_seed_text(dir / "AP_2.ZIP", "", &text, &chosen, &error));
  CHECK(text == claude && chosen == "AP_2_P1_Claude.apso2");
  CHECK(!launcher::read_seed_text(dir / "AP_3.zip", "", &text, &chosen, &error));
  CHECK(contains(error, "no Star Ocean seed"));
  CHECK(launcher::read_seed_text(dir / "loose.apso2", "ignored", &text, &chosen, &error));
  CHECK(text == rena && chosen.empty());

  // The game's file: copied out of the archive into the cache (rewritten only when it
  // differs), or the loose file itself.
  fs::path file;
  const fs::path cache = dir / "cache";
  CHECK(launcher::seed_file(dir / "AP_1.zip", "AP_1_P3_Rena.APSO2", &file, &error, cache));
  CHECK(file == cache / "AP_1_P3_Rena.APSO2");
  CHECK(launcher::read_seed_text(file, "", &text, nullptr, &error) && text == rena);
  write_file(file, "stale");
  CHECK(launcher::seed_file(dir / "AP_1.zip", "AP_1_P3_Rena.APSO2", &file, &error, cache));
  CHECK(launcher::read_seed_text(file, "", &text, nullptr, &error) && text == rena);
  CHECK(!fs::exists(cache / "AP_1_P3_Rena.APSO2.tmp"));
  CHECK(!launcher::seed_file(dir / "AP_1.zip", "", &file, &error, cache));
  CHECK(launcher::seed_file(dir / "loose.apso2", "", &file, &error, cache));
  CHECK(file == dir / "loose.apso2");
}

// Where the Browse button starts for a seed: Archipelago's output folder, from host.yaml.
TEST(launcher_archipelago_output_folder) {
  const fs::path ap = scratch("apdir");
  CHECK(launcher::archipelago_output_dir(ap).empty());  // no output folder yet
  fs::create_directories(ap / "output");
  CHECK(launcher::archipelago_output_dir(ap) == ap / "output");  // no host.yaml: the default
  write_file(ap / "host.yaml", "general_options:\n  # Where to place output files\n  output_path: \"output\"\n");
  CHECK(launcher::archipelago_output_dir(ap) == ap / "output");
  fs::create_directories(ap / "seeds out");
  write_file(ap / "host.yaml",
             "server_options:\n  output_path: elsewhere\ngeneral_options:\r\n  output_path: 'seeds out'\r\n");
  CHECK(launcher::archipelago_output_dir(ap) == ap / "seeds out");
  const fs::path absolute = scratch("apdir_abs");
  write_file(ap / "host.yaml", "general_options:\n  output_path: " + launcher::path_utf8(absolute) + "  # mine\n");
  CHECK(launcher::archipelago_output_dir(ap) == absolute);
  write_file(ap / "host.yaml", "general_options:\n  output_path: missing\n");
  CHECK(launcher::archipelago_output_dir(ap).empty());
  CHECK(launcher::archipelago_output_dir(fs::path()).empty());
#if !defined(_WIN32) && !defined(__APPLE__)
  // Linux: $XDG_DATA_HOME/Archipelago, as Archipelago's Utils.home_path.
  const char* old = std::getenv("XDG_DATA_HOME");
  const std::string saved = old ? old : "";
  const fs::path data = scratch("xdg_data");
  setenv("XDG_DATA_HOME", launcher::path_utf8(data).c_str(), 1);
  const fs::path none = launcher::archipelago_dir();
  fs::create_directories(data / "Archipelago" / "output");
  CHECK(launcher::archipelago_dir() == data / "Archipelago");
  CHECK(launcher::archipelago_output_dir() == data / "Archipelago" / "output");
  CHECK(none != data / "Archipelago");
  if (old) setenv("XDG_DATA_HOME", saved.c_str(), 1);
  else unsetenv("XDG_DATA_HOME");
#endif
}

// A seed plays on its own card: <base>/<seed>_<slot>/memcard1.mcd. The saves an earlier
// version made for it on the player's card are copied over once; the player's card stays.
TEST(launcher_archipelago_seed_memcard) {
  const fs::path dir = scratch("apcard");
  CHECK(launcher::seed_memcard("123", "Claude", dir / "ap") == dir / "ap" / "123_Claude" / "memcard1.mcd");
  CHECK(launcher::seed_memcard("123", "a/b:c?. ", dir) == dir / "123_a_b_c_" / "memcard1.mcd");

  // The player's card: saves 1, 3 and 5, another game's file; sidecars for 3 (this seed),
  // 5 (another seed) and 7 (a save since deleted).
  const fs::path own = dir / "memcard1.mcd";
  {
    psx::MemoryCard card;
    CHECK(card.open(own, true));
    for (const char* name : {"BASCUS-94421S02-S01", "BASCUS-94421S02-S03", "BASCES-02159S02-S05", "BESLES-OTHER"})
      CHECK(card.create(name, 1));
  }
  write_file(dir / "memcard1.slot03.ap.json", "{\n \"format\": \"apso2-save\",\n \"seed\": \"S1\",\n \"slot_name\": \"Cl\\u00e9o\"\n}\n");
  write_file(dir / "memcard1.slot05.ap.json", R"({"seed": "S2", "slot_name": "Cléo"})");
  write_file(dir / "memcard1.slot07.ap.json", R"({"seed": "S1", "slot_name": "Cléo"})");
  std::ifstream before_in(own, std::ios::binary);
  const std::string before((std::istreambuf_iterator<char>(before_in)), std::istreambuf_iterator<char>());

  std::string error;
  const fs::path seed_card = launcher::seed_memcard("S1", "Cléo", dir / "ap");
  CHECK_EQ(launcher::adopt_seed_saves(seed_card, {fs::path(), dir / "missing.mcd", own}, "S1", "Cléo", &error), 1);
  {
    psx::MemoryCard card;
    CHECK(card.open(seed_card, false));
    const auto files = card.files();
    CHECK_EQ(files.size(), 1u);
    CHECK(!files.empty() && files[0].name == "BASCUS-94421S02-S03");
  }
  CHECK(fs::exists(seed_card.parent_path() / "memcard1.slot03.ap.json"));
  CHECK(!fs::exists(seed_card.parent_path() / "memcard1.slot07.ap.json"));
  {
    // [hubcore agent] The copied sidecar is stamped with the hash of its save on the seed's card
    // (the save guard): "save_hash" first, the rest as it was.
    psx::MemoryCard card;
    CHECK(card.open(seed_card, false));
    std::vector<u8> block(psx::MemoryCard::kBlockSize);
    CHECK(card.read(card.files()[0], 0, block.data(), static_cast<u32>(block.size())));
    std::ifstream in(seed_card.parent_path() / "memcard1.slot03.ap.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text == "{\n \"save_hash\": \"" + util::save_digest(block.data(), block.size()) +
                      "\",\n \"format\": \"apso2-save\",\n \"seed\": \"S1\",\n \"slot_name\": \"Cl\\u00e9o\"\n}\n");
  }
  CHECK(!fs::exists(seed_card.parent_path() / "memcard1.mcd.tmp"));
  std::ifstream after_in(own, std::ios::binary);
  const std::string after((std::istreambuf_iterator<char>(after_in)), std::istreambuf_iterator<char>());
  CHECK(after == before);  // the player's card is never changed
  CHECK(fs::exists(dir / "memcard1.slot03.ap.json"));
  // Only once: the seed's card exists now.
  CHECK_EQ(launcher::adopt_seed_saves(seed_card, {own}, "S1", "Cléo", &error), 0);
  // A seed with nothing on the player's card: no card made (the game makes a new one).
  const fs::path other = launcher::seed_memcard("S9", "Claude", dir / "ap");
  CHECK_EQ(launcher::adopt_seed_saves(other, {own}, "S9", "Claude", &error), 0);
  CHECK(!fs::exists(other));

  // [hubcore agent] An earlier version's full sidecar (version 1, no hash) for a save with data:
  // adopted, it reads as version 2 with the save's hash, and the guard lets that save load.
  const fs::path own2 = dir / "own2" / "memcard1.mcd";
  fs::create_directories(own2.parent_path());
  std::vector<u8> save(psx::MemoryCard::kBlockSize);
  for (size_t i = 0; i < save.size(); ++i) save[i] = static_cast<u8>(i * 13 + 1);
  save[0x21A] = 0x80;
  save[0x21B] = 0x0B;  // length 0xb80
  {
    psx::MemoryCard card;
    CHECK(card.open(own2, true));
    CHECK(card.create("BASCUS-94421S02-S02", 1));
    const auto f = card.find("BASCUS-94421S02-S02");
    CHECK(f.has_value() && card.write(*f, 0, save.data(), static_cast<u32>(save.size())));
  }
  ss::game::ap::SaveState st;
  st.seed = "S5";
  st.slot_name = "Claude";
  st.checked = {7, 8};
  ss::game::ap::Json j = ss::game::ap::sidecar_json(st);
  j.set("version", ss::game::ap::Json::number(static_cast<int64_t>(1)));
  write_file(own2.parent_path() / "memcard1.slot02.ap.json", j.dump());
  const fs::path seed5 = launcher::seed_memcard("S5", "Claude", dir / "ap");
  CHECK_EQ(launcher::adopt_seed_saves(seed5, {own2}, "S5", "Claude", &error), 1);
  ss::game::ap::SaveState back;
  CHECK(ss::game::ap::read_sidecar((seed5.parent_path() / "memcard1.slot02.ap.json").string(), &back, &error));
  CHECK(back.save_hash == util::save_digest(save.data(), save.size()) && back.checked.size() == 2);
  {
    std::ifstream in(seed5.parent_path() / "memcard1.slot02.ap.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("\"version\": 2") != std::string::npos);
  }
  ss::game::ap::Seed seed;
  CHECK(ss::game::ap::load_seed_text(R"({"format": "apso2", "version": 1, "seed": "S5", "slot": {"name": "Claude", "id": 1},
                                         "items": {}, "locations": []})",
                                     &seed, &error));
  ss::game::ap::start_with(seed);
  CHECK(ss::game::ap::load_allowed(launcher::path_utf8(seed5), 1, save.data(), save.size()));
  save[0x100] ^= 0xFF;
  CHECK(!ss::game::ap::load_allowed(launcher::path_utf8(seed5), 1, save.data(), save.size()));
  ss::game::ap::stop();
}

TEST(mods_install_and_remove) {
  const fs::path dir = scratch("install");
  const fs::path mods_dir = dir / "mods";
  auto zip_at = [&](const std::string& name, const std::vector<ZipFile>& files) {
    const fs::path p = dir / name;
    write_file(p, make_zip(files));
    return p;
  };
  const std::string json = manifest("shop-tweak", "\"stock.patch\"");
  // A package in one top-level folder, its patch deflated, a readme in lower case.
  const fs::path good = zip_at("good.zip", {{"shop-tweak/", "", {}},
                                            {"shop-tweak/mod.json", json, {}},
                                            {"shop-tweak/stock.patch", kFixedText, kFixedDeflate},
                                            {"shop-tweak/readme.md", "Read me.\n", {}},
                                            {"__MACOSX/shop-tweak/._mod.json", "junk", {}}});
  mods::ZipPackage pkg;
  std::string error;
  CHECK(mods::read_zip_package(good, &pkg, &error));
  CHECK(pkg.manifest.id == "shop-tweak");
  CHECK_EQ(pkg.files.size(), 3u);
  mods::Catalog c = mods::scan(mods_dir, "0.01.00");
  CHECK(!c.folder_exists);
  mods::InstallResult r;
  bool exists = false;
  CHECK(mods::install_package(pkg, c, false, &r, &error, &exists));
  CHECK(!r.replaced);
  CHECK(fs::is_regular_file(mods_dir / "shop-tweak" / "stock.patch"));
  CHECK(fs::is_regular_file(mods_dir / "shop-tweak" / "README.md"));
  c = mods::scan(mods_dir, "0.01.00");
  CHECK_EQ(c.entries.size(), 1u);
  CHECK(c.find("shop-tweak") == 0 && c.entries[0].usable());
  // Again: refused unless replacing; replacing keeps one copy and reports the old version.
  CHECK(!mods::install_package(pkg, c, false, &r, &error, &exists));
  CHECK(exists);
  pkg.manifest.version = "1.3";
  CHECK(mods::install_package(pkg, c, true, &r, &error, &exists));
  CHECK(r.replaced);
  CHECK(r.previous_version == "1.2.0");
  std::error_code ec;
  int dirs = 0;
  for (const auto& e : fs::directory_iterator(mods_dir, ec)) dirs += e.is_directory() ? 1 : 0;
  CHECK_EQ(dirs, 1);  // no temporary folders left behind

  // Archives that aren't acceptable packages.
  auto refused = [&](const fs::path& zip, const char* why) {
    mods::ZipPackage p;
    std::string e;
    CHECK(!mods::read_zip_package(zip, &p, &e));
    if (!contains(e, why)) std::fprintf(stderr, "  refusal '%s' lacks '%s'\n", e.c_str(), why);
    CHECK(contains(e, why));
  };
  refused(zip_at("exe.zip", {{"mod.json", json, {}}, {"stock.patch", "x", {}}, {"tool.exe", "MZ", {}}}), "tool.exe");
  refused(zip_at("image.zip", {{"mod.json", json, {}}, {"stock.patch", "x", {}}, {"shot.png", "PNG", {}}}), "shot.png");
  refused(zip_at("evil.zip", {{"../mod.json", json, {}}}), "unsafe path");
  refused(zip_at("nested.zip", {{"mod.json", json, {}}, {"stock.patch", "x", {}}, {"sub/x.patch", "x", {}}}),
          "subfolder");
  refused(zip_at("unlisted.zip", {{"mod.json", json, {}}, {"stock.patch", "x", {}}, {"extra.patch", "x", {}}}),
          "isn't listed");
  refused(zip_at("missing.zip", {{"mod.json", json, {}}}), "isn't in it");
  refused(zip_at("bare.zip", {{"a.patch", "x", {}}}), "no mod.json");
  refused(zip_at("binary.zip", {{"mod.json", json, {}}, {"stock.patch", std::string("a\0b", 3), {}}}), "text file");
  write_file(dir / "notzip.zip", "just text");
  refused(dir / "notzip.zip", "not a .zip");

  // Removing: only package folders inside the mods folder.
  c = mods::scan(mods_dir, "0.01.00");
  write_file(mods_dir / "loose.patch", "item 5 price 10 20\n");
  c = mods::scan(mods_dir, "0.01.00");
  CHECK(!mods::remove_package(c, c.find(mods::kLooseId), &error));
  CHECK(mods::remove_package(c, c.find("shop-tweak"), &error));
  CHECK(!fs::exists(mods_dir / "shop-tweak"));
  CHECK(fs::exists(mods_dir / "loose.patch"));
}

// The engine applies the enabled patches in the configured order: item 21's price (200 on
// the disc) as three entries change it, at a battle's start. Needs the disc and the game.
TEST(mods_game_applies_load_order) {
  const char* disc = std::getenv("SS_DISC1");
  const fs::path game = launcher::sibling_program(launcher::kGameProgram);
  if (!disc || !*disc || game.empty()) {
    std::printf("  (skipped: needs SS_DISC1 and the game built next to the tests)\n");
    return;
  }
  const fs::path home = scratch("game");
  const fs::path mods_dir = home / "mods";
  write_file(mods_dir / "10-loose.patch", "item 21 price 200 500\n");
  write_file(mods_dir / "cheap" / "mod.json", manifest("cheap", "\"p.patch\""));
  write_file(mods_dir / "cheap" / "p.patch", "item 21 price 200 300\n");
  write_file(mods_dir / "dear" / "mod.json", manifest("dear", "\"p.patch\""));
  write_file(mods_dir / "dear" / "p.patch", "item 21 price 200 400\n");
  auto run = [&](const std::string& config_text, const std::vector<std::pair<std::string, std::string>>& extra) {
    write_file(home / "shattered-stars" / "config.ini", config_text);
    // A battle start loads the item table (battle/loaders.cpp), which applies item lines.
    std::vector<std::string> argv_strings = {launcher::path_utf8(game), "--headless", "--frames", "60", "--battle", "19",
                                             "--no-memcard"};
    std::vector<const char*> argv;
    for (const std::string& a : argv_strings) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_Environment* env = SDL_CreateEnvironment(true);
    const std::string h = launcher::path_utf8(home);
    for (const auto& [k, v] : std::vector<std::pair<std::string, std::string>>{
             {"XDG_CONFIG_HOME", h}, {"APPDATA", h}, {"SDL_VIDEO_DRIVER", "offscreen"}, {"SDL_AUDIO_DRIVER", "dummy"}})
      SDL_SetEnvironmentVariable(env, k.c_str(), v.c_str(), true);
    for (const char* k : {"SS_MODS_DIR", "SS_MOD_ORDER", "SS_MEMCARD1", "SS_MEMCARD2", "SS_EVENT_SWEEP"})
      SDL_UnsetEnvironmentVariable(env, k);
    for (const auto& [k, v] : extra) SDL_SetEnvironmentVariable(env, k.c_str(), v.c_str(), true);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(argv.data()));
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    std::string output;
    if (process) {
      size_t size = 0;
      int code = -1;
      if (void* data = SDL_ReadProcess(process, &size, &code)) {
        output.assign(static_cast<const char*>(data), size);
        SDL_free(data);
      }
      CHECK_EQ(code, 0);
      SDL_DestroyProcess(process);
    }
    return output;
  };
  const std::string base = std::string("disc1=") + disc + "\nmods=" + launcher::path_utf8(mods_dir) + "\n";
  // No mod_order: the loose file alone (packages are off until enabled).
  std::string out = run(base, {});
  CHECK(contains(out, "[mods] 10-loose.patch:1: item 21 +0 0xc8 -> 0x1f4"));
  CHECK(contains(out, "cheap 1.2.0 is installed but not enabled"));
  // Both packages on: the last one wins; the earlier lines are left out and say so.
  out = run(base + "mod_order=(loose),cheap,dear\n", {});
  CHECK(contains(out, "[mods] load order: (loose) 1 file(s), cheap 1.2.0, dear 1.2.0"));
  CHECK(contains(out, "[mods] dear/p.patch:1: item 21 +0 0xc8 -> 0x190"));
  CHECK(contains(out, "[mods] cheap/p.patch:1: overridden by dear/p.patch:1"));
  CHECK(contains(out, "[mods] 10-loose.patch:1: overridden by dear/p.patch:1"));
  CHECK(contains(out, "[mods] conflict: item 21 price"));
  // Swapped, loose off.
  out = run(base + "mod_order=-(loose),dear,cheap\n", {});
  CHECK(contains(out, "[mods] cheap/p.patch:1: item 21 +0 0xc8 -> 0x12c"));
  CHECK(!contains(out, "10-loose.patch:1: item"));
  // A test run's folder override ignores config.ini's order (the defaults apply)...
  out = run(base + "mod_order=-(loose),dear\n", {{"SS_MODS_DIR", launcher::path_utf8(mods_dir)}});
  CHECK(contains(out, "[mods] 10-loose.patch:1: item 21 +0 0xc8 -> 0x1f4"));
  // ... unless SS_MOD_ORDER gives one.
  out = run(base, {{"SS_MODS_DIR", launcher::path_utf8(mods_dir)}, {"SS_MOD_ORDER", "dear"}});
  CHECK(contains(out, "[mods] dear/p.patch:1: item 21 +0 0xc8 -> 0x190"));
  // The game never writes mod_order.
  std::ifstream in(home / "shattered-stars" / "config.ini");
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(text == base);
}
