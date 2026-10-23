#include "mods/catalog.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>

#include "mods/zip.h"

namespace ss::mods {
namespace {

namespace fs = std::filesystem;

std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

std::string lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::string utf8(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

fs::path path_of(const std::string& s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

// A file's text, at most 'limit' bytes (false when it can't be read or is longer).
bool read_file(const fs::path& path, size_t limit, std::string* out, bool cut = false) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out->clear();
  char buf[4096];
  while (in.read(buf, sizeof buf) || in.gcount() > 0) {
    out->append(buf, static_cast<size_t>(in.gcount()));
    if (out->size() > limit) {
      if (!cut) return false;
      out->resize(limit);
      return true;
    }
  }
  return true;
}

// ---- a small JSON reader for mod.json ----------------------------------------------------

struct JValue {
  enum class T { kNull, kBool, kNumber, kString, kArray, kObject } t = T::kNull;
  bool b = false;
  double num = 0;
  bool integral = false;
  std::string s;
  std::vector<JValue> a;
  std::vector<std::pair<std::string, JValue>> o;
  const JValue* get(const std::string& key) const {
    for (auto it = o.rbegin(); it != o.rend(); ++it)
      if (it->first == key) return &it->second;
    return nullptr;
  }
};

class JParser {
 public:
  explicit JParser(const std::string& text) : s_(text) {}
  bool parse(JValue* out, std::string* error) {
    if (s_.compare(0, 3, "\xEF\xBB\xBF") == 0) p_ = 3;
    ws();
    bool ok = value(out, 0);
    ws();
    if (ok && p_ != s_.size()) ok = fail("unexpected text after the end");
    if (!ok && error) *error = error_;
    return ok;
  }

 private:
  const std::string& s_;
  size_t p_ = 0;
  std::string error_;

  bool fail(const char* why) {
    if (error_.empty()) {
      const int line = 1 + static_cast<int>(std::count(s_.begin(), s_.begin() + static_cast<std::ptrdiff_t>(std::min(p_, s_.size())), '\n'));
      error_ = std::string(why) + " on line " + std::to_string(line);
    }
    return false;
  }
  void ws() {
    while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r')) ++p_;
  }
  bool literal(const char* word) {
    const size_t n = std::char_traits<char>::length(word);
    if (s_.compare(p_, n, word) != 0) return fail("unexpected text");
    p_ += n;
    return true;
  }
  static void put_utf8(std::string& out, u32 c) {
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  bool hex4(u32* out) {
    if (p_ + 4 > s_.size()) return fail("a broken \\u escape");
    u32 v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = s_[p_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<u32>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<u32>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<u32>(c - 'A' + 10);
      else return fail("a broken \\u escape");
    }
    *out = v;
    return true;
  }
  bool string(std::string* out) {
    ++p_;  // the opening quote
    out->clear();
    while (p_ < s_.size()) {
      const char c = s_[p_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return fail("a control character inside a string");
      if (c != '\\') {
        *out += c;
        continue;
      }
      if (p_ >= s_.size()) break;
      const char e = s_[p_++];
      switch (e) {
        case '"': *out += '"'; break;
        case '\\': *out += '\\'; break;
        case '/': *out += '/'; break;
        case 'b': *out += '\b'; break;
        case 'f': *out += '\f'; break;
        case 'n': *out += '\n'; break;
        case 'r': *out += '\r'; break;
        case 't': *out += '\t'; break;
        case 'u': {
          u32 c1 = 0;
          if (!hex4(&c1)) return false;
          if (c1 >= 0xD800 && c1 < 0xDC00 && s_.compare(p_, 2, "\\u") == 0) {
            p_ += 2;
            u32 c2 = 0;
            if (!hex4(&c2)) return false;
            if (c2 < 0xDC00 || c2 >= 0xE000) return fail("a broken surrogate pair");
            c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
          }
          put_utf8(*out, c1);
          break;
        }
        default: return fail("an unknown escape in a string");
      }
    }
    return fail("a string without its closing quote");
  }
  bool value(JValue* v, int depth) {
    if (depth > 32) return fail("nested too deeply");
    if (p_ >= s_.size()) return fail("the text ends early");
    const char c = s_[p_];
    if (c == '{') {
      v->t = JValue::T::kObject;
      ++p_;
      ws();
      if (p_ < s_.size() && s_[p_] == '}') {
        ++p_;
        return true;
      }
      for (;;) {
        ws();
        if (p_ >= s_.size() || s_[p_] != '"') return fail("expected a key in quotes");
        std::string key;
        if (!string(&key)) return false;
        ws();
        if (p_ >= s_.size() || s_[p_] != ':') return fail("expected ':' after a key");
        ++p_;
        ws();
        JValue item;
        if (!value(&item, depth + 1)) return false;
        v->o.emplace_back(std::move(key), std::move(item));
        ws();
        if (p_ < s_.size() && s_[p_] == ',') {
          ++p_;
          continue;
        }
        if (p_ < s_.size() && s_[p_] == '}') {
          ++p_;
          return true;
        }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      v->t = JValue::T::kArray;
      ++p_;
      ws();
      if (p_ < s_.size() && s_[p_] == ']') {
        ++p_;
        return true;
      }
      for (;;) {
        ws();
        JValue item;
        if (!value(&item, depth + 1)) return false;
        v->a.push_back(std::move(item));
        ws();
        if (p_ < s_.size() && s_[p_] == ',') {
          ++p_;
          continue;
        }
        if (p_ < s_.size() && s_[p_] == ']') {
          ++p_;
          return true;
        }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      v->t = JValue::T::kString;
      return string(&v->s);
    }
    if (c == 't' || c == 'f') {
      v->t = JValue::T::kBool;
      v->b = c == 't';
      return literal(v->b ? "true" : "false");
    }
    if (c == 'n') {
      v->t = JValue::T::kNull;
      return literal("null");
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      const size_t start = p_;
      if (s_[p_] == '-') ++p_;
      bool integral = true;
      while (p_ < s_.size() && ((s_[p_] >= '0' && s_[p_] <= '9') || s_[p_] == '.' || s_[p_] == 'e' || s_[p_] == 'E' ||
                                s_[p_] == '+' || s_[p_] == '-')) {
        if (s_[p_] == '.' || s_[p_] == 'e' || s_[p_] == 'E') integral = false;
        ++p_;
      }
      const std::string text = s_.substr(start, p_ - start);
      char* end = nullptr;
      v->num = std::strtod(text.c_str(), &end);
      if (end == nullptr || *end != 0) return fail("a malformed number");
      v->t = JValue::T::kNumber;
      v->integral = integral;
      return true;
    }
    return fail("unexpected text");
  }
};

bool is_patch_name(const std::string& name) {
  return name.size() > 6 && name.compare(name.size() - 6, 6, ".patch") == 0 && name[0] != '.' &&
         name.find_first_of("/\\:") == std::string::npos;
}

// The lines of an entry's files, counted by kind (for the info panel).
void count_lines(Entry* e) {
  std::vector<Line> lines;
  std::vector<BadLine> bad;
  for (const fs::path& file : e->files) {
    std::ifstream in(file);
    for (std::string text; std::getline(in, text);) add_line(text, {}, 0, &lines, &bad);
  }
  for (const Line& l : lines) ++e->counts[static_cast<int>(l.patch.kind)];
  e->bad_lines = static_cast<int>(bad.size());
}

}  // namespace

// ---- manifests and versions ------------------------------------------------------------

bool valid_id(const std::string& id) {
  if (id.empty() || id.size() > 48) return false;
  for (size_t i = 0; i < id.size(); ++i) {
    const char c = id[i];
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!alnum && (i == 0 || (c != '-' && c != '_' && c != '.'))) return false;
  }
  return true;
}

bool valid_version(const std::string& v) {
  int parts = 0;
  size_t i = 0;
  while (i < v.size()) {
    const size_t start = i;
    while (i < v.size() && v[i] >= '0' && v[i] <= '9') ++i;
    if (i == start || i - start > 9) return false;
    ++parts;
    if (i == v.size()) break;
    if (v[i] != '.' || i + 1 == v.size()) return false;
    ++i;
  }
  return parts >= 1 && parts <= 4;
}

int compare_versions(const std::string& a, const std::string& b) {
  auto parts = [](const std::string& s) {
    std::vector<long> out;
    std::stringstream in(s);
    for (std::string p; std::getline(in, p, '.');) out.push_back(std::strtol(p.c_str(), nullptr, 10));
    return out;
  };
  const std::vector<long> x = parts(a), y = parts(b);
  for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
    const long u = i < x.size() ? x[i] : 0, v = i < y.size() ? y[i] : 0;
    if (u != v) return u < v ? -1 : 1;
  }
  return 0;
}

bool parse_manifest(const std::string& text, Manifest* out, std::string* error, std::vector<std::string>* warnings) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  auto warn = [&](const std::string& why) {
    if (warnings) warnings->push_back(why);
  };
  JValue root;
  std::string why;
  if (!JParser(text).parse(&root, &why)) return fail("mod.json isn't valid JSON: " + why);
  if (root.t != JValue::T::kObject) return fail("mod.json must hold a JSON object");
  Manifest m;
  auto str = [&](const char* key, std::string* dst, bool required) {
    const JValue* v = root.get(key);
    if (v == nullptr) {
      if (required) warn(std::string("mod.json has no \"") + key + "\"");
      return true;
    }
    if (v->t != JValue::T::kString) {
      if (error) *error = std::string("mod.json's \"") + key + "\" must be text";
      return false;
    }
    *dst = v->s;
    return true;
  };
  const JValue* format = root.get("format");
  if (format == nullptr || format->t != JValue::T::kNumber || !format->integral)
    return fail("mod.json has no \"format\" number (1 for this layout)");
  m.format = static_cast<int>(format->num);
  if (m.format > kManifestFormat)
    return fail("mod.json is in format " + std::to_string(m.format) + ", made for a newer version of the port");
  if (m.format < 1) return fail("mod.json's \"format\" must be 1");
  if (!str("id", &m.id, false) || !str("name", &m.name, false) || !str("version", &m.version, false) ||
      !str("author", &m.author, true) || !str("description", &m.description, true) ||
      !str("min_port_version", &m.min_port_version, true) || !str("homepage", &m.homepage, false))
    return false;
  if (m.id.empty()) return fail("mod.json has no \"id\"");
  if (!valid_id(m.id))
    return fail("mod.json's id \"" + m.id + "\" isn't valid (up to 48 lower-case letters, digits, '-', '_' or '.')");
  if (m.name.empty()) return fail("mod.json has no \"name\"");
  if (!valid_version(m.version)) return fail("mod.json's \"version\" must be numbers with dots, like 1.0.0");
  if (!m.min_port_version.empty() && !valid_version(m.min_port_version))
    return fail("mod.json's \"min_port_version\" must be numbers with dots, like 0.01.00");
  if (const JValue* tags = root.get("tags")) {
    if (tags->t != JValue::T::kArray) warn("mod.json's \"tags\" isn't a list (ignored)");
    else
      for (const JValue& t : tags->a)
        if (t.t == JValue::T::kString) m.tags.push_back(t.s);
  }
  const JValue* patches = root.get("patches");
  if (patches == nullptr || patches->t != JValue::T::kArray || patches->a.empty())
    return fail("mod.json has no \"patches\" list naming its .patch files");
  if (patches->a.size() > kMaxPatchFiles) return fail("mod.json lists more than 32 patch files");
  std::set<std::string> seen;
  for (const JValue& p : patches->a) {
    if (p.t != JValue::T::kString || !is_patch_name(p.s))
      return fail("mod.json's \"patches\" must list file names ending in .patch (no folders)");
    if (!seen.insert(lower(p.s)).second) return fail("mod.json lists " + p.s + " twice");
    m.patches.push_back(p.s);
  }
  if (const JValue* risks = root.get("declared_risks")) {
    if (risks->t != JValue::T::kArray) {
      warn("mod.json's \"declared_risks\" isn't a list (ignored)");
    } else {
      for (const JValue& r : risks->a) {
        const JValue* rule = r.get("rule");
        const JValue* reason = r.get("reason");
        if (r.t != JValue::T::kObject || rule == nullptr || rule->t != JValue::T::kString || reason == nullptr ||
            reason->t != JValue::T::kString) {
          warn("a \"declared_risks\" entry needs a \"rule\" and a \"reason\" (ignored)");
          continue;
        }
        DeclaredRisk d{rule->s, reason->s, {}};
        if (const JValue* lines = r.get("lines"); lines && lines->t == JValue::T::kArray)
          for (const JValue& l : lines->a)
            if (l.t == JValue::T::kString) d.lines.push_back(l.s);
        m.declared_risks.push_back(std::move(d));
      }
    }
  }
  static const std::set<std::string> known = {"format", "id",   "name",     "version",  "author",        "description",
                                              "min_port_version", "tags", "homepage", "patches", "declared_risks"};
  for (const auto& [key, v] : root.o)
    if (!known.count(key)) warn("mod.json has an unknown key \"" + key + "\" (ignored)");
  *out = std::move(m);
  return true;
}

// ---- scanning ----------------------------------------------------------------------------

std::string Entry::name() const { return loose ? "Loose patches" : (manifest.name.empty() ? id : manifest.name); }

int Catalog::find(const std::string& id) const {
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i].id == id) return static_cast<int>(i);
  return -1;
}

bool Catalog::has_packages() const {
  return std::any_of(entries.begin(), entries.end(), [](const Entry& e) { return !e.loose; });
}

Catalog scan(const fs::path& folder, const std::string& port_version) {
  Catalog c;
  c.folder = folder;
  std::error_code ec;
  c.folder_exists = !folder.empty() && fs::is_directory(folder, ec);
  if (!c.folder_exists) return c;
  std::vector<fs::path> loose, dirs;
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    std::error_code ec2;
    if (e.is_regular_file(ec2) && e.path().extension() == ".patch") loose.push_back(e.path());
    else if (e.is_directory(ec2) && utf8(e.path().filename()).rfind('.', 0) != 0 &&
             fs::is_regular_file(e.path() / "mod.json", ec2))
      dirs.push_back(e.path());
  }
  std::sort(loose.begin(), loose.end());  // name order, as the game always did
  std::sort(dirs.begin(), dirs.end());
  if (!loose.empty()) {
    Entry e;
    e.loose = true;
    e.id = kLooseId;
    e.folder = folder;
    e.files = loose;
    count_lines(&e);
    c.entries.push_back(std::move(e));
  }
  for (const fs::path& dir : dirs) {
    Entry e;
    e.folder = dir;
    e.id = utf8(dir.filename());
    std::string text, why;
    if (!read_file(dir / "mod.json", kMaxManifestBytes, &text)) {
      e.error = "mod.json can't be read or is larger than 16 KiB";
    } else if (!parse_manifest(text, &e.manifest, &why, &e.warnings)) {
      e.error = why;
    } else {
      e.id = e.manifest.id;
      if (utf8(dir.filename()) != e.id)
        e.warnings.push_back("the folder is named " + utf8(dir.filename()) + ", not after the id " + e.id);
      if (const int other = c.find(e.id); other >= 0)
        e.error = "another package (folder " + utf8(c.entries[static_cast<size_t>(other)].folder.filename()) +
                  ") has the same id";
      for (const std::string& name : e.manifest.patches) {
        const fs::path file = dir / path_of(name);
        std::error_code ec2;
        if (!fs::is_regular_file(file, ec2)) {
          if (e.error.empty()) e.error = name + " (listed in mod.json) is missing";
        } else if (fs::file_size(file, ec2) > kMaxPatchBytes) {
          if (e.error.empty()) e.error = name + " is larger than 256 KiB";
        } else {
          e.files.push_back(file);
        }
      }
      if (e.error.empty() && !e.manifest.min_port_version.empty() &&
          compare_versions(e.manifest.min_port_version, port_version) > 0)
        e.error = "needs port version " + e.manifest.min_port_version + " or newer (this is " + port_version + ")";
      std::set<std::string> listed;
      for (const std::string& name : e.manifest.patches) listed.insert(name);
      for (const auto& f : fs::directory_iterator(dir, ec)) {
        const std::string name = utf8(f.path().filename());
        if (name == "mod.json" || name == "README.md" || listed.count(name)) continue;
        if (is_patch_name(name)) e.warnings.push_back(name + " isn't listed in mod.json, so it isn't loaded");
        else e.warnings.push_back(name + " isn't part of a mod package (ignored)");
      }
    }
    std::error_code ec3;
    if (fs::is_regular_file(dir / "README.md", ec3)) read_file(dir / "README.md", kMaxReadmeBytes, &e.readme, true);
    count_lines(&e);
    c.entries.push_back(std::move(e));
  }
  return c;
}

// ---- load order --------------------------------------------------------------------------

std::vector<Slot> resolve_order(const Catalog& c, const std::string& mod_order) {
  std::vector<Slot> out;
  std::vector<bool> placed(c.entries.size(), false);
  std::stringstream in(mod_order);
  for (std::string token; std::getline(in, token, ',');) {
    token = trim(token);
    bool enabled = true;
    if (!token.empty() && token[0] == '-') {
      enabled = false;
      token = trim(token.substr(1));
    }
    const int i = c.find(token);
    if (i < 0 || placed[static_cast<size_t>(i)]) continue;
    placed[static_cast<size_t>(i)] = true;
    out.push_back({i, enabled});
  }
  // Defaults: loose patches on, at the front; packages off, at the end, by id.
  std::vector<int> rest;
  for (size_t i = 0; i < c.entries.size(); ++i) {
    if (placed[i]) continue;
    if (c.entries[i].loose) out.insert(out.begin(), Slot{static_cast<int>(i), true});
    else rest.push_back(static_cast<int>(i));
  }
  std::sort(rest.begin(), rest.end(), [&](int a, int b) {
    return c.entries[static_cast<size_t>(a)].id < c.entries[static_cast<size_t>(b)].id;
  });
  for (int i : rest) out.push_back({i, false});
  return out;
}

std::string format_order(const Catalog& c, const std::vector<Slot>& order) {
  std::string out;
  for (const Slot& s : order) {
    if (s.entry < 0 || static_cast<size_t>(s.entry) >= c.entries.size()) continue;
    if (!out.empty()) out += ',';
    if (!s.enabled) out += '-';
    out += c.entries[static_cast<size_t>(s.entry)].id;
  }
  return out;
}

bool add_line(std::string text, const std::string& label, int source, std::vector<Line>* lines,
              std::vector<BadLine>* bad) {
  if (!strip_comment(text)) return false;
  Line l;
  l.label = label;
  l.source = source;
  if (!parse_patch_line(text, &l.patch)) {
    if (bad) bad->push_back({label, text});
    return false;
  }
  lines->push_back(std::move(l));
  return true;
}

void read_lines(const Catalog& c, const std::vector<Slot>& order, std::vector<Line>* lines, std::vector<BadLine>* bad) {
  for (const Slot& s : order) {
    if (!s.enabled || s.entry < 0 || static_cast<size_t>(s.entry) >= c.entries.size()) continue;
    const Entry& e = c.entries[static_cast<size_t>(s.entry)];
    if (!e.usable()) continue;
    for (const fs::path& file : e.files) {
      const std::string prefix = (e.loose ? std::string() : e.id + "/") + utf8(file.filename()) + ":";
      std::ifstream in(file);
      int line_no = 0;
      for (std::string text; std::getline(in, text);) {
        ++line_no;
        add_line(text, prefix + std::to_string(line_no), s.entry, lines, bad);
      }
    }
  }
}

// ---- conflicts ---------------------------------------------------------------------------

Analysis analyse(const std::vector<Line>& lines) {
  Analysis an;
  const size_t n = lines.size();
  an.overridden_by.assign(n, -1);
  an.redundant_with.assign(n, -1);
  an.same_place.assign(n, {});
  std::map<std::pair<int, int>, std::vector<size_t>> groups;  // (kind, target) -> lines in order
  for (size_t i = 0; i < n; ++i)
    groups[{static_cast<int>(lines[i].patch.kind), lines[i].patch.target}].push_back(i);
  for (const auto& [key, members] : groups) {
    for (size_t x = 0; x < members.size(); ++x) {
      for (size_t y = x + 1; y < members.size(); ++y) {
        const size_t i = members[x], j = members[y];
        const Line& a = lines[i];
        const Line& b = lines[j];
        bool overlap = false;
        Relation rel;
        if (same_place(a.patch, b.patch, &overlap)) {
          an.same_place[i].push_back(static_cast<int>(j));
          an.same_place[j].push_back(static_cast<int>(i));
          if (a.patch.expected == b.patch.expected && a.patch.value == b.patch.value) rel = Relation::kSame;
          else if (b.patch.expected == a.patch.value) rel = Relation::kBuildsOn;
          else if (a.source < 0 && b.source >= 0) rel = Relation::kSeedWins;
          else if (a.source == b.source) rel = Relation::kWithin;
          else if (a.patch.expected == b.patch.expected) rel = Relation::kOverride;
          else rel = Relation::kMismatch;
        } else if (overlap) {
          rel = a.source < 0 && b.source >= 0 ? Relation::kSeedWins
                : a.source == b.source        ? Relation::kWithin
                                              : Relation::kOverlap;
        } else {
          continue;
        }
        if (rel == Relation::kSame && an.redundant_with[j] < 0) an.redundant_with[j] = static_cast<int>(i);
        if (rel == Relation::kOverride) an.overridden_by[i] = static_cast<int>(j);  // the last one wins
        an.conflicts.push_back({rel, i, j});
      }
    }
  }
  // A line left out (overridden, or the same change as an earlier one) can't clash with
  // a third: drop those pairs, which would only repeat the conflict that left it out.
  auto out = [&](size_t i) { return an.overridden_by[i] >= 0 || an.redundant_with[i] >= 0; };
  std::erase_if(an.conflicts, [&](const Conflict& c) {
    return (c.relation == Relation::kMismatch || c.relation == Relation::kOverlap) && (out(c.first) || out(c.second));
  });
  std::sort(an.conflicts.begin(), an.conflicts.end(), [](const Conflict& x, const Conflict& y) {
    return x.second != y.second ? x.second < y.second : x.first < y.first;
  });
  return an;
}

std::string explain(const Conflict& c, const std::vector<Line>& lines, const std::function<std::string(int)>& name) {
  const Line& a = lines[c.first];
  const Line& b = lines[c.second];
  const std::string A = name(a.source), B = name(b.source), place = describe_place(a.patch);
  auto v = [](const Line& l, u32 x) { return describe_value(l.patch, x); };
  switch (c.relation) {
    case Relation::kSame:
      return A + " and " + B + " make the same change to " + place + " (to " + v(a, a.patch.value) + ").";
    case Relation::kBuildsOn:
      return B + " builds on " + A + "'s change to " + place + " (" + v(a, a.patch.value) + " -> " +
             v(b, b.patch.value) + "): both apply.";
    case Relation::kOverride:
      return A + " and " + B + " both change " + place + ": " + A + " to " + v(a, a.patch.value) + ", " + B + " to " +
             v(b, b.patch.value) + ". " + B + " is later in the load order, so its change is used and " + A +
             "'s is not. Move " + A + " below " + B + " to use its change instead.";
    case Relation::kMismatch:
      return A + " and " + B + " both change " + place + ", but were written for different values (" + A +
             " expects " + v(a, a.patch.expected) + ", " + B + " expects " + v(b, b.patch.expected) +
             "). At most one of them matches the disc; if " + A + "'s line applies, " + B + "'s is skipped.";
    case Relation::kOverlap:
      return A + " changes " + place + " and " + B + " changes " + describe_place(b.patch) +
             ", which overlap. " + A + "'s line applies first, so " + B + "'s is most likely skipped.";
    case Relation::kSeedWins:
      return "The Archipelago seed changes " + place + " too; the seed wins, so " + B + "'s line (" + b.label +
             ") is skipped.";
    case Relation::kWithin:
      return "Two lines of " + A + " change " + place + " (" + a.label + " and " + b.label +
             "); only the first applies.";
  }
  return {};
}

// ---- installing and removing ---------------------------------------------------------------

bool read_zip_package(const fs::path& zip, ZipPackage* out, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  ZipReader z;
  std::string why;
  if (!z.open(zip, &why)) return fail("It can't be installed: " + why + ".");
  std::vector<const ZipEntry*> files;
  for (const ZipEntry& e : z.entries()) {
    if (e.name.rfind("__MACOSX/", 0) == 0) continue;  // macOS archive metadata
    const std::string base = e.name.substr(e.name.find_last_of('/') + 1);
    if (base == ".DS_Store" || e.directory) continue;
    const bool parent = e.name == ".." || e.name.rfind("../", 0) == 0 || e.name.find("/../") != std::string::npos ||
                        (e.name.size() >= 3 && e.name.compare(e.name.size() - 3, 3, "/..") == 0);
    if (parent || e.name.empty() || e.name[0] == '/' || e.name.find(':') != std::string::npos)
      return fail("It can't be installed: it holds a file with an unsafe path (" + e.name + ").");
    files.push_back(&e);
  }
  // The package: at the archive's root, or in its one top-level folder.
  std::string prefix;
  const bool at_root = std::any_of(files.begin(), files.end(), [](const ZipEntry* e) { return e->name == "mod.json"; });
  if (!at_root) {
    std::set<std::string> tops;
    for (const ZipEntry* e : files) tops.insert(e->name.substr(0, e->name.find('/')));
    const bool one = tops.size() == 1 && std::any_of(files.begin(), files.end(), [&](const ZipEntry* e) {
                       return e->name == *tops.begin() + "/mod.json";
                     });
    if (!one)
      return fail("It isn't a mod package: there is no mod.json at its top. A package is a folder with mod.json, its "
                  ".patch files and an optional README.md.");
    prefix = *tops.begin() + "/";
  }
  ZipPackage pkg;
  size_t total = 0;
  std::set<std::string> names;
  for (const ZipEntry* e : files) {
    std::string rel = e->name.substr(prefix.size());
    if (rel.find('/') != std::string::npos)
      return fail("It can't be installed: it holds a subfolder (" + rel + "); a mod package holds only files.");
    size_t limit = 0;
    if (rel == "mod.json") {
      limit = kMaxManifestBytes;
    } else if (lower(rel) == "readme.md") {
      rel = "README.md";
      limit = kMaxReadmeBytes;
    } else if (is_patch_name(rel)) {
      limit = kMaxPatchBytes;
    } else {
      return fail("It can't be installed: it holds " + rel +
                  ", and a mod package may hold only mod.json, .patch files and README.md.");
    }
    if (!names.insert(lower(rel)).second) return fail("It can't be installed: it holds " + rel + " twice.");
    std::vector<u8> bytes;
    if (e->size > limit) return fail("It can't be installed: " + rel + " is larger than allowed.");
    if (!z.read(*e, &bytes, &why, limit)) return fail("It can't be installed: " + why + ".");
    total += bytes.size();
    if (total > kMaxPackageBytes) return fail("It can't be installed: the package is larger than 1 MiB.");
    if (std::find(bytes.begin(), bytes.end(), u8{0}) != bytes.end())
      return fail("It can't be installed: " + rel + " isn't a text file.");
    pkg.files.emplace_back(rel, std::move(bytes));
  }
  const auto manifest = std::find_if(pkg.files.begin(), pkg.files.end(), [](const auto& f) { return f.first == "mod.json"; });
  const std::string text(manifest->second.begin(), manifest->second.end());
  if (!parse_manifest(text, &pkg.manifest, &why)) return fail("It can't be installed: " + why + ".");
  for (const std::string& p : pkg.manifest.patches)
    if (!names.count(lower(p))) return fail("It can't be installed: " + p + " (listed in mod.json) isn't in it.");
  for (const auto& [rel, bytes] : pkg.files)
    if (is_patch_name(rel) &&
        std::none_of(pkg.manifest.patches.begin(), pkg.manifest.patches.end(), [&](const std::string& p) { return p == rel; }))
      return fail("It can't be installed: " + rel + " isn't listed in its mod.json.");
  *out = std::move(pkg);
  return true;
}

bool install_package(const ZipPackage& pkg, const Catalog& c, bool replace, InstallResult* out, std::string* error,
                     bool* exists) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  if (exists) *exists = false;
  const std::string& id = pkg.manifest.id;
  const int existing = c.find(id);
  if (existing >= 0 && !replace) {
    if (exists) *exists = true;
    return fail(id + " is installed already.");
  }
  std::error_code ec;
  fs::create_directories(c.folder, ec);
  const fs::path tmp = c.folder / path_of(".installing-" + id), target = c.folder / path_of(id);
  const fs::path old = existing >= 0 ? c.entries[static_cast<size_t>(existing)].folder : fs::path();
  if (existing < 0 && fs::exists(target, ec))
    return fail("A folder named " + id + " is in the mods folder already, and it isn't a mod package.");
  if (existing >= 0 && old != target && fs::exists(target, ec))
    return fail("A folder named " + id + " is in the mods folder already.");
  fs::remove_all(tmp, ec);
  if (!fs::create_directory(tmp, ec)) return fail("The mods folder can't be written: " + ec.message());
  for (const auto& [rel, bytes] : pkg.files) {
    std::ofstream f(tmp / path_of(rel), std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f) {
      fs::remove_all(tmp, ec);
      return fail("The mods folder can't be written (" + rel + ").");
    }
  }
  const fs::path aside = c.folder / path_of(".removing-" + id);
  if (existing >= 0) {
    fs::remove_all(aside, ec);
    fs::rename(old, aside, ec);
    if (ec) {
      fs::remove_all(tmp, ec);
      return fail("The installed copy can't be replaced: " + ec.message());
    }
  }
  fs::rename(tmp, target, ec);
  if (ec) {
    const std::string why = ec.message();
    std::error_code ec2;
    if (existing >= 0) fs::rename(aside, old, ec2);
    fs::remove_all(tmp, ec2);
    return fail("The package can't be put in place: " + why);
  }
  if (existing >= 0) fs::remove_all(aside, ec);
  if (out) {
    out->id = id;
    out->version = pkg.manifest.version;
    out->previous_version = existing >= 0 ? c.entries[static_cast<size_t>(existing)].manifest.version : std::string();
    out->folder = target;
    out->replaced = existing >= 0;
  }
  return true;
}

bool remove_package(const Catalog& c, int entry, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  if (entry < 0 || static_cast<size_t>(entry) >= c.entries.size()) return fail("No such mod.");
  const Entry& e = c.entries[static_cast<size_t>(entry)];
  if (e.loose) return fail("Loose patches aren't a package: remove the files from the mods folder.");
  std::error_code ec;
  const fs::path parent = fs::weakly_canonical(e.folder.parent_path(), ec);
  const fs::path folder = fs::weakly_canonical(c.folder, ec);
  if (ec || parent != folder || !fs::is_regular_file(e.folder / "mod.json", ec))
    return fail("Only a package folder directly inside the mods folder can be removed.");
  fs::remove_all(e.folder, ec);
  if (ec) return fail("The folder can't be deleted: " + ec.message());
  return true;
}

}  // namespace ss::mods
