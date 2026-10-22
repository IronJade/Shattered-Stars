#include "game/field/mods.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

struct ScriptPatch {
  int map = 0;
  u32 word = 0;
  u32 expected = 0;
  u32 value = 0;
  std::string where;  // file:line, for the log
};

bool parse_number(const std::string& s, u32* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  const long long v = std::strtoll(s.c_str(), &end, 0);
  if (end == nullptr || *end != 0) return false;
  *out = static_cast<u32>(v);
  return true;
}

const std::vector<ScriptPatch>& patches() {
  static std::vector<ScriptPatch> list;
  static bool loaded = false;
  if (loaded) return list;
  loaded = true;
  auto& rt = runtime::Runtime::get();
  std::string dir = rt.mods_dir;
  if (const char* env = std::getenv("SS_MODS_DIR"); env && *env) dir = env;
  std::error_code ec;
  if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return list;
  std::vector<std::filesystem::path> files;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec))
    if (e.is_regular_file() && e.path().extension() == ".patch") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  for (const auto& path : files) {
    std::ifstream in(path);
    int line_no = 0;
    for (std::string line; std::getline(in, line);) {
      ++line_no;
      if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
      std::istringstream fields(line);
      std::string kind, map, word, expected, value, extra;
      if (!(fields >> kind)) continue;
      const std::string where = path.filename().string() + ":" + std::to_string(line_no);
      ScriptPatch p;
      u32 m = 0;
      if (kind != "script" || !(fields >> map >> word >> expected >> value) || (fields >> extra) ||
          !parse_number(map, &m) || !parse_number(word, &p.word) || !parse_number(expected, &p.expected) ||
          !parse_number(value, &p.value)) {
        std::fprintf(stderr, "[mods] %s: not understood: %s\n", where.c_str(), line.c_str());
        continue;
      }
      p.map = static_cast<int>(m);
      p.where = where;
      list.push_back(p);
    }
  }
  std::fprintf(stderr, "[mods] %zu script patch(es) from %s\n", list.size(), dir.c_str());
  return list;
}

}  // namespace

int mods_apply_script(int map, std::vector<u8>& script) {
  int changed = 0;
  for (const ScriptPatch& p : patches()) {
    if (p.map != map) continue;
    const size_t at = 0x1C + static_cast<size_t>(p.word) * 4;
    if (at + 4 > script.size()) {
      runtime::Runtime::get().log_once("mods:" + p.where, "[mods] " + p.where + ": word past the end of map " +
                                                              std::to_string(map) + "'s script; skipped");
      continue;
    }
    u32 cur;
    std::memcpy(&cur, script.data() + at, 4);
    if (cur != p.expected) {
      char msg[160];
      std::snprintf(msg, sizeof msg, "[mods] %s: map %d word %u holds %#x, not %#x; skipped", p.where.c_str(), map,
                    p.word, cur, p.expected);
      runtime::Runtime::get().log_once("mods:" + p.where, msg);
      continue;
    }
    std::memcpy(script.data() + at, &p.value, 4);
    ++changed;
  }
  if (changed != 0) {
    char msg[64];
    std::snprintf(msg, sizeof msg, "mods: %d script word(s) patched on map %d", changed, map);
    runtime::Runtime::get().trace(msg);
  }
  return changed;
}

}  // namespace ss::game::field
