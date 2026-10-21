// Texture viewer: archive entry -> SLZ -> texture upload -> GPU -> window/PNG.
// Kept as a tool alongside the game (shattered-stars --viewer).

#include "tools/viewer.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL_keycode.h>

#include "disc/slz.h"
#include "game/common/texture.h"
#include "platform/window.h"
#include "psx/gpu.h"
#include "util/png.h"

namespace ss::tools {

namespace {

struct LoadedBlock {
  int index = 0;
  std::vector<u8> unpacked;
  game::TextureBlock texture;
  u16 tpage = 0, clut = 0;
  int x = 0, y = 0;  // VRAM position (halfwords)
};

struct LoadedEntry {
  int entry = -1;
  std::vector<LoadedBlock> blocks;
  psx::Gpu gpu;
};

// Offsets of SLZ blocks in an entry: the chain at offset 0, or blocks found by
// scanning when the entry is a container the game parses itself.
std::vector<size_t> block_offsets(std::span<const u8> data) {
  std::vector<size_t> out;
  if (auto h = disc::slz::header_at(data, 0)) {
    for (;;) {
      out.push_back(h->offset);
      if (h->next == 0) break;
      h = disc::slz::header_at(data, h->offset + h->next);
      if (!h) break;
    }
    return out;
  }
  for (size_t at = 4; at + 16 <= data.size(); at += 4)
    if (auto h = disc::slz::header_at(data, at); h && h->packed <= data.size() - at && h->unpacked < 0x200000)
      out.push_back(at);
  return out;
}

// Title program layout for entry #10 (title_intro_sequence's vram_upload_packed_block calls).
struct Placement {
  int x, y, clut_y;
};
const Placement kTitleLayout[] = {{0x280, 0, 0x1E0}, {0x140, 0, 0x1E1}, {0x300, 0x100, 0x1E2},
                                  {0x340, 0x100, 0x1E3}, {0x340, 0, 0x1E4}};

std::optional<LoadedEntry> load_entry(disc::Archive& archive, int entry) {
  auto data = archive.read_entry(entry);
  if (!data) return std::nullopt;
  LoadedEntry out;
  out.entry = entry;
  int shelf_x = 0, shelf_y = 0, shelf_h = 0, clut_row = 511;
  for (size_t off : block_offsets(*data)) {
    std::string error;
    std::vector<u8> unpacked;
    if (!disc::slz::unpack(*data, off, unpacked, &error)) continue;
    LoadedBlock b;
    b.index = static_cast<int>(out.blocks.size());
    b.unpacked = std::move(unpacked);
    auto t = game::parse_texture_block(b.unpacked);
    if (!t || t->width_px == 0 || t->height == 0 || t->height > 512 || t->vram_width > 1024) continue;
    b.texture = *t;
    // Placement: the title program's own positions for #10, else a shelf packer
    // that keeps every image on a 64-halfword texture-page boundary.
    int clut_y;
    if (entry == 10 && b.index < 5) {
      b.x = kTitleLayout[b.index].x;
      b.y = kTitleLayout[b.index].y;
      clut_y = kTitleLayout[b.index].clut_y;
    } else {
      const int w = (t->vram_width + 63) & ~63;
      if (shelf_x + w > 1024) shelf_x = 0, shelf_y += shelf_h, shelf_h = 0;
      if (shelf_y + t->height > clut_row) break;  // VRAM full
      b.x = shelf_x;
      b.y = shelf_y;
      shelf_x += w;
      shelf_h = std::max(shelf_h, t->height);
      clut_y = clut_row--;
    }
    b.clut = t->clut.empty() ? 0 : static_cast<u16>(clut_y << 6);
    // The same steps vram_upload_packed_block takes, applied to the parsed block.
    if (!t->clut.empty()) {
      std::vector<u16> row(256, 0);
      for (size_t i = 0; i < row.size() && i < t->clut.size(); ++i) row[i] = t->clut[i];
      game::upload_clut256(out.gpu, row.data(), 0, clut_y);
    }
    b.tpage = game::upload_texture(out.gpu, t->pixels.data(), t->depth, 0, b.x, b.y, t->width_px, t->height);
    out.blocks.push_back(std::move(b));
  }
  return out;
}

// Draws a block through the GPU as raw textured sprites, splitting at 256-texel
// texture-page boundaries the way PS1 code has to.
void draw_block(psx::Gpu& gpu, const LoadedBlock& b, int dst_x, int dst_y) {
  const int depth = b.texture.depth > 2 ? 2 : b.texture.depth;
  const int texels_per_hw = depth == 0 ? 4 : depth == 1 ? 2 : 1;
  for (int col = 0; col < b.texture.width_px; col += 256) {
    const int w = std::min(256, b.texture.width_px - col);
    const int src_hw_x = b.x + col / texels_per_hw;
    for (int row = 0; row < b.texture.height; row += 256) {
      const int h = std::min(256, b.texture.height - row);
      const int src_y = b.y + row;
      const u32 tpage = static_cast<u32>(depth << 7 | (src_y & 0x100) >> 4 | (src_hw_x & 0x3FF) >> 6);
      const u32 u0 = static_cast<u32>((src_hw_x & 63) * texels_per_hw), v0 = static_cast<u32>(src_y & 0xFF);
      const u32 e1[] = {0xE1000000u | tpage};
      gpu.gp0(e1);
      const u32 sprite[] = {0x65808080u, static_cast<u32>((dst_y + row) << 16 | ((dst_x + col) & 0xFFFF)),
                            static_cast<u32>(b.clut) << 16 | v0 << 8 | u0, static_cast<u32>(h << 16 | w)};
      gpu.gp0(sprite);
    }
  }
}

// Renders one block alone into a fresh GPU (texture at x >= 512) and returns RGBA.
std::vector<u32> render_preview(const LoadedBlock& b, int& w, int& h) {
  psx::Gpu gpu;
  LoadedBlock copy = b;
  copy.x = 512;
  copy.y = 0;
  if (!b.texture.clut.empty()) {
    std::vector<u16> row(256, 0);
    for (size_t i = 0; i < row.size() && i < b.texture.clut.size(); ++i) row[i] = b.texture.clut[i];
    copy.clut = game::upload_clut256(gpu, row.data(), 0, 511);
  }
  game::upload_texture(gpu, b.texture.pixels.data(), b.texture.depth, 0, copy.x, copy.y, b.texture.width_px,
                       b.texture.height);
  w = std::min(b.texture.width_px, 512);
  h = std::min(b.texture.height, 512);
  const u32 area[] = {0xE3000000u, 0xE4000000u | static_cast<u32>((h - 1) << 10 | (w - 1)), 0xE5000000u};
  for (u32 word : area) gpu.gp0(std::span<const u32>(&word, 1));
  draw_block(gpu, copy, 0, 0);
  gpu.display() = psx::DisplayMode{0, 0, w, h, false, true};
  std::vector<u32> rgba;
  gpu.display_to_rgba(rgba, w, h);
  return rgba;
}

}  // namespace

int dump_textures(disc::Archive& archive, int entry, const std::filesystem::path& dir) {
  auto loaded = load_entry(archive, entry);
  if (!loaded) {
    std::fprintf(stderr, "entry %d not present\n", entry);
    return 1;
  }
  std::filesystem::create_directories(dir);
  std::vector<u32> rgba;
  loaded->gpu.vram_to_rgba(rgba);
  util::write_png(dir / "vram.png", rgba, psx::kVramWidth, psx::kVramHeight);
  for (const auto& b : loaded->blocks) {
    int w, h;
    auto preview = render_preview(b, w, h);
    const auto name = dir / ("block" + std::to_string(b.index) + ".png");
    util::write_png(name, preview, w, h);
    std::printf("block %d: %s %dx%d at VRAM (%d,%d) tpage %#x clut %#x -> %s\n", b.index,
                b.texture.depth == 0 ? "4bpp" : b.texture.depth == 1 ? "8bpp" : "16bpp", b.texture.width_px,
                b.texture.height, b.x, b.y, b.tpage, b.clut, name.string().c_str());
  }
  return 0;
}

int run_viewer(disc::Archive& archive, int entry, int max_frames) {
  platform::Window window;
  std::string error;
  if (!window.create("Shattered Stars", 1280, 960, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  auto has_textures = [&](int n) {
    auto e = load_entry(archive, n);
    return e && !e->blocks.empty();
  };
  auto loaded = load_entry(archive, entry);
  int block = 0;
  bool vram_view = false;
  bool dirty = true;
  std::vector<u32> pixels;
  int w = 0, h = 0;
  for (int frame = 0; max_frames <= 0 || frame < max_frames; ++frame) {
    const bool running = window.pump([&](const platform::KeyEvent& k) {
      if (!k.down) return;
      int step = 0;
      if (k.key == SDLK_RIGHT) step = 1;
      if (k.key == SDLK_LEFT) step = -1;
      if (step) {
        for (int n = entry + step; n > 0 && n < static_cast<int>(archive.entry_count()); n += step)
          if (archive.has_entry(n) && has_textures(n)) {
            entry = n;
            loaded = load_entry(archive, entry);
            block = 0;
            break;
          }
      }
      if (loaded && !loaded->blocks.empty()) {
        const int count = static_cast<int>(loaded->blocks.size());
        if (k.key == SDLK_DOWN) block = (block + 1) % count;
        if (k.key == SDLK_UP) block = (block + count - 1) % count;
      }
      if (k.key == SDLK_V) vram_view = !vram_view;
      dirty = true;
    });
    if (!running) break;
    if (dirty && loaded) {
      if (vram_view || loaded->blocks.empty()) {
        loaded->gpu.vram_to_rgba(pixels);
        w = psx::kVramWidth;
        h = psx::kVramHeight;
      } else {
        pixels = render_preview(loaded->blocks[block], w, h);
      }
      char title[160];
      std::snprintf(title, sizeof title, "Shattered Stars - entry #%d - %s", entry,
                    vram_view ? "VRAM" : ("block " + std::to_string(block) + "/" +
                                          std::to_string(loaded->blocks.size())).c_str());
      window.set_title(title);
      dirty = false;
    }
    if (!pixels.empty()) window.present(pixels.data(), w, h, static_cast<float>(w) / h);
  }
  std::printf("viewer closed\n");
  return 0;
}



}  // namespace ss::tools
