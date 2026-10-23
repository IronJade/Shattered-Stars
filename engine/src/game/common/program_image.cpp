#include "game/common/program_image.h"

#include <map>
#include <string>
#include <vector>

#include "disc/slz.h"
#include "game/boot/system.h"
#include "runtime/region.h"

namespace ss::game {

const u8* program_data(int entry, u32 address) { return program_data_at(entry, kProgramBase, address); }

const u8* program_data_at(int entry, u32 base, u32 address) {
  // Another release's image is read through its address map (runtime/region.h): 'address'
  // stays NA's, the byte comes from wherever that release keeps it.
  struct Image {
    std::vector<u8> bytes;
    const region::ImageMap* map = nullptr;
    std::string key;
  };
  static std::map<int, Image> images;
  auto it = images.find(entry);
  if (it == images.end()) {
    Image image;
    if (disc::Archive* archive = get_system()->loader.archive()) {
      if (auto packed = archive->read_entry(static_cast<u32>(entry))) {
        if (auto unpacked = disc::slz::unpack_block(*packed, 0, 0, nullptr)) image.bytes = std::move(*unpacked);
      }
    }
    if (!region::is_na()) {
      image.map = region::entry_map(entry, 0);
      if (image.map == nullptr) image.bytes.clear();  // no map: nothing can be read at NA addresses
      else region::prepare(image.map, image.bytes);
    }
    image.key = region::entry_key(entry, 0);
    it = images.emplace(entry, std::move(image)).first;
  }
  return region::image_byte(it->second.map, it->second.bytes, base, address, it->second.key.c_str());
}

}  // namespace ss::game
