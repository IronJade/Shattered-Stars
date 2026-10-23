#include "game/ap/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ss::game::ap {
namespace {

const Json kNull;
const std::string kEmpty;
const std::map<std::string, Json> kNoItems;

struct Parser {
  const std::string& s;
  size_t i = 0;
  std::string error;

  int line() const {
    int n = 1;
    for (size_t k = 0; k < i && k < s.size(); ++k) n += s[k] == '\n';
    return n;
  }
  bool fail(const std::string& what) {
    if (error.empty()) error = "line " + std::to_string(line()) + ": " + what;
    return false;
  }
  void skip() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  }
  bool literal(const char* word) {
    size_t n = 0;
    while (word[n] != 0) ++n;
    if (s.compare(i, n, word) != 0) return false;
    i += n;
    return true;
  }
  static void utf8(std::string& out, uint32_t c) {
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
  bool hex4(uint32_t* out) {
    if (i + 4 > s.size()) return fail("short \\u escape");
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s[i++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
      else return fail("bad \\u escape");
    }
    *out = v;
    return true;
  }
  bool string(std::string* out) {
    ++i;  // the opening quote
    while (i < s.size()) {
      const char c = s[i++];
      if (c == '"') return true;
      if (c != '\\') {
        *out += c;
        continue;
      }
      if (i >= s.size()) break;
      const char e = s[i++];
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
          uint32_t c1 = 0;
          if (!hex4(&c1)) return false;
          if (c1 >= 0xD800 && c1 < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            i += 2;
            uint32_t c2 = 0;
            if (!hex4(&c2)) return false;
            c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
          }
          utf8(*out, c1);
          break;
        }
        default: return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }
  bool value(Json* out, int depth) {
    if (depth > 64) return fail("nested too deep");
    skip();
    if (i >= s.size()) return fail("value expected");
    const char c = s[i];
    if (c == '{') {
      ++i;
      *out = Json::object();
      skip();
      if (i < s.size() && s[i] == '}') {
        ++i;
        return true;
      }
      for (;;) {
        skip();
        if (i >= s.size() || s[i] != '"') return fail("key expected");
        std::string key;
        if (!string(&key)) return false;
        skip();
        if (i >= s.size() || s[i] != ':') return fail("':' expected");
        ++i;
        Json v;
        if (!value(&v, depth + 1)) return false;
        out->set(key, std::move(v));
        skip();
        if (i < s.size() && s[i] == ',') {
          ++i;
          continue;
        }
        if (i < s.size() && s[i] == '}') {
          ++i;
          return true;
        }
        return fail("',' or '}' expected");
      }
    }
    if (c == '[') {
      ++i;
      *out = Json::array();
      skip();
      if (i < s.size() && s[i] == ']') {
        ++i;
        return true;
      }
      for (;;) {
        Json v;
        if (!value(&v, depth + 1)) return false;
        out->push(std::move(v));
        skip();
        if (i < s.size() && s[i] == ',') {
          ++i;
          continue;
        }
        if (i < s.size() && s[i] == ']') {
          ++i;
          return true;
        }
        return fail("',' or ']' expected");
      }
    }
    if (c == '"') {
      std::string str;
      if (!string(&str)) return false;
      *out = Json::string(std::move(str));
      return true;
    }
    if (literal("true")) {
      *out = Json::boolean(true);
      return true;
    }
    if (literal("false")) {
      *out = Json::boolean(false);
      return true;
    }
    if (literal("null")) {
      *out = Json();
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      const size_t start = i;
      bool real = false;
      if (s[i] == '-') ++i;
      while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                              s[i] == '+' || s[i] == '-')) {
        real = real || s[i] == '.' || s[i] == 'e' || s[i] == 'E';
        ++i;
      }
      const std::string text = s.substr(start, i - start);
      char* end = nullptr;
      if (!real) {
        const long long v = std::strtoll(text.c_str(), &end, 10);
        if (end == nullptr || *end != 0) return fail("bad number");
        *out = Json::number(static_cast<int64_t>(v));
      } else {
        const double v = std::strtod(text.c_str(), &end);
        if (end == nullptr || *end != 0) return fail("bad number");
        *out = Json::number(v);
      }
      return true;
    }
    return fail(std::string("unexpected '") + c + "'");
  }
};

void escape(std::string& out, const std::string& s) {
  out += '"';
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

}  // namespace

Json Json::boolean(bool b) {
  Json j;
  j.type_ = Type::Bool;
  j.bool_ = b;
  return j;
}
Json Json::number(int64_t n) {
  Json j;
  j.type_ = Type::Number;
  j.int_ = n;
  j.real_ = static_cast<double>(n);
  j.integral_ = true;
  return j;
}
Json Json::number(double d) {
  Json j;
  j.type_ = Type::Number;
  j.real_ = d;
  j.int_ = static_cast<int64_t>(d);
  j.integral_ = std::floor(d) == d && std::fabs(d) < 9.0e18;
  return j;
}
Json Json::string(std::string s) {
  Json j;
  j.type_ = Type::String;
  j.string_ = std::move(s);
  return j;
}
Json Json::array() {
  Json j;
  j.type_ = Type::Array;
  j.array_ = std::make_shared<std::vector<Json>>();
  return j;
}
Json Json::object() {
  Json j;
  j.type_ = Type::Object;
  j.object_ = std::make_shared<std::map<std::string, Json>>();
  return j;
}

bool Json::as_bool(bool fallback) const {
  if (type_ == Type::Bool) return bool_;
  if (type_ == Type::Number) return int_ != 0;
  return fallback;
}
int64_t Json::as_int(int64_t fallback) const {
  if (type_ == Type::Number) return integral_ ? int_ : static_cast<int64_t>(real_);
  if (type_ == Type::String && !string_.empty()) {
    char* end = nullptr;
    const long long v = std::strtoll(string_.c_str(), &end, 0);
    if (end != nullptr && *end == 0) return v;
  }
  return fallback;
}
const std::string& Json::as_string() const { return type_ == Type::String ? string_ : kEmpty; }

size_t Json::size() const {
  if (type_ == Type::Array) return array_->size();
  if (type_ == Type::Object) return object_->size();
  return 0;
}
const Json& Json::operator[](size_t i) const {
  return type_ == Type::Array && i < array_->size() ? (*array_)[i] : kNull;
}
void Json::push(Json v) {
  if (type_ != Type::Array) *this = array();
  array_->push_back(std::move(v));
}
bool Json::has(const std::string& key) const { return type_ == Type::Object && object_->count(key) != 0; }
const Json& Json::operator[](const std::string& key) const {
  if (type_ != Type::Object) return kNull;
  const auto it = object_->find(key);
  return it != object_->end() ? it->second : kNull;
}
void Json::set(const std::string& key, Json v) {
  if (type_ != Type::Object) *this = object();
  (*object_)[key] = std::move(v);
}
const std::map<std::string, Json>& Json::items() const { return type_ == Type::Object ? *object_ : kNoItems; }

bool Json::parse(const std::string& text, Json* out, std::string* error) {
  Parser p{text, 0, {}};
  Json v;
  bool ok = p.value(&v, 0);
  if (ok) {
    p.skip();
    if (p.i != text.size()) ok = p.fail("trailing text");
  }
  if (!ok) {
    if (error != nullptr) *error = p.error;
    return false;
  }
  *out = std::move(v);
  return true;
}

std::string Json::dump(int indent) const {
  std::string out;
  dump_to(out, indent, 0);
  out += '\n';
  return out;
}

void Json::dump_to(std::string& out, int indent, int depth) const {
  const auto newline = [&](int d) {
    if (indent <= 0) return;
    out += '\n';
    out.append(static_cast<size_t>(d * indent), ' ');
  };
  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number:
      if (integral_) {
        out += std::to_string(int_);
      } else {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.17g", real_);
        out += buf;
      }
      break;
    case Type::String: escape(out, string_); break;
    case Type::Array: {
      out += '[';
      bool first = true;
      for (const Json& v : *array_) {
        if (!first) out += ',';
        first = false;
        newline(depth + 1);
        v.dump_to(out, indent, depth + 1);
      }
      if (!array_->empty()) newline(depth);
      out += ']';
      break;
    }
    case Type::Object: {
      out += '{';
      bool first = true;
      for (const auto& [k, v] : *object_) {
        if (!first) out += ',';
        first = false;
        newline(depth + 1);
        escape(out, k);
        out += indent > 0 ? ": " : ":";
        v.dump_to(out, indent, depth + 1);
      }
      if (!object_->empty()) newline(depth);
      out += '}';
      break;
    }
  }
}

}  // namespace ss::game::ap
