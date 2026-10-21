#include "game/common/program_image.h"

#include <map>
#include <vector>

#include "disc/slz.h"
#include "game/boot/system.h"

namespace ss::game {

const u8* program_data(int entry, u32 address) {
  static std::map<int, std::vector<u8>> images;
  auto it = images.find(entry);
  if (it == images.end()) {
    std::vector<u8> image;
    if (disc::Archive* archive = get_system()->loader.archive()) {
      if (auto packed = archive->read_entry(static_cast<u32>(entry))) {
        if (auto unpacked = disc::slz::unpack_block(*packed, 0, 0, nullptr)) image = std::move(*unpacked);
      }
    }
    it = images.emplace(entry, std::move(image)).first;
  }
  const u32 offset = address - kProgramBase;
  return offset < it->second.size() ? it->second.data() + offset : nullptr;
}

}  // namespace ss::game
