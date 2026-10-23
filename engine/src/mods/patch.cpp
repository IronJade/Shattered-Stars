#include "mods/patch.h"

#include <cstdio>
#include <cstdlib>
#include <span>
#include <sstream>

namespace ss::mods {
namespace {

bool parse_number(const std::string& s, u32* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  const long long v = std::strtoll(s.c_str(), &end, 0);
  if (end == nullptr || *end != 0) return false;
  *out = static_cast<u32>(v);
  return true;
}

struct FieldName {
  const char* name;
  u32 offset;
  int size;
};
constexpr FieldName kItemFields[] = {{"price", 0x00, 4}, {"atk", 0x08, 2},    {"def", 0x10, 2},  {"amount", 0x19, 1},
                                     {"effect", 0x28, 1}, {"type", 0x2A, 1}, {"sell", 0x2D, 1}};
constexpr FieldName kEnemyFields[] = {{"hp", 0x00, 4},     {"mp", 0x04, 4},         {"exp", 0x08, 4},
                                      {"fol", 0x0C, 4},    {"speed", 0x16, 2},      {"drop1", 0x18, 2},
                                      {"drop2", 0x1A, 2},  {"atk", 0x1C, 2},        {"def", 0x1E, 2},
                                      {"int", 0x20, 2},    {"level", 0x24, 2},      {"eva", 0x26, 2},
                                      {"acc", 0x28, 2},    {"guts", 0x36, 1},       {"drop1_odds", 0x37, 1},
                                      {"drop2_odds", 0x38, 1}};

std::span<const FieldName> fields_of(Kind kind) {
  return kind == Kind::kItem ? std::span<const FieldName>(kItemFields) : std::span<const FieldName>(kEnemyFields);
}

// A field name, "OFFSET" or "OFFSET:SIZE"; the default size by record layout.
bool parse_field(const std::string& s, Kind kind, u32* offset, int* size) {
  for (const FieldName& f : fields_of(kind)) {
    if (s == f.name) {
      *offset = f.offset;
      *size = f.size;
      return true;
    }
  }
  const auto colon = s.find(':');
  if (!parse_number(s.substr(0, colon), offset)) return false;
  if (colon != std::string::npos) {
    u32 n = 0;
    if (!parse_number(s.substr(colon + 1), &n) || (n != 1 && n != 2 && n != 4)) return false;
    *size = static_cast<int>(n);
  } else if (kind == Kind::kItem) {
    *size = *offset == 0 ? 4 : (*offset >= 4 && *offset < 0x14) ? 2 : 1;
  } else {
    *size = *offset < 0x10 ? 4 : ((*offset < 0x2A) || *offset == 0x3A) ? 2 : 1;
  }
  const size_t limit = kind == Kind::kItem ? kItemRecord : kEnemyRecord;
  return *offset + static_cast<u32>(*size) <= limit;
}

}  // namespace

u32 field_mask(int size) { return size == 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1; }

bool strip_comment(std::string& line) {
  if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
  return line.find_first_not_of(" \t\r") != std::string::npos;
}

bool parse_patch_line(const std::string& line, PatchLine* out) {
  std::istringstream fields(line);
  std::string kind, a, b, c, d, extra;
  if (!(fields >> kind)) return false;
  PatchLine p;
  u32 target = 0;
  bool ok = static_cast<bool>(fields >> a >> b >> c >> d) && !(fields >> extra) && parse_number(a, &target) &&
            parse_number(c, &p.expected) && parse_number(d, &p.value);
  if (ok) {
    if (kind == "script") {
      p.kind = Kind::kScript;
      ok = parse_number(b, &p.where);
    } else if (kind == "item") {
      p.kind = Kind::kItem;
      ok = parse_field(b, p.kind, &p.where, &p.size) && target >= 1 && target < 0x400;
    } else if (kind == "shop") {
      p.kind = Kind::kShop;
      ok = parse_number(b, &p.where) && p.where < 0x100;
    } else if (kind == "enemy") {
      p.kind = Kind::kEnemy;
      ok = parse_field(b, p.kind, &p.where, &p.size);
    } else {
      ok = false;
    }
  }
  if (!ok) return false;
  p.target = static_cast<int>(target);
  if (p.kind == Kind::kItem || p.kind == Kind::kEnemy) {
    p.expected &= field_mask(p.size);
    p.value &= field_mask(p.size);
  }
  *out = p;
  return true;
}

const char* kind_name(Kind kind) {
  switch (kind) {
    case Kind::kScript: return "script";
    case Kind::kItem: return "item";
    case Kind::kShop: return "shop";
    case Kind::kEnemy: return "enemy";
  }
  return "?";
}

const char* field_name(Kind kind, u32 offset, int size) {
  if (kind != Kind::kItem && kind != Kind::kEnemy) return nullptr;
  for (const FieldName& f : fields_of(kind))
    if (f.offset == offset && f.size == size) return f.name;
  return nullptr;
}

std::string describe_place(const PatchLine& p) {
  char buf[96];
  switch (p.kind) {
    case Kind::kScript:
      std::snprintf(buf, sizeof buf, "map %d word %#x", p.target, p.where);
      break;
    case Kind::kShop:
      std::snprintf(buf, sizeof buf, "shop %d entry %u", p.target, p.where);
      break;
    case Kind::kItem:
    case Kind::kEnemy:
      if (const char* name = field_name(p.kind, p.where, p.size))
        std::snprintf(buf, sizeof buf, "%s %d %s", kind_name(p.kind), p.target, name);
      else
        std::snprintf(buf, sizeof buf, "%s %d +%#x:%d", kind_name(p.kind), p.target, p.where, p.size);
      break;
  }
  return buf;
}

std::string describe_value(const PatchLine& p, u32 value) {
  char buf[48];
  if (p.kind == Kind::kScript) std::snprintf(buf, sizeof buf, "%#010x", value);
  else if (p.kind == Kind::kShop) std::snprintf(buf, sizeof buf, value == 0 ? "nothing (removed)" : "item %#x", value);
  else std::snprintf(buf, sizeof buf, "%u", value);
  return buf;
}

bool same_place(const PatchLine& a, const PatchLine& b, bool* overlap) {
  if (overlap) *overlap = false;
  if (a.kind != b.kind || a.target != b.target) return false;
  switch (a.kind) {
    case Kind::kScript: return a.where == b.where;
    case Kind::kShop: return a.where == b.where && a.expected != 0 && b.expected != 0;
    case Kind::kItem:
    case Kind::kEnemy:
      if (a.where == b.where && a.size == b.size) return true;
      if (overlap && a.where < b.where + static_cast<u32>(b.size) && b.where < a.where + static_cast<u32>(a.size))
        *overlap = true;
      return false;
  }
  return false;
}

}  // namespace ss::mods
