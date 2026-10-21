#pragma once

// The handful of OpenGL 3.3 core entry points the presenter uses, loaded through
// SDL so no system GL headers or loader library are required on any platform.

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define SS_GLAPI __stdcall
#else
#define SS_GLAPI
#endif

namespace ss::gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLchar = char;

constexpr GLenum TEXTURE_2D = 0x0DE1, TEXTURE0 = 0x84C0;
constexpr GLenum TEXTURE_MIN_FILTER = 0x2801, TEXTURE_MAG_FILTER = 0x2800, NEAREST = 0x2600, LINEAR = 0x2601;
constexpr GLenum TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803, CLAMP_TO_EDGE = 0x812F;
constexpr GLenum RGBA = 0x1908, RGBA8 = 0x8058, UNSIGNED_BYTE = 0x1401;
constexpr GLenum VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30;
constexpr GLenum COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82;
constexpr GLenum TRIANGLES = 0x0004, COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum UNPACK_ALIGNMENT = 0x0CF5;

#define SS_GL_FUNCTIONS(X)                                                                       \
  X(void, Viewport, GLint, GLint, GLsizei, GLsizei)                                              \
  X(void, ClearColor, GLfloat, GLfloat, GLfloat, GLfloat)                                         \
  X(void, Clear, GLbitfield)                                                                      \
  X(void, GenTextures, GLsizei, GLuint*)                                                          \
  X(void, DeleteTextures, GLsizei, const GLuint*)                                                 \
  X(void, BindTexture, GLenum, GLuint)                                                            \
  X(void, ActiveTexture, GLenum)                                                                  \
  X(void, TexParameteri, GLenum, GLenum, GLint)                                                   \
  X(void, TexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
  X(void, TexSubImage2D, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) \
  X(void, PixelStorei, GLenum, GLint)                                                             \
  X(void, DrawArrays, GLenum, GLint, GLsizei)                                                     \
  X(void, GenVertexArrays, GLsizei, GLuint*)                                                      \
  X(void, BindVertexArray, GLuint)                                                                \
  X(void, DeleteVertexArrays, GLsizei, const GLuint*)                                             \
  X(GLuint, CreateShader, GLenum)                                                                 \
  X(void, ShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*)                      \
  X(void, CompileShader, GLuint)                                                                  \
  X(void, GetShaderiv, GLuint, GLenum, GLint*)                                                    \
  X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                                   \
  X(void, DeleteShader, GLuint)                                                                   \
  X(GLuint, CreateProgram)                                                                        \
  X(void, AttachShader, GLuint, GLuint)                                                           \
  X(void, LinkProgram, GLuint)                                                                    \
  X(void, GetProgramiv, GLuint, GLenum, GLint*)                                                   \
  X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                                  \
  X(void, UseProgram, GLuint)                                                                     \
  X(void, DeleteProgram, GLuint)                                                                  \
  X(GLint, GetUniformLocation, GLuint, const GLchar*)                                             \
  X(void, Uniform1i, GLint, GLint)

#define SS_GL_DECLARE(ret, name, ...) \
  using PFN_##name = ret(SS_GLAPI*)(__VA_ARGS__); \
  extern PFN_##name name;
SS_GL_FUNCTIONS(SS_GL_DECLARE)
#undef SS_GL_DECLARE

// Loads every function above; returns the name of the first one missing, or nullptr.
const char* load(void* (*get_proc)(const char*));

}  // namespace ss::gl
