#include "game/common/program_image.h"

#include <map>
#include <span>
#include <string>
#include <vector>

#include "disc/slz.h"
#include "game/boot/system.h"
#include "runtime/region.h"

namespace ss::game {

const u8* program_data(int entry, u32 address) { return program_data_at(entry, kProgramBase, address); }

namespace {

// Another release's image is read through its address map (runtime/region.h): addresses
// stay NA's, the bytes come from wherever that release keeps them.
struct Image {
  std::vector<u8> bytes;
  const region::ImageMap* map = nullptr;
  std::string key;
};

const Image& image_of(int entry) {
  static std::map<int, Image> images;
  auto it = images.find(entry);
  if (it == images.end()) {
    // No disc open yet (a test asking before any boot): nothing to read, and nothing cached, so
    // the image is read once a disc is open. (Cached empty, every later read gave null: a field
    // test running after one that asked first crashed in field_create_state_block.) [release agent]
    disc::Archive* archive = get_system()->loader.archive();
    if (archive == nullptr) {
      static const Image none;
      return none;
    }
    Image image;
    if (auto packed = archive->read_entry(static_cast<u32>(entry))) {
      if (auto unpacked = disc::slz::unpack_block(*packed, 0, 0, nullptr)) image.bytes = std::move(*unpacked);
    }
    if (!region::is_na()) {
      image.map = region::entry_map(entry, 0);
      if (image.map == nullptr) image.bytes.clear();  // no map: nothing can be read at NA addresses
      else region::prepare(image.map, image.bytes);
    }
    image.key = region::entry_key(entry, 0);
    it = images.emplace(entry, std::move(image)).first;
  }
  return it->second;
}

}  // namespace

const u8* program_data_at(int entry, u32 base, u32 address) {
  const Image& image = image_of(entry);
  return region::image_byte(image.map, image.bytes, base, address, image.key.c_str());
}

std::span<const u8> program_run(int entry, u32 base, u32 address) {
  const Image& image = image_of(entry);
  const u8* start = region::image_byte(image.map, image.bytes, base, address, image.key.c_str());
  if (start == nullptr) return {};
  return {start, static_cast<size_t>(image.bytes.data() + image.bytes.size() - start)};
}

}  // namespace ss::game
