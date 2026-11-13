// tgs-parse-imgui — Telegram .tgs (gzip Lottie) stickers for Dear ImGui
// Core API: decompression, loading, software rendering. No ImGui dependency here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#define TGS_VERSION_MAJOR 1
#define TGS_VERSION_MINOR 0
#define TGS_VERSION_PATCH 0
#define TGS_VERSION_STRING "1.0.0"

namespace tgs {

// Upper bound for decompressed Lottie JSON (protection from gzip bombs).
// Real Telegram stickers are < 1 MB of JSON.
constexpr size_t kDefaultMaxJsonSize = size_t(32) << 20; // 32 MB

// --------------------------------------------------------------------------
// Decompression
// --------------------------------------------------------------------------

// Input formats (auto-detected):
//   - gzip   (.tgs, 1F 8B ...)       -> header + CRC32 + ISIZE are verified
//   - zlib   (78 xx ...)             -> Adler-32 verified
//   - plain Lottie JSON ('{' ...)   -> copied as is
bool Decompress(const void* data, size_t size, std::string& out_json,
                std::string* error = nullptr, size_t max_output = kDefaultMaxJsonSize);

bool IsGzip(const void* data, size_t size);

// --------------------------------------------------------------------------
// Pixel formats for Renderer::Render
// --------------------------------------------------------------------------
enum class PixelFormat : uint8_t
{
    RGBA8,         // bytes R,G,B,A — straight alpha   (ImGui textures, DX11 R8G8B8A8, GL_RGBA)
    BGRA8,         // bytes B,G,R,A — straight alpha   (DX9 A8R8G8B8, DXGI B8G8R8A8)
    PremulBGRA8,   // bytes B,G,R,A — premultiplied    (raw rlottie output, fastest)
};

struct AnimationInfo
{
    int    width       = 0;   // composition size from the file (tgs: 512x512)
    int    height      = 0;
    double fps         = 0;   // tgs: 30 or 60
    int    frame_count = 0;
    double duration    = 0;   // seconds
};

// --------------------------------------------------------------------------
// Animation — immutable, parsed & validated Lottie. Share it freely (shared_ptr).
// --------------------------------------------------------------------------
class Animation
{
public:
    using Ptr = std::shared_ptr<const Animation>;

    // .tgs / zlib / json in memory. Data is copied (decompressed), you can free it after the call.
    static Ptr FromMemory(const void* data, size_t size, std::string* error = nullptr, std::string name = {});

    // UTF-8 path.
    static Ptr FromFile(const std::string& utf8_path, std::string* error = nullptr);
#ifdef _WIN32
    static Ptr FromFile(const std::wstring& path, std::string* error = nullptr);
#endif

    // Sticker embedded at compile time with tgs_embed(). Result is cached per name.
    static Ptr FromEmbedded(const char* name, std::string* error = nullptr);

    // Plain Lottie JSON.
    static Ptr FromJson(std::string json, std::string* error = nullptr, std::string name = {});

    const AnimationInfo& Info() const { return info_; }
    const std::string&   Name() const { return name_; }
    const std::string&   Json() const { return json_; }
    const std::string&   CacheKey() const { return key_; }

    Animation(const Animation&) = delete;
    Animation& operator=(const Animation&) = delete;
    ~Animation();

private:
    Animation() = default;
    AnimationInfo info_;
    std::string   name_;
    std::string   json_;
    std::string   key_;
    friend class Renderer;
};

// Drop cached FromEmbedded() animations (players keep their own references).
void ClearEmbeddedCache();

// --------------------------------------------------------------------------
// Renderer — CPU rasterizer bound to one output size. Not thread-safe per instance;
// different instances may be used from different threads.
// --------------------------------------------------------------------------
class Renderer
{
public:
    Renderer();
    Renderer(Animation::Ptr animation, int width, int height);
    ~Renderer();
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Valid() const;
    int  Width() const;
    int  Height() const;
    const Animation::Ptr& GetAnimation() const;

    // Renders frame (clamped to [0, frame_count-1]) into dst (width*height pixels, dst_pitch bytes per row).
    // keep_aspect: letterbox if output aspect differs from the composition.
    bool Render(int frame, void* dst, int dst_pitch, PixelFormat format = PixelFormat::RGBA8, bool keep_aspect = true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tgs
