#include "runtime/boot_exe.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <vector>

#include "disc/disc_image.h"

namespace ss::runtime {
namespace {

struct Image {
  std::vector<u8> text;  // the executable's loaded bytes
  u32 address = 0;       // where they load (t_addr)
};
Image g_image;

struct FileExtent {
  u32 lba = 0;
  u32 size = 0;
};

// ISO 9660: the root directory (from the primary volume descriptor at sector 16),
// searched for 'name' (case-insensitive, ignoring the ";1" version).
std::optional<FileExtent> find_root_file(disc::DiscImage& disc, const std::string& name) {
  std::vector<u8> pvd(disc::DiscImage::kSectorData);
  if (!disc.read_sectors(16, 1, pvd.data()) || pvd[0] != 1 || std::string(pvd.begin() + 1, pvd.begin() + 6) != "CD001")
    return std::nullopt;
  const u8* root = pvd.data() + 156;
  const u32 dir_lba = read_u32(root + 2), dir_size = read_u32(root + 10);
  std::vector<u8> dir((dir_size + 2047) / 2048 * 2048);
  if (dir.empty() || !disc.read_sectors(dir_lba, static_cast<u32>(dir.size() / 2048), dir.data())) return std::nullopt;
  for (size_t pos = 0; pos < dir_size;) {
    const u8 len = dir[pos];
    if (len == 0) {  // records don't cross sectors: skip to the next one
      pos = (pos / 2048 + 1) * 2048;
      continue;
    }
    std::string entry(reinterpret_cast<const char*>(&dir[pos + 33]), dir[pos + 32]);
    entry = entry.substr(0, entry.find(';'));
    auto upper = [](std::string s) {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      return s;
    };
    if (upper(entry) == upper(name)) return FileExtent{read_u32(&dir[pos + 2]), read_u32(&dir[pos + 10])};
    pos += len;
  }
  return std::nullopt;
}

std::optional<std::vector<u8>> read_file(disc::DiscImage& disc, const FileExtent& f) {
  std::vector<u8> data((f.size + 2047) / 2048 * 2048);
  if (!disc.read_sectors(f.lba, static_cast<u32>(data.size() / 2048), data.data())) return std::nullopt;
  data.resize(f.size);
  return data;
}

}  // namespace

std::string boot_exe_name(disc::DiscImage& disc) {
  // SYSTEM.CNF: "BOOT = cdrom:\SCUS_944.21;1"
  if (auto cnf = find_root_file(disc, "SYSTEM.CNF")) {
    if (auto text = read_file(disc, *cnf)) {
      const std::string s(text->begin(), text->end());
      const auto boot = s.find("BOOT");
      if (boot != std::string::npos) {
        const auto start = s.find_first_of(":\\", boot);
        auto name_start = s.find_first_not_of(":\\", start);
        const auto name_end = s.find_first_of(";\r\n", name_start);
        if (start != std::string::npos && name_start != std::string::npos)
          return s.substr(name_start, name_end - name_start);
      }
    }
  }
  return {};
}

bool boot_exe_open(disc::DiscImage& disc, std::string* error) {
  g_image = Image{};
  auto fail = [&](const char* what) {
    if (error) *error = what;
    return false;
  };
  std::string exe_name = boot_exe_name(disc);
  if (exe_name.empty()) exe_name = "SCUS_944.21";
  auto file = find_root_file(disc, exe_name);
  if (!file) return fail("boot executable not found on the disc");
  auto exe = read_file(disc, *file);
  if (!exe || exe->size() < 0x800 || std::string(exe->begin(), exe->begin() + 8) != "PS-X EXE")
    return fail("boot executable is not a PS-X EXE");
  const u32 t_addr = read_u32(exe->data() + 0x18), t_size = read_u32(exe->data() + 0x1C);
  g_image.address = t_addr;
  g_image.text.assign(exe->begin() + 0x800, exe->begin() + 0x800 + std::min<size_t>(t_size, exe->size() - 0x800));
  return true;
}

const u8* boot_exe_data(u32 address) {
  const u32 offset = address - g_image.address;
  return offset < g_image.text.size() ? g_image.text.data() + offset : nullptr;
}

}  // namespace ss::runtime
