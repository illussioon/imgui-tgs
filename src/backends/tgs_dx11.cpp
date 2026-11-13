#include "tgs/backends/tgs_dx11.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <cstring>

namespace tgs {
namespace {

uint64_t Create(void* ud, int w, int h)
{
    ID3D11Device* dev = static_cast<ID3D11Device*>(ud);
    if (!dev)
        return 0;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = UINT(w);
    desc.Height = UINT(h);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&desc, nullptr, &tex)) || !tex)
        return 0;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = desc.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = dev->CreateShaderResourceView(tex, &sd, &srv);
    tex->Release(); // the SRV keeps the texture alive
    if (FAILED(hr) || !srv)
        return 0;
    return uint64_t(uintptr_t(srv));
}

bool Update(void* ud, uint64_t handle, const void* pixels, int w, int h, int pitch)
{
    ID3D11Device* dev = static_cast<ID3D11Device*>(ud);
    ID3D11ShaderResourceView* srv = reinterpret_cast<ID3D11ShaderResourceView*>(uintptr_t(handle));
    if (!dev || !srv)
        return false;
    ID3D11Resource* res = nullptr;
    srv->GetResource(&res);
    ID3D11DeviceContext* ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    bool ok = false;
    if (res && ctx)
    {
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(ctx->Map(res, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            const unsigned char* src = static_cast<const unsigned char*>(pixels);
            unsigned char* dst = static_cast<unsigned char*>(m.pData);
            for (int y = 0; y < h; ++y)
                std::memcpy(dst + size_t(y) * m.RowPitch, src + size_t(y) * pitch, size_t(w) * 4);
            ctx->Unmap(res, 0);
            ok = true;
        }
    }
    if (ctx)
        ctx->Release();
    if (res)
        res->Release();
    return ok;
}

void Destroy(void*, uint64_t handle)
{
    if (auto* srv = reinterpret_cast<ID3D11ShaderResourceView*>(uintptr_t(handle)))
        srv->Release();
}

} // namespace

TextureCallbacks MakeDX11TextureCallbacks(ID3D11Device* device)
{
    TextureCallbacks cb;
    cb.user_data = device;
    cb.format = PixelFormat::RGBA8;
    cb.create = &Create;
    cb.update = &Update;
    cb.destroy = &Destroy;
    cb.destroy_delay_frames = 2;
    return cb;
}

} // namespace tgs
