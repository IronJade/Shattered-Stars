// The programs' desktop identity (platform/app_icon.h): the icon compiled into them decodes,
// at the sizes and in the colours port/res/icon has, and the app metadata reaches SDL. No window.

#include <SDL3/SDL.h>

#include <cstring>
#include <string>

#include "platform/app_icon.h"
#include "test.h"

using namespace ss;

TEST(app_icon_embedded_images) {
  const auto images = platform::icon_images();
  CHECK_EQ(images.size(), 3u);
  const int sizes[] = {64, 128, 256};
  for (size_t i = 0; i < images.size() && i < 3; ++i) {
    CHECK_EQ(images[i].size, sizes[i]);
    CHECK(images[i].bytes > 1000);
    CHECK(images[i].png != nullptr && std::memcmp(images[i].png, "\x89PNG\r\n\x1a\n", 8) == 0);
  }
}

TEST(app_icon_surface) {
  std::string error;
  SDL_Surface* icon = platform::create_icon_surface(&error);
  CHECK(icon != nullptr);
  if (icon == nullptr) {
    std::fprintf(stderr, "  %s\n", error.c_str());
    return;
  }
  // The 100%-scale image is the 64 px one; 128 and 256 are its high-DPI images.
  CHECK_EQ(icon->w, 64);
  CHECK_EQ(icon->h, 64);
  CHECK(SDL_SurfaceHasAlternateImages(icon));
  int count = 0;
  SDL_Surface** images = SDL_GetSurfaceImages(icon, &count);
  CHECK_EQ(count, 3);
  for (int i = 0; images != nullptr && i < count; ++i) {
    SDL_Surface* s = images[i];
    const int n = s->w;
    CHECK_EQ(s->h, n);
    CHECK(n == 64 || n == 128 || n == 256);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    // Transparent outside the rounded tile's corner.
    CHECK(SDL_ReadSurfacePixel(s, 0, 0, &r, &g, &b, &a));
    CHECK_EQ(a, 0);
    // The midnight-blue tile near its top edge (between the star's points).
    CHECK(SDL_ReadSurfacePixel(s, n / 2 + n / 4, n / 10, &r, &g, &b, &a));
    CHECK(a == 255 && b > r && b > g && b > 60);
    // The gold star a little below the centre (inside one shard).
    CHECK(SDL_ReadSurfacePixel(s, n / 2 + n / 10, n / 2 + n / 6, &r, &g, &b, &a));
    CHECK(a == 255 && r > 200 && g > 140 && b < 120);
  }
  SDL_free(images);
  SDL_DestroySurface(icon);
}

TEST(app_icon_metadata) {
  CHECK(std::strcmp(platform::kGameAppId, platform::kSetupAppId) != 0);
  platform::set_app_metadata(platform::kSetupName, SS_VERSION, platform::kSetupAppId, false);
  const char* id = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING);
  CHECK(id != nullptr && std::strcmp(id, "io.github.IronJade.ShatteredStars.Setup") == 0);
  platform::set_app_metadata(platform::kGameName, SS_VERSION, platform::kGameAppId, true);
  id = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING);
  CHECK(id != nullptr && std::strcmp(id, "io.github.IronJade.ShatteredStars") == 0);
  const char* name = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING);
  CHECK(name != nullptr && std::strcmp(name, "Shattered Stars") == 0);
  const char* version = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING);
  CHECK(version != nullptr && std::strcmp(version, SS_VERSION) == 0);
  const char* type = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING);
  CHECK(type != nullptr && std::strcmp(type, "game") == 0);
}
