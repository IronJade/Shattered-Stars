#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "mods/patch.h"

// Mod packages and their load order (port feature; docs/port/notes/mods.md). Shared by the game
// (game/field/mods.cpp applies the enabled patches in order) and setup's Mods page.
//
// The mods folder holds loose *.patch files (shown as one entry, "Loose patches") and
// packages: direct subfolders with a mod.json manifest, one or more *.patch files listed
// in it, and an optional README.md. config.ini's mod_order key lists the entries in load
// order ("(loose),arlia-chest-tweak,-hearn-shop-stock": '-' = installed but disabled);
// unlisted entries get their default (loose patches on, at the front; packages off, at
// the end). With no packages and no mod_order key, exactly the loose patches load, in
// name order, as before packages existed.
namespace ss::mods {

inline constexpr const char* kLooseId = "(loose)";
inline constexpr int kManifestFormat = 1;
inline constexpr size_t kMaxPatchFiles = 32;
inline constexpr size_t kMaxPatchBytes = 256u << 10;
inline constexpr size_t kMaxReadmeBytes = 64u << 10;
inline constexpr size_t kMaxManifestBytes = 16u << 10;
inline constexpr size_t kMaxPackageBytes = 1u << 20;

struct DeclaredRisk {
  std::string rule, reason;
  std::vector<std::string> lines;
};

struct Manifest {
  int format = 0;
  std::string id, name, version, author, description, min_port_version, homepage;
  std::vector<std::string> tags, patches;
  std::vector<DeclaredRisk> declared_risks;
};

// mod.json's text; false and the reason when it can't be used. 'warnings' (may be null)
// gets what is odd but harmless (unknown keys and the like).
bool parse_manifest(const std::string& text, Manifest* out, std::string* error,
                    std::vector<std::string>* warnings = nullptr);
bool valid_id(const std::string& id);
bool valid_version(const std::string& v);  // 1-4 dot-separated numbers
// Dot-separated numbers compared one by one ("0.01.00" == "0.1", "0.2" > "0.01.05").
int compare_versions(const std::string& a, const std::string& b);

struct Entry {
  bool loose = false;
  std::string id;                            // kLooseId for the loose patches
  std::filesystem::path folder;              // the package's folder (the mods folder for loose)
  Manifest manifest;                         // packages
  std::vector<std::filesystem::path> files;  // the patch files in load order
  std::string error;                         // why it can't be loaded; empty: it can
  std::vector<std::string> warnings;
  std::string readme;     // README.md's text (cut at kMaxReadmeBytes)
  int counts[4] = {};     // parsed lines by Kind
  int bad_lines = 0;      // lines the game won't understand
  bool usable() const { return error.empty(); }
  std::string name() const;  // the manifest's name, or "Loose patches"
};

struct Catalog {
  std::filesystem::path folder;
  bool folder_exists = false;
  std::vector<Entry> entries;  // the loose entry first (when there are loose files), then packages by folder name
  int find(const std::string& id) const;  // index, or -1
  bool has_packages() const;
};

// Scans 'folder'; 'port_version' (SS_VERSION) for the manifests' min_port_version.
Catalog scan(const std::filesystem::path& folder, const std::string& port_version);

struct Slot {
  int entry = 0;  // index into Catalog::entries
  bool enabled = false;
  bool operator==(const Slot&) const = default;
};
// The load order from config.ini's mod_order (every installed entry exactly once).
std::vector<Slot> resolve_order(const Catalog& c, const std::string& mod_order);
// The mod_order text for an order (every entry, '-' for disabled ones).
std::string format_order(const Catalog& c, const std::vector<Slot>& order);

// A parsed patch line with where it came from.
struct Line {
  PatchLine patch;
  std::string label;  // "file:line" (loose), "id/file:line" (package), "seed:N"
  int source = -1;    // the entry's index in the Catalog; -1 for an Archipelago seed's lines
};
struct BadLine {
  std::string label, text;
};
// One raw line: comment stripped, blank lines ignored, the rest parsed into 'lines' (or
// 'bad' when not understood). Returns true for a parsed line.
bool add_line(std::string text, const std::string& label, int source, std::vector<Line>* lines,
              std::vector<BadLine>* bad);
// The enabled, usable entries' lines in load order.
void read_lines(const Catalog& c, const std::vector<Slot>& order, std::vector<Line>* lines, std::vector<BadLine>* bad);

// How two lines at the same place relate (the first earlier in the load order).
enum class Relation {
  kSame,      // the same change: nothing to do for the second
  kBuildsOn,  // the second expects the first's value: both apply
  kOverride,  // a conflict: the later one wins, the earlier one is left out
  kMismatch,  // a conflict: different EXPECTED values, at most one matches the disc
  kOverlap,   // a conflict: overlapping record bytes
  kSeedWins,  // a mod line where the Archipelago seed writes: the seed wins
  kWithin,    // two lines of one entry: the guard decides (the first wins)
};
struct Conflict {
  Relation relation = Relation::kSame;
  size_t first = 0, second = 0;  // indices into the lines
  bool is_warning() const { return relation != Relation::kSame && relation != Relation::kBuildsOn; }
};
struct Analysis {
  std::vector<Conflict> conflicts;
  std::vector<int> overridden_by;  // per line: the later line that replaces it, or -1
  std::vector<int> redundant_with;  // per line: an earlier line making the same change, or -1
  std::vector<std::vector<int>> same_place;  // per line: the other lines at its place, in order
};
Analysis analyse(const std::vector<Line>& lines);
// The conflict in a sentence; 'name' gives a source's display name (-1: the seed).
std::string explain(const Conflict& c, const std::vector<Line>& lines, const std::function<std::string(int)>& name);

// Installing and removing packages (setup's Mods page).
struct ZipPackage {
  Manifest manifest;
  std::vector<std::pair<std::string, std::vector<u8>>> files;  // name in the package -> bytes
};
// Reads a .zip holding one package (at its root or in one top-level folder) and checks it:
// the allowlist (mod.json, the listed *.patch files, README.md), the sizes, the manifest.
bool read_zip_package(const std::filesystem::path& zip, ZipPackage* out, std::string* error);
struct InstallResult {
  std::string id, version, previous_version;
  std::filesystem::path folder;
  bool replaced = false;
};
// Installs into c.folder/<id>/ through a temporary folder. An installed package with the
// same id is replaced only when 'replace' is set (else false, with 'exists' set).
bool install_package(const ZipPackage& pkg, const Catalog& c, bool replace, InstallResult* out, std::string* error,
                     bool* exists = nullptr);
// Deletes a package's folder (only a package folder directly inside the mods folder).
bool remove_package(const Catalog& c, int entry, std::string* error);

}  // namespace ss::mods
