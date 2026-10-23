#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// A small JSON reader / writer for the Archipelago seed and its save sidecar (port feature;
// no JSON library is in the build). Numbers keep their integer value when they have one
// (ids exceed 2^31); objects keep their keys sorted, which the files do not depend on.
namespace ss::game::ap {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Json() = default;
  static Json boolean(bool b);
  static Json number(int64_t n);
  static Json number(double d);
  static Json string(std::string s);
  static Json array();
  static Json object();

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  // Values with a fallback when the type does not match.
  bool as_bool(bool fallback = false) const;
  int64_t as_int(int64_t fallback = 0) const;
  const std::string& as_string() const;  // empty unless a string

  // Arrays.
  size_t size() const;
  const Json& operator[](size_t i) const;  // a null value out of range
  void push(Json v);

  // Objects.
  bool has(const std::string& key) const;
  const Json& operator[](const std::string& key) const;  // a null value when missing
  void set(const std::string& key, Json v);
  const std::map<std::string, Json>& items() const;

  // Text. parse returns false (and an error with the line) on malformed input.
  static bool parse(const std::string& text, Json* out, std::string* error);
  std::string dump(int indent = 1) const;

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  bool integral_ = true;
  int64_t int_ = 0;
  double real_ = 0;
  std::string string_;
  std::shared_ptr<std::vector<Json>> array_;
  std::shared_ptr<std::map<std::string, Json>> object_;
  void dump_to(std::string& out, int indent, int depth) const;
};

}  // namespace ss::game::ap
