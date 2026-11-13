#include "tgs/backends/tgs_opengl.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace tgs {
namespace {

uint64_t Create(void*, int w, int h)
{
    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex)
        return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, GLuint(prev));
    return uint64_t(tex);
}

bool Update(void*, uint64_t handle, const void* pixels, int w, int h, int pitch)
{
    GLint prev = 0, prev_row = 0, prev_align = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prev_row);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align);
    glBindTexture(GL_TEXTURE_2D, GLuint(handle));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, prev_row);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align);
    glBindTexture(GL_TEXTURE_2D, GLuint(prev));
    return true;
}

void Destroy(void*, uint64_t handle)
{
    GLuint tex = GLuint(handle);
    glDeleteTextures(1, &tex);
}

} // namespace

TextureCallbacks MakeOpenGLTextureCallbacks()
{
    TextureCallbacks cb;
    cb.format = PixelFormat::RGBA8;
    cb.create = &Create;
    cb.update = &Update;
    cb.destroy = &Destroy;
    cb.destroy_delay_frames = 2;
    return cb;
}

} // namespace tgs
