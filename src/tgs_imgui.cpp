#include "tgs/tgs_imgui.h"

#if TGS_IMGUI_HAS_TEXTURES
#include "imgui_internal.h"   // RegisterUserTexture / UnregisterUserTexture
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace tgs {

// ===========================================================================
// Global state (heap singleton, never destroyed -> safe from static-destruction order
// issues when the DLL is unloaded while user objects still exist)
// ===========================================================================
namespace {

struct DeadTexture
{
    TextureMode      mode = TextureMode::None;
    uint64_t         handle = 0;
    TextureCallbacks cb;
#if TGS_IMGUI_HAS_TEXTURES
    ImTextureData*   data = nullptr;
#endif
    int              release_frame = 0;
};

struct CachedWidget
{
    std::unique_ptr<Player> player;
    int                     last_frame = 0;
    bool                    was_paused = false;
};

struct Globals
{
    bool                                      has_cb = false;
    TextureCallbacks                          cb;
    int                                       generation = 0;
    std::vector<Player::Impl*>                players;
    std::vector<DeadTexture>                  graveyard;
    std::unordered_map<ImGuiID, CachedWidget> widgets;
    int                                       widget_lifetime = 120;
    int                                       last_maint_frame = -1;
    int                                       textures = 0;

    int    stat_frame = -1;
    int    rendered_cur = 0, rendered_last = 0;
    double ms_cur = 0, ms_last = 0;
};

Globals& G()
{
    static Globals* g = new Globals();
    return *g;
}

inline bool HasContext() { return ImGui::GetCurrentContext() != nullptr; }
inline int  CurFrame() { return HasContext() ? ImGui::GetFrameCount() : 0; }

TextureMode CurrentMode()
{
    if (G().has_cb)
        return TextureMode::Callbacks;
#if TGS_IMGUI_HAS_TEXTURES
    if (HasContext() && (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures))
        return TextureMode::ImGuiManaged;
#endif
    return TextureMode::None;
}

void ProcessGraveyard(bool force)
{
    Globals& g = G();
    const int f = CurFrame();
    for (size_t i = 0; i < g.graveyard.size();)
    {
        DeadTexture& e = g.graveyard[i];
        bool remove = false;
        if (e.mode == TextureMode::Callbacks)
        {
            if (force || !HasContext() || f - e.release_frame >= std::max(0, e.cb.destroy_delay_frames))
            {
                if (e.cb.destroy && e.handle)
                    e.cb.destroy(e.cb.user_data, e.handle);
                remove = true;
            }
        }
#if TGS_IMGUI_HAS_TEXTURES
        else if (e.mode == TextureMode::ImGuiManaged && e.data)
        {
            ImTextureData* t = e.data;
            const bool gpu_free = t->Status == ImTextureStatus_Destroyed ||
                                  (t->TexID == ImTextureID_Invalid && t->BackendUserData == nullptr);
            if (gpu_free)
            {
                if (HasContext())
                {
                    ImGui::UnregisterUserTexture(t);
                    ImGui::GetPlatformIO().Textures.find_erase(t);
                }
                IM_DELETE(t);
                remove = true;
            }
            else if (force)
            {
                // Backend still owns a GPU texture (tgs::Shutdown() called before ImGui_ImplXXX_Shutdown()).
                // Leave it registered so the backend frees it on its shutdown; drop the pixels.
                t->DestroyPixels();
                t->WantDestroyNextFrame = true;
                t->Status = ImTextureStatus_WantDestroy;
                t->UnusedFrames = 1000;
                remove = true; // tiny struct intentionally leaked
            }
            else if (f > e.release_frame)
            {
                // Not referenced by draw data any more -> ask the backend to destroy it.
                if (t->Status != ImTextureStatus_WantDestroy)
                {
                    t->WantDestroyNextFrame = true;
                    t->Status = ImTextureStatus_WantDestroy;
                }
                t->UnusedFrames = std::max(t->UnusedFrames + 1, f - e.release_frame);
            }
        }
#endif
        else
        {
            remove = true;
        }

        if (remove)
        {
            g.graveyard[i] = g.graveyard.back();
            g.graveyard.pop_back();
        }
        else
        {
            ++i;
        }
    }
}

void StatRoll()
{
    Globals& g = G();
    int f = CurFrame();
    if (f != g.stat_frame)
    {
        g.rendered_last = g.rendered_cur;
        g.ms_last = g.ms_cur;
        g.rendered_cur = 0;
        g.ms_cur = 0;
        g.stat_frame = f;
    }
}

ImVec2 ResolveSize(ImVec2 size, const Animation* anim)
{
    float aspect = 1.0f;
    if (anim && anim->Info().width > 0)
        aspect = float(anim->Info().height) / float(anim->Info().width);
    if (size.x <= 0.0f)
        size.x = std::max(1.0f, ImGui::GetContentRegionAvail().x + size.x);
    if (size.y <= 0.0f)
        size.y = std::max(1.0f, size.x * aspect);
    return size;
}

void DrawPlaceholder(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    const ImU32 col = IM_COL32(255, 90, 90, 160);
    dl->AddRect(a, b, col);
    dl->AddLine(a, b, col);
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), col);
}

} // namespace

// ===========================================================================
// Player::Impl
// ===========================================================================
struct Player::Impl
{
    Animation::Ptr anim;
    PlayerOptions  opt;

    bool   playing = false;
    bool   finished = false;
    bool   stop_at_loop_end = false;
    double time = 0;
    int    last_update_frame = -1;
    int    fixed_w = 0, fixed_h = 0;

    Renderer                          renderer;
    std::vector<uint8_t>              scratch;
    std::vector<std::vector<uint8_t>> cache;
    PixelFormat                       cache_fmt = PixelFormat::RGBA8;

    // texture
    TextureMode      tex_mode = TextureMode::None;
    TextureCallbacks tex_cb;
    int              tex_gen = -1;
    int              tex_w = 0, tex_h = 0;
    uint64_t         tex_handle = 0;
#if TGS_IMGUI_HAS_TEXTURES
    ImTextureData*   tex_data = nullptr;
#endif
    int              uploaded_frame = -1;

    // one player drawn at several sizes in a frame -> texture = largest size of the previous frame
    int req_frame = -1, cur_max_w = 0, cur_max_h = 0, prev_max_w = 0, prev_max_h = 0;

    Impl() { G().players.push_back(this); }
    ~Impl()
    {
        ReleaseTex();
        auto& v = G().players;
        v.erase(std::remove(v.begin(), v.end(), this), v.end());
    }

    bool HasTex() const
    {
#if TGS_IMGUI_HAS_TEXTURES
        if (tex_data)
            return true;
#endif
        return tex_handle != 0;
    }

    int FrameCount() const { return anim ? anim->Info().frame_count : 0; }
    double Duration() const { return anim ? anim->Info().duration : 0.0; }

    int Frame() const
    {
        if (!anim)
            return 0;
        int f = int(std::floor(time * anim->Info().fps));
        return std::max(0, std::min(f, FrameCount() - 1));
    }

    void SetTimeClamped(double t)
    {
        const double d = Duration();
        if (d <= 0)
        {
            time = 0;
            return;
        }
        time = std::max(0.0, std::min(t, d - 0.5 / anim->Info().fps));
    }

    void Restart()
    {
        time = opt.speed < 0 && anim ? Duration() - 0.5 / anim->Info().fps : 0.0;
        finished = false;
        stop_at_loop_end = false;
        playing = opt.autoplay;
    }

    void Update(float dt)
    {
        if (!anim || !playing || dt <= 0.0f)
            return;
        const double d = Duration();
        if (d <= 0)
            return;
        time += double(dt) * double(opt.speed);
        if (time >= 0.0 && time < d)
            return;
        if (opt.loop)
        {
            if (stop_at_loop_end)
            {
                time = 0;
                playing = false;
                stop_at_loop_end = false;
                return;
            }
            time = std::fmod(time, d);
            if (time < 0)
                time += d;
        }
        else
        {
            SetTimeClamped(time < 0 ? 0.0 : d);
            playing = false;
            finished = true;
        }
    }

    void AutoUpdate()
    {
        if (!opt.auto_update || !HasContext())
            return;
        const int f = ImGui::GetFrameCount();
        if (f == last_update_frame)
            return;
        if (last_update_frame >= 0)
            Update(ImGui::GetIO().DeltaTime);
        last_update_frame = f;
    }

    void ReleaseTex()
    {
        if (!HasTex())
            return;
        DeadTexture d;
        d.mode = tex_mode;
        d.handle = tex_handle;
        d.cb = tex_cb;
#if TGS_IMGUI_HAS_TEXTURES
        d.data = tex_data;
        tex_data = nullptr;
#endif
        d.release_frame = CurFrame();
        G().graveyard.push_back(d);
        G().textures--;
        tex_handle = 0;
        tex_mode = TextureMode::None;
        tex_w = tex_h = 0;
        uploaded_frame = -1;
        if (!HasContext())
            ProcessGraveyard(true);
    }

    bool CreateTex(TextureMode mode, int w, int h)
    {
        if (mode == TextureMode::Callbacks)
        {
            const TextureCallbacks& cb = G().cb;
            if (!cb.create || !cb.update)
                return false;
            uint64_t handle = cb.create(cb.user_data, w, h);
            if (!handle)
                return false;
            tex_handle = handle;
            tex_cb = cb;
            tex_gen = G().generation;
        }
#if TGS_IMGUI_HAS_TEXTURES
        else if (mode == TextureMode::ImGuiManaged)
        {
            ImTextureData* t = IM_NEW(ImTextureData)();
            t->Create(ImTextureFormat_RGBA32, w, h);
            t->UseColors = true;
            t->Status = ImTextureStatus_WantCreate; // 1.92.0's Create() leaves Status untouched
#if IMGUI_VERSION_NUM < 19220
            t->RefCount = 1; // 1.92.0/1.92.1: RegisterUserTexture() expects the caller to set it
#endif
            ImGui::RegisterUserTexture(t);
            tex_data = t;
            tex_gen = G().generation;
        }
#endif
        else
        {
            return false;
        }
        tex_mode = mode;
        tex_w = w;
        tex_h = h;
        uploaded_frame = -1;
        G().textures++;
        return true;
    }

    // Rasterize `frame` (or take it from the frame cache). Writes into `dst` when not cached.
    const uint8_t* FramePixels(int frame, PixelFormat fmt, uint8_t* dst)
    {
        const int w = renderer.Width(), h = renderer.Height();
        const size_t bytes = size_t(w) * size_t(h) * 4;
        uint8_t* target = dst;
        if (opt.cache_frames)
        {
            if (cache_fmt != fmt || cache.size() != size_t(FrameCount()))
            {
                cache.clear();
                cache.resize(size_t(FrameCount()));
                cache_fmt = fmt;
            }
            std::vector<uint8_t>& slot = cache[size_t(frame)];
            if (slot.size() == bytes)
                return slot.data();
            slot.resize(bytes);
            target = slot.data();
        }
        else if (!cache.empty())
        {
            cache.clear();
            cache.shrink_to_fit();
        }
        if (!target)
        {
            scratch.resize(bytes);
            target = scratch.data();
        }
        StatRoll();
        auto t0 = std::chrono::steady_clock::now();
        renderer.Render(frame, target, w * 4, fmt);
        G().ms_cur += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        G().rendered_cur++;
        return target;
    }

    void Upload(int frame)
    {
        const int w = tex_w, h = tex_h;
        if (tex_mode == TextureMode::Callbacks)
        {
            const uint8_t* px = FramePixels(frame, tex_cb.format, nullptr);
            tex_cb.update(tex_cb.user_data, tex_handle, px, w, h, w * 4);
        }
#if TGS_IMGUI_HAS_TEXTURES
        else if (tex_mode == TextureMode::ImGuiManaged)
        {
            ImTextureData* t = tex_data;
            uint8_t* dst = static_cast<uint8_t*>(t->GetPixels());
            const uint8_t* px = FramePixels(frame, PixelFormat::RGBA8, dst);
            if (px != dst)
                std::memcpy(dst, px, size_t(w) * size_t(h) * 4);

            if (t->Status == ImTextureStatus_Destroyed) // backend dropped it (device reset...) -> recreate
                t->Status = ImTextureStatus_WantCreate;
            if (t->Status == ImTextureStatus_OK || t->Status == ImTextureStatus_WantUpdates)
            {
                ImTextureRect r;
                r.x = 0;
                r.y = 0;
                r.w = (unsigned short)w;
                r.h = (unsigned short)h;
                t->Updates.resize(0);
                t->Updates.push_back(r);
                t->UpdateRect = r;
                t->UsedRect = r;
                t->Status = ImTextureStatus_WantUpdates;
            }
            // WantCreate: backend uploads the whole buffer on creation anyway
        }
#endif
    }

    bool Prepare(int pw, int ph)
    {
        if (!anim)
            return false;
        NewFrame();
        const TextureMode mode = CurrentMode();
        if (mode == TextureMode::None)
            return false;

        const int f = CurFrame();
        if (f != req_frame)
        {
            const bool consecutive = req_frame == f - 1;
            prev_max_w = consecutive ? cur_max_w : 0;
            prev_max_h = consecutive ? cur_max_h : 0;
            cur_max_w = cur_max_h = 0;
            req_frame = f;
        }
        cur_max_w = std::max(cur_max_w, pw);
        cur_max_h = std::max(cur_max_h, ph);

        const int maxr = std::max(1, std::min(opt.max_resolution, 4096));
        int w = fixed_w > 0 ? fixed_w : std::max(pw, prev_max_w);
        int h = fixed_h > 0 ? fixed_h : std::max(ph, prev_max_h);
        w = std::max(1, std::min(w, maxr));
        h = std::max(1, std::min(h, maxr));

        if (HasTex() && (tex_mode != mode || tex_w != w || tex_h != h ||
                         (mode == TextureMode::Callbacks && tex_gen != G().generation)))
            ReleaseTex();

        if (!renderer.Valid() || renderer.Width() != w || renderer.Height() != h || renderer.GetAnimation() != anim)
        {
            renderer = Renderer(anim, w, h);
            cache.clear();
        }
        if (!renderer.Valid())
            return false;

        if (!HasTex() && !CreateTex(mode, w, h))
            return false;

        const int frame = Frame();
        if (uploaded_frame != frame)
        {
            Upload(frame);
            uploaded_frame = frame;
        }
        return true;
    }

    void AddImage(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col)
    {
#if TGS_IMGUI_HAS_TEXTURES
        if (tex_mode == TextureMode::ImGuiManaged && tex_data)
        {
            dl->AddImage(tex_data->GetTexRef(), a, b, ImVec2(0, 0), ImVec2(1, 1), col);
            return;
        }
#endif
        dl->AddImage((ImTextureID)(uintptr_t)tex_handle, a, b, ImVec2(0, 0), ImVec2(1, 1), col);
    }

    void DrawRect(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col)
    {
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        const int pw = int(std::lround((b.x - a.x) * (scale.x > 0 ? scale.x : 1.0f)));
        const int ph = int(std::lround((b.y - a.y) * (scale.y > 0 ? scale.y : 1.0f)));
        if (pw <= 0 || ph <= 0)
            return;
        if (Prepare(pw, ph))
            AddImage(dl, a, b, col);
        else
            DrawPlaceholder(dl, a, b);
    }
};

struct PlayerAccess
{
    static Player::Impl* Get(Player& p) { return p.impl_.get(); }
};

// ===========================================================================
// Player
// ===========================================================================
Player::Player() : impl_(new Impl()) {}

Player::Player(Animation::Ptr animation, const PlayerOptions& options) : impl_(new Impl())
{
    impl_->opt = options;
    SetAnimation(std::move(animation), true);
}

Player::~Player() = default;
Player::Player(Player&&) noexcept = default;
Player& Player::operator=(Player&&) noexcept = default;

void Player::SetAnimation(Animation::Ptr animation, bool restart)
{
    if (!impl_)
        impl_.reset(new Impl());
    if (impl_->anim != animation)
    {
        impl_->anim = std::move(animation);
        impl_->renderer = Renderer();
        impl_->cache.clear();
        impl_->uploaded_frame = -1;
    }
    if (restart)
        impl_->Restart();
    else if (impl_->anim)
        impl_->SetTimeClamped(impl_->time);
}

const Animation::Ptr& Player::GetAnimation() const
{
    static const Animation::Ptr empty;
    return impl_ ? impl_->anim : empty;
}

bool Player::Valid() const { return impl_ && impl_->anim; }
PlayerOptions& Player::Options() { return impl_->opt; }
const PlayerOptions& Player::Options() const { return impl_->opt; }

void Player::Play()
{
    if (impl_->finished)
    {
        impl_->finished = false;
        impl_->time = impl_->opt.speed < 0 ? impl_->Duration() - 0.5 / std::max(1.0, impl_->anim ? impl_->anim->Info().fps : 1.0) : 0.0;
    }
    impl_->stop_at_loop_end = false;
    impl_->playing = true;
}
void Player::Pause() { impl_->playing = false; }
void Player::Stop()
{
    impl_->playing = false;
    impl_->finished = false;
    impl_->stop_at_loop_end = false;
    impl_->time = 0;
}
void Player::TogglePlay() { IsPlaying() ? Pause() : Play(); }
bool Player::IsPlaying() const { return impl_->playing; }
bool Player::IsFinished() const { return impl_->finished; }

void  Player::SetLoop(bool loop) { impl_->opt.loop = loop; }
bool  Player::GetLoop() const { return impl_->opt.loop; }
void  Player::SetSpeed(float speed) { impl_->opt.speed = speed; }
float Player::GetSpeed() const { return impl_->opt.speed; }

void Player::SetFrame(int frame)
{
    if (!impl_->anim)
        return;
    impl_->SetTimeClamped((double(frame) + 0.5) / impl_->anim->Info().fps);
    impl_->finished = false;
}
int Player::GetFrame() const { return impl_->Frame(); }
int Player::GetFrameCount() const { return impl_->FrameCount(); }

void Player::SetTime(double seconds)
{
    impl_->SetTimeClamped(seconds);
    impl_->finished = false;
}
double Player::GetTime() const { return impl_->time; }

void Player::SetProgress(float t01)
{
    SetTime(double(std::max(0.0f, std::min(1.0f, t01))) * impl_->Duration());
}
float Player::GetProgress() const
{
    const double d = impl_->Duration();
    return d > 0 ? float(impl_->time / d) : 0.0f;
}

void Player::Update(float dt) { impl_->Update(dt); }

void Player::SetResolution(int width, int height)
{
    impl_->fixed_w = std::max(0, width);
    impl_->fixed_h = std::max(0, height);
}

void Player::Draw(const ImVec2& size, const ImVec4& tint, const ImVec4& bg)
{
    const ImVec2 sz = ResolveSize(size, impl_->anim.get());
    ImGui::Dummy(sz);
    impl_->AutoUpdate();
    if (!ImGui::IsItemVisible())
        return;
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (bg.w > 0.0f)
        dl->AddRectFilled(a, b, ImGui::GetColorU32(bg));
    impl_->DrawRect(dl, a, b, ImGui::GetColorU32(tint));
}

bool Player::DrawButton(const char* str_id, const ImVec2& size, const ImVec4& tint)
{
    const ImVec2 sz = ResolveSize(size, impl_->anim.get());
    const bool pressed = ImGui::InvisibleButton(str_id, sz);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    impl_->AutoUpdate();
    if (!ImGui::IsItemVisible())
        return pressed;
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered || held)
        dl->AddRectFilled(a, b, ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered),
                          ImGui::GetStyle().FrameRounding);
    impl_->DrawRect(dl, a, b, ImGui::GetColorU32(tint));
    return pressed;
}

void Player::DrawTo(ImDrawList* draw_list, const ImVec2& p_min, const ImVec2& p_max, ImU32 col)
{
    if (!draw_list)
        return;
    impl_->AutoUpdate();
    impl_->DrawRect(draw_list, p_min, p_max, col);
}

bool Player::Prepare(int pixel_width, int pixel_height) { return impl_->Prepare(pixel_width, pixel_height); }

ImTextureID Player::GetTexID() const
{
#if TGS_IMGUI_HAS_TEXTURES
    if (impl_->tex_mode == TextureMode::ImGuiManaged && impl_->tex_data)
        return impl_->tex_data->GetTexID();
#endif
    if (impl_->tex_mode == TextureMode::Callbacks)
        return (ImTextureID)(uintptr_t)impl_->tex_handle;
    return ImTextureID{};
}

#if TGS_IMGUI_HAS_TEXTURES
ImTextureRef Player::GetTexRef() const
{
    if (impl_->tex_mode == TextureMode::ImGuiManaged && impl_->tex_data)
        return impl_->tex_data->GetTexRef();
    return ImTextureRef((ImTextureID)(uintptr_t)impl_->tex_handle);
}
#endif

void Player::ReleaseTexture() { impl_->ReleaseTex(); }

// ===========================================================================
// Texture backend selection
// ===========================================================================
void SetTextureCallbacks(const TextureCallbacks* callbacks)
{
    Globals& g = G();
    if (callbacks)
    {
        g.cb = *callbacks;
        g.has_cb = true;
    }
    else
    {
        g.has_cb = false;
    }
    g.generation++;
}

TextureMode GetTextureMode() { return CurrentMode(); }

const char* GetTextureModeName()
{
    switch (CurrentMode())
    {
    case TextureMode::ImGuiManaged: return "ImGui (RendererHasTextures)";
    case TextureMode::Callbacks: return "Callbacks";
    default: return "None";
    }
}

// ===========================================================================
// Immediate-mode widgets
// ===========================================================================
namespace ui {
namespace {

Player* Acquire(ImGuiID id, const Animation::Ptr& anim, const StickerOptions& o)
{
    CachedWidget& w = G().widgets[id];
    if (!w.player)
    {
        PlayerOptions po;
        po.loop = o.loop;
        po.speed = o.speed;
        po.cache_frames = o.cache_frames;
        po.autoplay = !o.paused && !o.play_on_hover;
        w.player.reset(new Player(anim, po));
        w.was_paused = o.paused;
    }
    else if (w.player->GetAnimation() != anim)
    {
        w.player->Options().autoplay = !o.paused && !o.play_on_hover;
        w.player->SetAnimation(anim, true);
    }
    Player& p = *w.player;
    p.SetLoop(o.loop);
    p.SetSpeed(o.speed);
    p.Options().cache_frames = o.cache_frames;
    if (o.paused != w.was_paused)
    {
        if (o.paused)
            p.Pause();
        else if (!o.play_on_hover)
            p.Play();
        w.was_paused = o.paused;
    }
    w.last_frame = CurFrame();
    return &p;
}

bool StickerImpl(const char* str_id, const Animation::Ptr& anim, const ImVec2& size, const StickerOptions& o, bool button)
{
    NewFrame();
    if (!anim)
    {
        const ImVec2 sz = ResolveSize(size, nullptr);
        if (button)
            ImGui::InvisibleButton(str_id, sz);
        else
            ImGui::Dummy(sz);
        if (ImGui::IsItemVisible())
            DrawPlaceholder(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        return false;
    }

    Player* p = Acquire(ImGui::GetID(str_id), anim, o);
    bool result = true;
    if (button)
    {
        ImGui::PushID(str_id);
        result = p->DrawButton("##tgs", size, o.tint);
        ImGui::PopID();
    }
    else
    {
        p->Draw(size, o.tint, o.bg);
    }

    if (o.play_on_hover && !o.paused)
    {
        Player::Impl* im = PlayerAccess::Get(*p);
        if (ImGui::IsItemHovered())
        {
            if (!im->playing || im->stop_at_loop_end)
                p->Play();
        }
        else if (im->playing && im->opt.loop)
        {
            im->stop_at_loop_end = true; // finish the current loop, then rest on frame 0
        }
    }
    return result;
}

} // namespace

bool Sticker(const char* embedded_name, const ImVec2& size, const StickerOptions& options)
{
    return StickerImpl(embedded_name, Animation::FromEmbedded(embedded_name), size, options, false);
}

bool Sticker(const char* str_id, const Animation::Ptr& animation, const ImVec2& size, const StickerOptions& options)
{
    return StickerImpl(str_id, animation, size, options, false);
}

bool StickerButton(const char* embedded_name, const ImVec2& size, const StickerOptions& options)
{
    return StickerImpl(embedded_name, Animation::FromEmbedded(embedded_name), size, options, true);
}

bool StickerButton(const char* str_id, const Animation::Ptr& animation, const ImVec2& size, const StickerOptions& options)
{
    return StickerImpl(str_id, animation, size, options, true);
}

Player* GetStickerPlayer(const char* str_id)
{
    if (!HasContext())
        return nullptr;
    auto it = G().widgets.find(ImGui::GetID(str_id));
    return it == G().widgets.end() ? nullptr : it->second.player.get();
}

} // namespace ui

// ===========================================================================
// Housekeeping
// ===========================================================================
void NewFrame()
{
    if (!HasContext())
        return;
    Globals& g = G();
    const int f = ImGui::GetFrameCount();
    if (f == g.last_maint_frame)
        return;
    g.last_maint_frame = f;
    StatRoll();
    ProcessGraveyard(false);
    for (auto it = g.widgets.begin(); it != g.widgets.end();)
    {
        if (f - it->second.last_frame > g.widget_lifetime)
            it = g.widgets.erase(it);
        else
            ++it;
    }
}

void SetStickerCacheLifetime(int max_unused_frames) { G().widget_lifetime = std::max(1, max_unused_frames); }

void GarbageCollect()
{
    Globals& g = G();
    const int f = CurFrame();
    for (auto it = g.widgets.begin(); it != g.widgets.end();)
    {
        if (it->second.last_frame != f)
            it = g.widgets.erase(it);
        else
            ++it;
    }
    ProcessGraveyard(false);
}

void Shutdown()
{
    Globals& g = G();
    g.widgets.clear();
    std::vector<Player::Impl*> players = g.players;
    for (Player::Impl* p : players)
    {
        p->ReleaseTex();
        p->renderer = Renderer();
        p->cache.clear();
        p->scratch.clear();
    }
    ProcessGraveyard(true);
    g.has_cb = false;
    g.generation++;
    g.last_maint_frame = -1;
    ClearEmbeddedCache();
}

Stats GetStats()
{
    Globals& g = G();
    StatRoll();
    Stats s;
    s.players = int(g.players.size());
    s.cached_widgets = int(g.widgets.size());
    s.textures = g.textures;
    s.textures_dying = int(g.graveyard.size());
    s.frames_rendered = g.rendered_last;
    s.render_ms = g.ms_last;
    for (Player::Impl* p : g.players)
        for (auto& f : p->cache)
            s.frame_cache_bytes += f.size();
    return s;
}

} // namespace tgs
