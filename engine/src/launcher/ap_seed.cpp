#include "launcher/ap_seed.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string_view>
#include <fstream>
#include <iterator>
#include <system_error>

#include "launcher/config.h"
#include "mods/zip.h"
#include "psx/memory_card.h"
#include "runtime/region.h"

#ifdef _WIN32
#include <cwchar>
#endif

namespace ss::launcher {
namespace {

namespace fs = std::filesystem;

// An environment variable as a path; on Windows read as UTF-16 so any user name works.
fs::path env_path(const char* name) {
#ifdef _WIN32
  std::wstring wide(name, name + std::char_traits<char>::length(name));
  if (const wchar_t* v = _wgetenv(wide.c_str()); v && *v) return fs::path(v);
  return {};
#else
  if (const char* v = std::getenv(name); v && *v) return fs::path(v);
  return {};
#endif
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

bool ends_with(const std::string& s, const std::string& tail) {
  return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// The file name part of an archive entry ('/' separated).
std::string base_name(const std::string& entry) {
  const auto slash = entry.find_last_of('/');
  return slash == std::string::npos ? entry : entry.substr(slash + 1);
}

bool fail(std::string* error, const std::string& why) {
  if (error) *error = why;
  return false;
}

// A file name part from a seed id or a slot name: any character a file system refuses
// becomes '_' (UTF-8 stays as it is).
std::string file_part(const std::string& s) {
  std::string out;
  for (const char c : s) {
    const auto u = static_cast<unsigned char>(c);
    out += u < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos ? '_' : c;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  return out.empty() ? std::string("_") : out;
}

// A string field of a sidecar (the game's own JSON: game/ap/json.cpp escapes '"', '\\' and
// control characters; \uXXXX decoded for safety). False when the key is missing.
bool json_string_field(const std::string& text, const std::string& key, std::string* out) {
  const std::string quoted = "\"" + key + "\"";
  size_t p = text.find(quoted);
  while (p != std::string::npos) {
    size_t q = text.find_first_not_of(" \t\r\n", p + quoted.size());
    if (q != std::string::npos && text[q] == ':') {
      q = text.find_first_not_of(" \t\r\n", q + 1);
      if (q == std::string::npos || text[q] != '"') return false;
      out->clear();
      for (size_t i = q + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') return true;
        if (c != '\\') {
          *out += c;
          continue;
        }
        if (++i >= text.size()) return false;
        switch (text[i]) {
          case 'n': *out += '\n'; break;
          case 'r': *out += '\r'; break;
          case 't': *out += '\t'; break;
          case 'b': *out += '\b'; break;
          case 'f': *out += '\f'; break;
          case 'u': {
            if (i + 4 >= text.size()) return false;
            u32 cp = static_cast<u32>(std::stoul(text.substr(i + 1, 4), nullptr, 16));
            i += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < text.size() && text[i + 1] == '\\' && text[i + 2] == 'u') {
              const u32 lo = static_cast<u32>(std::stoul(text.substr(i + 3, 4), nullptr, 16));
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              i += 6;
            }
            if (cp < 0x80) {
              *out += static_cast<char>(cp);
            } else if (cp < 0x800) {
              *out += static_cast<char>(0xC0 | cp >> 6);
              *out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
              *out += static_cast<char>(0xE0 | cp >> 12);
              *out += static_cast<char>(0x80 | (cp >> 6 & 0x3F));
              *out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
              *out += static_cast<char>(0xF0 | cp >> 18);
              *out += static_cast<char>(0x80 | (cp >> 12 & 0x3F));
              *out += static_cast<char>(0x80 | (cp >> 6 & 0x3F));
              *out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            break;
          }
          default: *out += text[i]; break;  // '"', '\\', '/'
        }
      }
      return false;
    }
    p = text.find(quoted, p + 1);
  }
  return false;
}

// The save number (1-15) of the game's save file "<product code>S02-Snn", any release; 0
// for anything else on a card.
int game_save_number(const std::string& name) {
  for (const region::Profile& pr : region::profiles()) {
    const std::string prefix = std::string(pr.product) + "S02-S";
    if (name.size() == prefix.size() + 2 && name.rfind(prefix, 0) == 0 && std::isdigit(static_cast<unsigned char>(name[prefix.size()])) &&
        std::isdigit(static_cast<unsigned char>(name[prefix.size() + 1])))
      return (name[prefix.size()] - '0') * 10 + (name[prefix.size() + 1] - '0');
  }
  return 0;
}

// A card's sidecar for save number n (game/ap/ap.cpp sidecar_path): "<card without its
// extension>.slotNN.ap.json".
fs::path sidecar_of(const fs::path& card, int n) {
  fs::path p = card;
  p.replace_extension();
  char tail[32];
  std::snprintf(tail, sizeof tail, ".slot%02d.ap.json", n);
  return fs::path(p.native() + fs::path(tail).native());
}

// "AP_1_P1_Claude.apso2, AP_1_P2_Rena.apso2"
std::string name_list(const std::vector<std::string>& names) {
  std::string s;
  for (const std::string& n : names) s += (s.empty() ? "" : ", ") + base_name(n);
  return s;
}

}  // namespace

fs::path archipelago_dir() {
  std::vector<fs::path> candidates;
#if defined(_WIN32)
  if (fs::path data = env_path("ProgramData"); !data.empty()) candidates.push_back(data / "Archipelago");
#elif defined(__APPLE__)
  if (fs::path home = env_path("HOME"); !home.empty())
    candidates.push_back(home / "Library" / "Application Support" / "Archipelago");
#else
  const fs::path home = env_path("HOME");
  if (fs::path xdg = env_path("XDG_DATA_HOME"); !xdg.empty()) candidates.push_back(xdg / "Archipelago");
  else if (!home.empty()) candidates.push_back(home / ".local" / "share" / "Archipelago");
  if (!home.empty()) candidates.push_back(home / "Archipelago");  // before Archipelago 0.4
#endif
  std::error_code ec;
  for (const fs::path& d : candidates)
    if (fs::is_directory(d, ec)) return d;
  return {};
}

fs::path archipelago_output_dir(const fs::path& ap_dir) {
  if (ap_dir.empty()) return {};
  // host.yaml's general_options.output_path (a plain scalar, maybe quoted); relative to
  // the Archipelago folder, as Utils.user_path takes it.
  std::string output = "output";
  std::ifstream in(ap_dir / "host.yaml", std::ios::binary);
  std::string line, section;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    if (line[0] != ' ' && line[0] != '\t') {  // a top-level key
      section = t.substr(0, t.find(':'));
      continue;
    }
    if (section != "general_options" || t.rfind("output_path:", 0) != 0) continue;
    std::string v = trim(t.substr(12));
    if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
    else if (const auto hash = v.find(" #"); hash != std::string::npos) v = trim(v.substr(0, hash));
    if (!v.empty() && v != "null" && v != "~") output = v;
    break;
  }
  const fs::path p = utf8_path(output);
  const fs::path dir = p.is_absolute() ? p : ap_dir / p;
  std::error_code ec;
  return fs::is_directory(dir, ec) ? dir : fs::path();
}

fs::path archipelago_output_dir() { return archipelago_output_dir(archipelago_dir()); }

bool is_seed_archive(const fs::path& path) { return lower(path_utf8(path.extension())) == ".zip"; }

bool list_archive_seeds(const fs::path& zip, std::vector<std::string>* names, std::string* error) {
  names->clear();
  mods::ZipReader z;
  std::string why;
  if (!z.open_file(zip, &why)) return fail(error, "the archive can't be read: " + why);
  for (const mods::ZipEntry& e : z.entries())
    if (!e.directory && ends_with(lower(e.name), ".apso2")) names->push_back(e.name);
  return true;
}

bool read_seed_text(const fs::path& path, const std::string& entry, std::string* text, std::string* chosen,
                    std::string* error) {
  if (chosen) chosen->clear();
  if (!is_seed_archive(path)) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(error, "cannot open " + path_utf8(path));
    text->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
  }
  mods::ZipReader z;
  std::string why;
  if (!z.open_file(path, &why)) return fail(error, "the archive can't be read: " + why);
  std::vector<const mods::ZipEntry*> seeds;
  for (const mods::ZipEntry& e : z.entries())
    if (!e.directory && ends_with(lower(e.name), ".apso2")) seeds.push_back(&e);
  std::vector<std::string> names;
  for (const mods::ZipEntry* e : seeds) names.push_back(e->name);
  const mods::ZipEntry* pick = nullptr;
  if (seeds.empty()) {
    return fail(error, "the archive holds no Star Ocean seed (.apso2): was Star Ocean: The Second Story one of "
                       "the games generated?");
  } else if (!entry.empty()) {
    for (const mods::ZipEntry* e : seeds)
      if (e->name == entry) pick = e;
    if (pick == nullptr)
      for (const mods::ZipEntry* e : seeds)
        if (base_name(e->name) == base_name(entry)) pick = e;
    if (pick == nullptr)
      return fail(error, "the archive has no seed " + base_name(entry) + " (it holds " + name_list(names) + ")");
  } else if (seeds.size() == 1) {
    pick = seeds[0];
  } else {
    return fail(error, "the archive holds " + std::to_string(seeds.size()) + " Star Ocean seeds (" + name_list(names) +
                           "): choose yours on setup's Archipelago page (or with --ap-seed-entry NAME)");
  }
  std::vector<u8> bytes;
  if (!z.read(*pick, &bytes, &why, 64u << 20)) return fail(error, "the seed can't be read from the archive: " + why);
  text->assign(bytes.begin(), bytes.end());
  if (chosen) *chosen = pick->name;
  return true;
}

bool seed_file(const fs::path& path, const std::string& entry, fs::path* out, std::string* error,
               const fs::path& cache) {
  if (!is_seed_archive(path)) {
    *out = path;
    return true;
  }
  std::string text, chosen;
  if (!read_seed_text(path, entry, &text, &chosen, error)) return false;
  const fs::path dir = cache.empty() ? config_dir() / "archipelago" : cache;
  const fs::path dest = dir / utf8_path(base_name(chosen));
  std::error_code ec;
  {
    std::ifstream old(dest, std::ios::binary);
    if (old && std::string(std::istreambuf_iterator<char>(old), std::istreambuf_iterator<char>()) == text) {
      *out = dest;
      return true;
    }
  }
  fs::create_directories(dir, ec);
  fs::path tmp = dest;
  tmp += ".tmp";
  {
    std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
    if (!o || !o.write(text.data(), static_cast<std::streamsize>(text.size())))
      return fail(error, "the seed can't be copied out of the archive to " + path_utf8(dir));
  }
  fs::rename(tmp, dest, ec);
  if (ec) {  // some file systems refuse to replace: remove, then move (as Config::save)
    std::error_code ec2;
    fs::remove(dest, ec2);
    fs::rename(tmp, dest, ec);
  }
  if (ec) {
    fs::remove(tmp, ec);
    return fail(error, "the seed can't be copied out of the archive to " + path_utf8(dir));
  }
  *out = dest;
  return true;
}

fs::path seed_memcard(const std::string& seed, const std::string& slot_name, const fs::path& base) {
  const fs::path dir = base.empty() ? config_dir() / "archipelago" : base;
  return dir / utf8_path(file_part(seed) + "_" + file_part(slot_name)) / "memcard1.mcd";
}

int adopt_seed_saves(const fs::path& seed_card, const std::vector<fs::path>& own_cards, const std::string& seed,
                     const std::string& slot_name, std::string* error) {
  std::error_code ec;
  if (fs::exists(seed_card, ec)) return 0;
  for (const fs::path& card : own_cards) {
    if (card.empty() || !fs::is_regular_file(card, ec) || fs::file_size(card, ec) != psx::MemoryCard::kSize) continue;
    // This seed's saves on the card: their sidecars name it.
    std::set<int> numbers;
    for (int n = 1; n <= 15; ++n) {
      std::ifstream in(sidecar_of(card, n), std::ios::binary);
      if (!in) continue;
      const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      std::string s, slot;
      if (json_string_field(text, "seed", &s) && json_string_field(text, "slot_name", &slot) && s == seed &&
          slot == slot_name)
        numbers.insert(n);
    }
    if (numbers.empty()) continue;
    // A copy of the card with only those saves on it (a temporary file first: a failure
    // leaves no seed card behind, and the game makes a new one).
    fs::create_directories(seed_card.parent_path(), ec);
    fs::path tmp = seed_card;
    tmp += ".tmp";
    fs::copy_file(card, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      fail(error, "can't copy " + path_utf8(card) + ": " + ec.message());
      return -1;
    }
    std::set<int> kept;
    {
      psx::MemoryCard copy;
      if (!copy.open(tmp, false) || !copy.formatted()) {
        fs::remove(tmp, ec);
        continue;  // not a card the game reads: nothing to take over from it
      }
      for (const psx::MemoryCard::File& f : copy.files()) {
        const int n = game_save_number(f.name);
        if (n != 0 && numbers.count(n)) kept.insert(n);
        else copy.erase(f.name);
      }
      if (kept.empty()) {
        fs::remove(tmp, ec);
        continue;  // sidecars without their saves (the saves were deleted since)
      }
    }
    fs::rename(tmp, seed_card, ec);
    if (ec) {
      fs::remove(tmp, ec);
      fail(error, "can't write " + path_utf8(seed_card));
      return -1;
    }
    int adopted = 0;
    for (const int n : kept) {
      fs::copy_file(sidecar_of(card, n), sidecar_of(seed_card, n), fs::copy_options::overwrite_existing, ec);
      if (!ec) ++adopted;
    }
    return adopted;
  }
  return 0;
}

}  // namespace ss::launcher
