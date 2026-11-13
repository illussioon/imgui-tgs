#include "tgs/tgs.h"
#include "tgs/tgs_embed.h"

#include <rlottie.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef _MSC_VER
// rlottie's parser uses PathCanonicalizeW -> pull shlwapi automatically when linking the .lib by hand
#pragma comment(lib, "shlwapi.lib")
#endif
#endif

namespace tgs {
namespace {

void SetError(std::string* error, std::string text)
{
    if (error)
        *error = std::move(text);
}

std::string NextCacheKey()
{
    static std::atomic<unsigned long long> counter{0};
    return "tgs-parse-imgui#" + std::to_string(++counter);
}

void ConfigureRlottieOnce()
{
    static std::once_flag once;
    std::call_once(once, [] { rlottie::configureModelCacheSize(256); });
}

bool ReadWholeFile(std::ifstream& f, std::vector<char>& data, std::string* error)
{
    if (!f)
    {
        SetError(error, "cannot open file");
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff len = f.tellg();
    if (len <= 0)
    {
        SetError(error, "file is empty");
        return false;
    }
    if (len > std::streamoff(kDefaultMaxJsonSize))
    {
        SetError(error, "file too large");
        return false;
    }
    f.seekg(0, std::ios::beg);
    data.resize(size_t(len));
    f.read(data.data(), len);
    if (!f)
    {
        SetError(error, "read error");
        return false;
    }
    return true;
}

std::string StemOf(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = file.find_last_of('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}

// ARGB32 premultiplied (native uint32) -> straight RGBA/BGRA bytes
struct UnpremulTable
{
    uint32_t inv[256];
    UnpremulTable()
    {
        inv[0] = 0;
        for (uint32_t a = 1; a < 256; ++a)
            inv[a] = (255u << 16) / a;
    }
};
const UnpremulTable& Unpremul()
{
    static const UnpremulTable t;
    return t;
}

inline uint8_t UnpremulChannel(uint32_t c, uint32_t inv)
{
    uint32_t v = (c * inv + 0x8000u) >> 16;
    return uint8_t(v > 255 ? 255 : v);
}

void ConvertRow(const uint32_t* src, uint8_t* dst, int w, PixelFormat fmt)
{
    const uint32_t* inv = Unpremul().inv;
    for (int x = 0; x < w; ++x)
    {
        uint32_t px = src[x];
        uint32_t a = px >> 24;
        uint8_t* d = dst + x * 4;
        if (a == 0)
        {
            d[0] = d[1] = d[2] = d[3] = 0;
            continue;
        }
        uint32_t r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
        if (fmt == PixelFormat::PremulBGRA8)
        {
            d[0] = uint8_t(b); d[1] = uint8_t(g); d[2] = uint8_t(r); d[3] = uint8_t(a);
            continue;
        }
        uint8_t R, G, B;
        if (a == 255)
        {
            R = uint8_t(r); G = uint8_t(g); B = uint8_t(b);
        }
        else
        {
            R = UnpremulChannel(r, inv[a]);
            G = UnpremulChannel(g, inv[a]);
            B = UnpremulChannel(b, inv[a]);
        }
        if (fmt == PixelFormat::RGBA8)
        {
            d[0] = R; d[1] = G; d[2] = B;
        }
        else
        {
            d[0] = B; d[1] = G; d[2] = R;
        }
        d[3] = uint8_t(a);
    }
}

} // namespace

// ===========================================================================
// Animation
// ===========================================================================
Animation::~Animation() = default;

Animation::Ptr Animation::FromJson(std::string json, std::string* error, std::string name)
{
    ConfigureRlottieOnce();
    if (json.empty())
    {
        SetError(error, "empty json");
        return nullptr;
    }
    std::shared_ptr<Animation> a(new Animation());
    a->key_ = NextCacheKey();
    a->name_ = std::move(name);

    // Parse + validate once (model goes to rlottie cache under our unique key, renderers reuse it)
    auto probe = rlottie::Animation::loadFromData(json, a->key_, "", true);
    if (!probe)
    {
        SetError(error, "invalid Lottie JSON (rlottie failed to parse)");
        return nullptr;
    }
    size_t w = 0, h = 0;
    probe->size(w, h);
    a->info_.width = int(w);
    a->info_.height = int(h);
    a->info_.fps = probe->frameRate();
    // rlottie counts [ip, op] inclusive; Lottie layers are visible on [ip, op) so frame `op` is
    // usually empty -> exclude it, otherwise looped playback blinks once per cycle.
    const size_t total = probe->totalFrame();
    a->info_.frame_count = int(total > 1 ? total - 1 : 1);
    a->info_.duration = a->info_.frame_count / a->info_.fps;
    if (a->info_.frame_count <= 0 || a->info_.fps <= 0 || w == 0 || h == 0)
    {
        SetError(error, "Lottie has no frames / zero size");
        return nullptr;
    }
    a->json_ = std::move(json);
    return a;
}

Animation::Ptr Animation::FromMemory(const void* data, size_t size, std::string* error, std::string name)
{
    std::string json;
    if (!Decompress(data, size, json, error))
        return nullptr;
    return FromJson(std::move(json), error, std::move(name));
}

Animation::Ptr Animation::FromFile(const std::string& utf8_path, std::string* error)
{
    std::vector<char> data;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(), -1, nullptr, 0);
    std::wstring wpath(size_t(wlen > 0 ? wlen - 1 : 0), L'\0');
    if (wlen > 1)
        MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(), -1, &wpath[0], wlen);
    std::ifstream f(wpath.c_str(), std::ios::binary);
#else
    std::ifstream f(utf8_path, std::ios::binary);
#endif
    if (!ReadWholeFile(f, data, error))
    {
        if (error)
            *error += ": " + utf8_path;
        return nullptr;
    }
    return FromMemory(data.data(), data.size(), error, StemOf(utf8_path));
}

#ifdef _WIN32
Animation::Ptr Animation::FromFile(const std::wstring& path, std::string* error)
{
    std::vector<char> data;
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!ReadWholeFile(f, data, error))
        return nullptr;
    int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string u8(size_t(len > 0 ? len - 1 : 0), '\0');
    if (len > 1)
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &u8[0], len, nullptr, nullptr);
    return FromMemory(data.data(), data.size(), error, StemOf(u8));
}
#endif

namespace {
struct EmbeddedCache
{
    std::mutex                                     mutex;
    std::map<const EmbeddedAsset*, Animation::Ptr> items;
};
EmbeddedCache& GetEmbeddedCache()
{
    static EmbeddedCache c;
    return c;
}
} // namespace

Animation::Ptr Animation::FromEmbedded(const char* name, std::string* error)
{
    const EmbeddedAsset* asset = FindEmbedded(name);
    if (!asset)
    {
        SetError(error, std::string("embedded sticker not found: ") + (name ? name : "(null)"));
        return nullptr;
    }
    EmbeddedCache& c = GetEmbeddedCache();
    {
        std::lock_guard<std::mutex> lock(c.mutex);
        auto it = c.items.find(asset);
        if (it != c.items.end())
            return it->second;
    }
    Animation::Ptr anim = FromMemory(asset->data, asset->size, error, asset->name);
    if (!anim)
        return nullptr;
    std::lock_guard<std::mutex> lock(c.mutex);
    auto res = c.items.emplace(asset, anim);
    return res.first->second;
}

void ClearEmbeddedCache()
{
    EmbeddedCache& c = GetEmbeddedCache();
    std::lock_guard<std::mutex> lock(c.mutex);
    c.items.clear();
}

// ===========================================================================
// Renderer
// ===========================================================================
struct Renderer::Impl
{
    Animation::Ptr                     anim;
    std::unique_ptr<rlottie::Animation> lottie;
    int                                w = 0, h = 0;
    std::vector<uint32_t>              scratch;
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;
Renderer::Renderer(Renderer&&) noexcept = default;
Renderer& Renderer::operator=(Renderer&&) noexcept = default;

Renderer::Renderer(Animation::Ptr animation, int width, int height)
{
    if (!animation || width <= 0 || height <= 0 || width > 8192 || height > 8192)
        return;
    ConfigureRlottieOnce();
    auto lottie = rlottie::Animation::loadFromData(animation->Json(), animation->CacheKey(), "", true);
    if (!lottie)
        return;
    impl_.reset(new Impl());
    impl_->anim = std::move(animation);
    impl_->lottie = std::move(lottie);
    impl_->w = width;
    impl_->h = height;
}

bool Renderer::Valid() const { return impl_ && impl_->lottie; }
int  Renderer::Width() const { return impl_ ? impl_->w : 0; }
int  Renderer::Height() const { return impl_ ? impl_->h : 0; }

const Animation::Ptr& Renderer::GetAnimation() const
{
    static const Animation::Ptr empty;
    return impl_ ? impl_->anim : empty;
}

bool Renderer::Render(int frame, void* dst, int dst_pitch, PixelFormat format, bool keep_aspect)
{
    if (!Valid() || !dst || dst_pitch < impl_->w * 4)
        return false;
    const int total = impl_->anim->Info().frame_count;
    frame = std::max(0, std::min(frame, total - 1));

    const int w = impl_->w, h = impl_->h;
    const bool direct = format == PixelFormat::PremulBGRA8 && (reinterpret_cast<uintptr_t>(dst) % 4) == 0 && (dst_pitch % 4) == 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    const bool can_direct = false;
#else
    const bool can_direct = true;
#endif
    if (direct && can_direct)
    {
        uint8_t* d = static_cast<uint8_t*>(dst);
        for (int y = 0; y < h; ++y) // rlottie draws over existing content
            std::memset(d + size_t(y) * dst_pitch, 0, size_t(w) * 4);
        rlottie::Surface s(static_cast<uint32_t*>(dst), size_t(w), size_t(h), size_t(dst_pitch));
        impl_->lottie->renderSync(size_t(frame), s, keep_aspect);
        return true;
    }

    impl_->scratch.assign(size_t(w) * size_t(h), 0u); // rlottie draws over existing content
    rlottie::Surface s(impl_->scratch.data(), size_t(w), size_t(h), size_t(w) * 4);
    impl_->lottie->renderSync(size_t(frame), s, keep_aspect);

    uint8_t* out = static_cast<uint8_t*>(dst);
    for (int y = 0; y < h; ++y)
        ConvertRow(impl_->scratch.data() + size_t(y) * w, out + size_t(y) * dst_pitch, w, format);
    return true;
}

} // namespace tgs
