#include "launcher/process.h"

#include <SDL3/SDL.h>

#include <string_view>
#include <system_error>

#include "launcher/config.h"

namespace ss::launcher {
namespace {

SDL_Process* create(const std::filesystem::path& program, const std::vector<std::string>& args, bool background,
                    std::string* error) {
  std::vector<std::string> argv_strings;
  argv_strings.push_back(path_utf8(program));
  argv_strings.insert(argv_strings.end(), args.begin(), args.end());
  std::vector<const char*> argv;
  for (const std::string& a : argv_strings) argv.push_back(a.c_str());
  argv.push_back(nullptr);

  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(argv.data()));
  if (background) {
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
  } else {
    // The child shares this program's terminal (its messages show where ours do).
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
  }
  SDL_Process* process = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  if (!process && error) *error = SDL_GetError();
  return process;
}

}  // namespace

std::filesystem::path sibling_program(const char* name) {
  const char* base = SDL_GetBasePath();
  if (!base) return {};
  // In an .app SDL's base path is Contents/Resources; the programs are in Contents/MacOS.
  const std::filesystem::path p = in_app_bundle() ? (utf8_path(base) / ".." / "MacOS" / name).lexically_normal()
                                                  : utf8_path(base) / name;
  std::error_code ec;
  return std::filesystem::is_regular_file(p, ec) ? p : std::filesystem::path();
}

bool in_app_bundle() {
#ifdef __APPLE__
  const char* base = SDL_GetBasePath();  // ".../Name.app/Contents/Resources/" in a bundle
  return base && std::string_view(base).ends_with(".app/Contents/Resources/");
#else
  return false;
#endif
}

int run_and_wait(const std::filesystem::path& program, const std::vector<std::string>& args, std::string* error) {
  SDL_Process* process = create(program, args, false, error);
  if (!process) return -1;
  int code = -1;
  if (!SDL_WaitProcess(process, true, &code) && error) *error = SDL_GetError();
  SDL_DestroyProcess(process);
  return code;
}

bool start_program(const std::filesystem::path& program, const std::vector<std::string>& args, std::string* error) {
  SDL_Process* process = create(program, args, true, error);
  if (!process) return false;
  SDL_DestroyProcess(process);  // the program keeps running
  return true;
}

}  // namespace ss::launcher
