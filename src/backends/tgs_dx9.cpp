#include "tgs/backends/tgs_dx9.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d9.h>
#include <cstring>

namespace tgs {
namespace {

uint64_t Create(void* ud, int w, int h)
{
    IDirect3DDevice9* dev = static_cast<IDirect3DDevice9*>(ud);
    if (!dev)
        return 0;
    IDirect3DTexture9* tex = nullptr;
    // MANAGED survives IDirect3DDevice9::Reset; not allowed on D3D9Ex -> DEFAULT + DYNAMIC
    if (FAILED(dev->CreateTexture(UINT(w), UINT(h), 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex)
    {
        tex = nullptr;
        if (FAILED(dev->CreateTexture(UINT(w), UINT(h), 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr)) || !tex)
            return 0;
    }
    return uint64_t(uintptr_t(tex));
}

bool Update(void*, uint64_t handle, const void* pixels, int w, int h, int pitch)
{
    IDirect3DTexture9* tex = reinterpret_cast<IDirect3DTexture9*>(uintptr_t(handle));
    if (!tex)
        return false;
    D3DSURFACE_DESC desc = {};
    tex->GetLevelDesc(0, &desc);
    const DWORD flags = (desc.Usage & D3DUSAGE_DYNAMIC) ? D3DLOCK_DISCARD : 0;
    D3DLOCKED_RECT lr = {};
    if (FAILED(tex->LockRect(0, &lr, nullptr, flags)))
        return false;
    const unsigned char* src = static_cast<const unsigned char*>(pixels);
    unsigned char* dst = static_cast<unsigned char*>(lr.pBits);
    for (int y = 0; y < h; ++y)
        std::memcpy(dst + size_t(y) * size_t(lr.Pitch), src + size_t(y) * pitch, size_t(w) * 4);
    tex->UnlockRect(0);
    return true;
}

void Destroy(void*, uint64_t handle)
{
    if (auto* tex = reinterpret_cast<IDirect3DTexture9*>(uintptr_t(handle)))
        tex->Release();
}

} // namespace

TextureCallbacks MakeDX9TextureCallbacks(IDirect3DDevice9* device)
{
    TextureCallbacks cb;
    cb.user_data = device;
    cb.format = PixelFormat::BGRA8; // D3DFMT_A8R8G8B8 = B,G,R,A in memory
    cb.create = &Create;
    cb.update = &Update;
    cb.destroy = &Destroy;
    cb.destroy_delay_frames = 2;
    return cb;
}

} // namespace tgs
