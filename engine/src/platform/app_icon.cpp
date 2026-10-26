#include "platform/app_icon.h"

#include <SDL3/SDL.h>

namespace ss::platform {

void set_app_metadata(const char* name, const char* version, const char* app_id, bool game) {
  SDL_SetAppMetadata(name, version, app_id);
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, game ? "game" : "application");
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, "https://github.com/IronJade/Shattered-Stars");
}

SDL_Surface* create_icon_surface(std::string* error) {
  SDL_Surface* base = nullptr;
  for (const IconImage& image : icon_images()) {
    SDL_Surface* surface = SDL_LoadPNG_IO(SDL_IOFromConstMem(image.png, image.bytes), true);
    if (surface == nullptr || surface->w != image.size || surface->h != image.size) {
      if (error) *error = "icon " + std::to_string(image.size) + " px: " + (surface ? "wrong size" : SDL_GetError());
      SDL_DestroySurface(surface);
      SDL_DestroySurface(base);
      return nullptr;
    }
    if (base == nullptr) {
      base = surface;
      continue;
    }
    const bool added = SDL_AddSurfaceAlternateImage(base, surface);
    SDL_DestroySurface(surface);  // the base holds its own reference
    if (!added) {
      if (error) *error = std::string("icon: ") + SDL_GetError();
      SDL_DestroySurface(base);
      return nullptr;
    }
  }
  if (base == nullptr && error) *error = "icon: no images";
  return base;
}

bool set_window_icon(SDL_Window* window, std::string* error) {
#ifdef _WIN32
  (void)window;
  (void)error;
  return true;
#else
  SDL_Surface* icon = create_icon_surface(error);
  if (icon == nullptr) return false;
  const bool ok = SDL_SetWindowIcon(window, icon);
  if (!ok && error) *error = std::string("SDL_SetWindowIcon: ") + SDL_GetError();
  SDL_DestroySurface(icon);
  return ok;
#endif
}

}  // namespace ss::platform
