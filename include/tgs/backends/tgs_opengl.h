// Built-in texture callbacks for OpenGL (GL 1.1 calls only — works with any context: 2.x, 3.x core, ES-like
// desktop drivers). Texture handle = GLuint. Requires a current GL context on the calling thread.
// Link opengl32 (Windows) / GL (Linux).
//
//   auto cb = tgs::MakeOpenGLTextureCallbacks();
//   tgs::SetTextureCallbacks(&cb);
#pragma once
#include "../tgs_imgui.h"

namespace tgs {
TextureCallbacks MakeOpenGLTextureCallbacks();
} // namespace tgs
