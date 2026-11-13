// Built-in texture callbacks for Direct3D 11 (only needed for ImGui < 1.92 or a renderer
// without ImGuiBackendFlags_RendererHasTextures). Texture handle = ID3D11ShaderResourceView*.
//
//   auto cb = tgs::MakeDX11TextureCallbacks(device);
//   tgs::SetTextureCallbacks(&cb);
#pragma once
#include "../tgs_imgui.h"

struct ID3D11Device;

namespace tgs {
// device must outlive all textures (call tgs::Shutdown() before releasing it). Not AddRef'ed.
TextureCallbacks MakeDX11TextureCallbacks(ID3D11Device* device);
} // namespace tgs
