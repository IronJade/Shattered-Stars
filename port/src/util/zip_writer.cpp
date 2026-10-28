#include "util/zip_writer.h"

#include <algorithm>
#include <system_error>

#include "util/deflate.h"
#include "util/utf8.h"

namespace ss::util {
namespace {

namespace fs = std::filesystem;

void put16(std::string& out, u32 v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>(v >> 8 & 0xFF));
}

void put32(std::string& out, u32 v) {
  put16(out, v & 0xFFFF);
  put16(out, v >> 16);
}

constexpr u32 kLocalSig = 0x04034b50, kCentralSig = 0x02014b50, kEndSig = 0x06054b50;
constexpr u16 kVersion = 20;  // 2.0: the version a stored entry in a folder needs (made by: MS-DOS)
constexpr u16 kUtf8Flag = 1 << 11;

bool safe_name(std::string_view name) {
  if (name.empty() || name.size() > 0xFFFF || name.front() == '/' || name.back() == '/') return false;
  if (name.find('\\') != std::string_view::npos || name.find(':') != std::string_view::npos) return false;
  size_t at = 0;
  while (at <= name.size()) {
    const size_t end = std::min(name.find('/', at), name.size());
    const std::string_view part = name.substr(at, end - at);
    if (part.empty() || part == "." || part == "..") return false;
    at = end + 1;
  }
  for (unsigned char c : name)
    if (c < 0x20) return false;
  return true;
}

bool ascii(std::string_view s) {
  for (unsigned char c : s)
    if (c >= 0x80) return false;
  return true;
}

}  // namespace

ZipWriter::~ZipWriter() { abandon(); }

bool ZipWriter::fail(std::string* error, const std::string& why) {
  if (error) *error = why;
  abandon();
  return false;
}

bool ZipWriter::open(const fs::path& path, std::string* error) {
  abandon();
  path_ = path;
  part_ = path;
  part_ += ".part";
  offset_ = 0;
  central_.clear();
  file_.open(part_, std::ios::binary | std::ios::trunc);
  if (!file_) return fail(error, "could not create " + path_utf8(part_));
  return true;
}

bool ZipWriter::add(std::string_view name, std::span<const u8> data, std::string* error) {
  if (!file_.is_open()) {
    if (error) *error = "the archive is not open";
    return false;
  }
  if (!safe_name(name)) return fail(error, "bad entry name '" + std::string(name) + "'");
  if (central_.size() >= 0xFFFF) return fail(error, "too many entries for a .zip without zip64");
  const u64 header = 30 + name.size();
  if (offset_ + header + data.size() > 0xFFFFFFFFull || data.size() > 0xFFFFFFFFull)
    return fail(error, "the archive would pass 4 GiB (no zip64)");
  Central c;
  c.name = std::string(name);
  c.crc = crc32(data.data(), data.size());
  c.size = static_cast<u32>(data.size());
  c.offset = static_cast<u32>(offset_);
  c.utf8 = !ascii(name);
  std::string h;
  put32(h, kLocalSig);
  put16(h, kVersion);
  put16(h, c.utf8 ? kUtf8Flag : 0);
  put16(h, 0);  // stored
  put16(h, kDosTime);
  put16(h, kDosDate);
  put32(h, c.crc);
  put32(h, c.size);  // compressed size = size: stored
  put32(h, c.size);
  put16(h, static_cast<u32>(name.size()));
  put16(h, 0);  // no extra field
  h += name;
  file_.write(h.data(), static_cast<std::streamsize>(h.size()));
  if (!data.empty()) file_.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!file_) return fail(error, "could not write " + path_utf8(part_));
  offset_ += header + data.size();
  central_.push_back(std::move(c));
  return true;
}

bool ZipWriter::finish(std::string* error) {
  if (!file_.is_open()) {
    if (error) *error = "the archive is not open";
    return false;
  }
  std::string cd;
  for (const Central& c : central_) {
    put32(cd, kCentralSig);
    put16(cd, kVersion);  // made by: MS-DOS (0), 2.0
    put16(cd, kVersion);  // needed to extract
    put16(cd, c.utf8 ? kUtf8Flag : 0);
    put16(cd, 0);  // stored
    put16(cd, kDosTime);
    put16(cd, kDosDate);
    put32(cd, c.crc);
    put32(cd, c.size);
    put32(cd, c.size);
    put16(cd, static_cast<u32>(c.name.size()));
    put16(cd, 0);  // extra
    put16(cd, 0);  // comment
    put16(cd, 0);  // disk
    put16(cd, 0);  // internal attributes
    put32(cd, 0);  // external attributes
    put32(cd, c.offset);
    cd += c.name;
  }
  if (offset_ + cd.size() + 22 > 0xFFFFFFFFull) return fail(error, "the archive would pass 4 GiB (no zip64)");
  std::string end;
  put32(end, kEndSig);
  put16(end, 0);  // this disk
  put16(end, 0);  // the directory's disk
  put16(end, static_cast<u32>(central_.size()));
  put16(end, static_cast<u32>(central_.size()));
  put32(end, static_cast<u32>(cd.size()));
  put32(end, static_cast<u32>(offset_));
  put16(end, 0);  // no comment
  file_.write(cd.data(), static_cast<std::streamsize>(cd.size()));
  file_.write(end.data(), static_cast<std::streamsize>(end.size()));
  file_.close();
  if (!file_) return fail(error, "could not write " + path_utf8(part_));
  std::error_code ec;
  fs::rename(part_, path_, ec);
  if (ec) {  // a system that won't rename over an existing file: remove it first
    std::error_code ignore;
    fs::remove(path_, ignore);
    ec.clear();
    fs::rename(part_, path_, ec);
  }
  if (ec) return fail(error, "could not rename " + path_utf8(part_) + " to " + path_utf8(path_) + ": " + ec.message());
  central_.clear();
  part_.clear();
  return true;
}

void ZipWriter::abandon() {
  if (file_.is_open()) file_.close();
  file_.clear();
  if (!part_.empty()) {
    std::error_code ignore;
    fs::remove(part_, ignore);
    part_.clear();
  }
  central_.clear();
  offset_ = 0;
}

}  // namespace ss::util
