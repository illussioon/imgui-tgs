// Built-in texture callbacks for Direct3D 9 / 9Ex (only needed for ImGui < 1.92 or a renderer
// without ImGuiBackendFlags_RendererHasTextures). Texture handle = IDirect3DTexture9*.
// Uses D3DPOOL_MANAGED (survives device Reset); falls back to DEFAULT+DYNAMIC on 9Ex devices.
//
//   auto cb = tgs::MakeDX9TextureCallbacks(device);
//   tgs::SetTextureCallbacks(&cb);
#pragma once
#include "../tgs_imgui.h"

struct IDirect3DDevice9;

namespace tgs {
TextureCallbacks MakeDX9TextureCallbacks(IDirect3DDevice9* device);
} // namespace tgs
