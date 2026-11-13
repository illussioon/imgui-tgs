// tgs-parse-imgui — Dear ImGui layer: Player + immediate-mode Sticker widgets.
//
// Texture upload, two modes (auto-selected):
//   1) Dear ImGui >= 1.92 with a backend that sets ImGuiBackendFlags_RendererHasTextures
//      (all official backends: DX9/10/11/12, OpenGL2/3, Vulkan, Metal, SDL...).
//      Nothing to configure — textures go through ImTextureData like the font atlas.
//   2) Older ImGui / custom renderer / game hook without texture support:
//      call tgs::SetTextureCallbacks(...) once — ready-made callbacks for DX11 / DX9 / OpenGL
//      are in <tgs/backends/tgs_dx11.h>, <tgs/backends/tgs_dx9.h>, <tgs/backends/tgs_opengl.h>.
//
// Shutdown order:
//   ImGui_ImplXXX_Shutdown();  ImGui_ImplWin32_Shutdown();
//   tgs::Shutdown();           // <- here
//   ImGui::DestroyContext();   // then release your D3D device
//
// Threading: call everything from the thread that runs ImGui.
#pragma once

#include "tgs.h"
#include "imgui.h"

#include <memory>

#if defined(IMGUI_HAS_TEXTURES) && defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19200
#define TGS_IMGUI_HAS_TEXTURES 1
#else
#define TGS_IMGUI_HAS_TEXTURES 0
#endif

namespace tgs {

// ---------------------------------------------------------------------------
// Texture backend
// ---------------------------------------------------------------------------
struct TextureCallbacks
{
    void*       user_data = nullptr;
    PixelFormat format    = PixelFormat::RGBA8;  // what `update` receives (straight alpha)

    // Create an empty width x height texture, return its handle (what ImGui::Image expects as ImTextureID). 0 = failure.
    uint64_t (*create)(void* user_data, int width, int height) = nullptr;
    // Upload a whole frame. pitch = bytes per row.
    bool     (*update)(void* user_data, uint64_t texture, const void* pixels, int width, int height, int pitch) = nullptr;
    // Release. Called `destroy_delay_frames` ImGui frames after the texture was last used.
    void     (*destroy)(void* user_data, uint64_t texture) = nullptr;

    int destroy_delay_frames = 3;
};

enum class TextureMode
{
    None,          // nothing available: widgets draw a placeholder
    ImGuiManaged,  // ImGui >= 1.92 ImTextureData + RendererHasTextures
    Callbacks,     // user / built-in callbacks
};

// nullptr -> back to automatic mode. Existing players re-create their textures on next draw.
void              SetTextureCallbacks(const TextureCallbacks* callbacks);
TextureMode       GetTextureMode();
const char*       GetTextureModeName();

// ---------------------------------------------------------------------------
// Player — one animation instance with its own timeline + texture.
// ---------------------------------------------------------------------------
struct PlayerOptions
{
    bool  autoplay       = true;
    bool  loop           = true;
    float speed          = 1.0f;   // negative = reverse
    bool  auto_update    = true;   // Draw*() advances time by io.DeltaTime once per ImGui frame
    bool  cache_frames   = false;  // keep every rendered frame in RAM: zero CPU after 1st loop (frames*w*h*4 bytes)
    int   max_resolution = 1024;   // clamp of the texture side in pixels
};

class Player
{
public:
    Player();
    explicit Player(Animation::Ptr animation, const PlayerOptions& options = PlayerOptions());
    ~Player();
    Player(Player&&) noexcept;
    Player& operator=(Player&&) noexcept;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    void                  SetAnimation(Animation::Ptr animation, bool restart = true);
    const Animation::Ptr& GetAnimation() const;
    bool                  Valid() const;

    PlayerOptions&        Options();
    const PlayerOptions&  Options() const;

    // --- playback ------------------------------------------------------------
    void  Play();               // resumes; restarts if a non-looping animation has finished
    void  Pause();
    void  Stop();               // pause + rewind to frame 0
    void  TogglePlay();
    bool  IsPlaying() const;
    bool  IsFinished() const;   // non-looping animation reached its end

    void  SetLoop(bool loop);
    bool  GetLoop() const;
    void  SetSpeed(float speed);
    float GetSpeed() const;

    void  SetFrame(int frame);
    int   GetFrame() const;
    int   GetFrameCount() const;
    void  SetTime(double seconds);
    double GetTime() const;
    void  SetProgress(float t01);
    float GetProgress() const;

    // Manual clock (if options.auto_update == false, or to fast-forward).
    void  Update(float delta_seconds);

    // --- rendering -----------------------------------------------------------
    // Fixed texture resolution (0,0 = derive from the drawn size * framebuffer scale).
    void  SetResolution(int width, int height);

    // ImGui item (respects layout, clipping: off-screen stickers don't render).
    void  Draw(const ImVec2& size, const ImVec4& tint = ImVec4(1, 1, 1, 1), const ImVec4& bg = ImVec4(0, 0, 0, 0));
    // Clickable; returns true when pressed. Highlights on hover like ImageButton.
    bool  DrawButton(const char* str_id, const ImVec2& size, const ImVec4& tint = ImVec4(1, 1, 1, 1));
    // Raw draw-list version — overlays: DrawTo(ImGui::GetForegroundDrawList(), a, b)
    void  DrawTo(ImDrawList* draw_list, const ImVec2& p_min, const ImVec2& p_max, ImU32 col = IM_COL32_WHITE);

    // Low level: make sure the texture holds the current frame at the given pixel size.
    bool        Prepare(int pixel_width, int pixel_height);
    ImTextureID GetTexID() const;   // can be 0 during the first frame in ImGuiManaged mode
#if TGS_IMGUI_HAS_TEXTURES
    ImTextureRef GetTexRef() const; // always valid after Prepare() — prefer this on ImGui >= 1.92
#endif
    void  ReleaseTexture();         // free GPU memory now (recreated on next draw)

    struct Impl;
private:
    friend struct PlayerAccess;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// Immediate-mode widgets. A Player per (ImGui ID) is created/cached internally and
// garbage-collected when not drawn for a while.
// ---------------------------------------------------------------------------
namespace ui {

struct StickerOptions
{
    float  speed         = 1.0f;
    bool   loop          = true;
    bool   paused        = false;
    bool   play_on_hover = false;  // Telegram-like: still until hovered, finishes the loop after
    bool   cache_frames  = false;
    ImVec4 tint          = ImVec4(1, 1, 1, 1);
    ImVec4 bg            = ImVec4(0, 0, 0, 0);
};

// Embedded sticker by name (tgs_embed). Returns false if not found (draws a placeholder).
bool Sticker(const char* embedded_name, const ImVec2& size, const StickerOptions& options = StickerOptions());
// Any loaded animation; str_id identifies the widget.
bool Sticker(const char* str_id, const Animation::Ptr& animation, const ImVec2& size, const StickerOptions& options = StickerOptions());

// Returns true when clicked.
bool StickerButton(const char* embedded_name, const ImVec2& size, const StickerOptions& options = StickerOptions());
bool StickerButton(const char* str_id, const Animation::Ptr& animation, const ImVec2& size, const StickerOptions& options = StickerOptions());

// Cached player behind a widget drawn with the same str_id in the current ID scope (nullptr if none yet).
Player* GetStickerPlayer(const char* str_id);

} // namespace ui

// ---------------------------------------------------------------------------
// Housekeeping
// ---------------------------------------------------------------------------
// Optional: called automatically by the first tgs draw call of each ImGui frame.
void NewFrame();
// Drop cached widget players not drawn for `max_unused_frames` frames (auto: 120).
void SetStickerCacheLifetime(int max_unused_frames);
void GarbageCollect();
// See the shutdown order at the top of this file.
void Shutdown();

struct Stats
{
    int    players            = 0;  // alive Player objects (incl. cached widgets)
    int    cached_widgets     = 0;
    int    textures           = 0;  // alive textures
    int    textures_dying     = 0;  // waiting for the backend to release them
    int    frames_rendered    = 0;  // rasterized during the last finished ImGui frame
    double render_ms          = 0;  // CPU time spent rasterizing during the last finished frame
    size_t frame_cache_bytes  = 0;
};
Stats GetStats();

} // namespace tgs
