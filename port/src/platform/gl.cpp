#include "platform/gl.h"

namespace ss::gl {

#define SS_GL_DEFINE(ret, name, ...) PFN_##name name = nullptr;
SS_GL_FUNCTIONS(SS_GL_DEFINE)
#undef SS_GL_DEFINE

const char* load(void* (*get_proc)(const char*)) {
#define SS_GL_LOAD(ret, name, ...)                                     \
  name = reinterpret_cast<PFN_##name>(get_proc("gl" #name));           \
  if (!name) return "gl" #name;
  SS_GL_FUNCTIONS(SS_GL_LOAD)
#undef SS_GL_LOAD
  return nullptr;
}

}  // namespace ss::gl
