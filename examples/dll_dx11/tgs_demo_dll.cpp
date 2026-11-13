// tgs_demo.dll — example: .tgs stickers embedded INTO this DLL at compile time (see CMakeLists.txt:
// tgs_embed(tgs_demo FILES assets/*.tgs)) and played in Dear ImGui (Win32 + DirectX 11).
//
// Exports:
//   int         tgs_demo_run()                 opens the demo window, blocks until closed
//   int         tgs_demo_sticker_count()       number of stickers embedded in this DLL
//   const char* tgs_demo_sticker_name(int i)
//
// Drag & drop any .tgs onto the window to load it at runtime.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <tgs/backends/tgs_dx11.h>
#include <tgs/tgs_imgui.h>

#include "tgs_assets.h" // generated: tgs_assets::star_spin, tgs_assets::all[], ...

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#define TGS_DEMO_API extern "C" __declspec(dllexport)

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

HMODULE                 g_module = nullptr;
ID3D11Device*           g_device = nullptr;
ID3D11DeviceContext*    g_context = nullptr;
IDXGISwapChain*         g_swapchain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool                    g_occluded = false;
UINT                    g_resize_w = 0, g_resize_h = 0;

struct LoadedSticker
{
    std::string         label;
    tgs::Animation::Ptr anim;
};
std::vector<LoadedSticker> g_loaded;
std::string                g_status;

// ---------------------------------------------------------------------------
// D3D11 boilerplate (same as the official ImGui example)
// ---------------------------------------------------------------------------
void CreateRenderTarget()
{
    ID3D11Texture2D* back = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back)
    {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

void CleanupRenderTarget()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool CreateDeviceD3D(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &g_swapchain, &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swapchain, &g_device, &got, &g_context);
    if (hr != S_OK)
        return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

std::string WideToUtf8(const wchar_t* w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

void LoadFromFileW(const wchar_t* path)
{
    std::string err;
    tgs::Animation::Ptr a = tgs::Animation::FromFile(std::wstring(path), &err);
    const std::string u8 = WideToUtf8(path);
    if (!a)
    {
        g_status = "Failed: " + u8 + " — " + err;
        return;
    }
    g_loaded.push_back({a->Name().empty() ? u8 : a->Name(), a});
    g_status = "Loaded: " + u8;
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
        return true;
    switch (msg)
    {
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED)
            return 0;
        g_resize_w = UINT(LOWORD(lparam));
        g_resize_h = UINT(HIWORD(lparam));
        return 0;
    case WM_DROPFILES:
    {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i)
        {
            wchar_t path[MAX_PATH * 2];
            if (DragQueryFileW(drop, i, path, UINT(sizeof(path) / sizeof(path[0]))))
                LoadFromFileW(path);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// Demo UI
// ---------------------------------------------------------------------------
struct DemoState
{
    float size = 112.0f;
    tgs::ui::StickerOptions opt;
    bool  force_callbacks = false;
    bool  show_overlay = true;
    bool  show_imgui_demo = false;

    tgs::Player player;               // explicit player (Player window)
    std::string player_label;
    int         player_res = 0;       // 0 = auto
    char        path[512] = "";
};

void SelectForPlayer(DemoState& s, const tgs::Animation::Ptr& a, const std::string& label)
{
    if (!a)
        return;
    s.player.SetAnimation(a, true);
    s.player.Play();
    s.player_label = label;
}

void StickerTooltip(const tgs::Animation::Ptr& a, const char* name)
{
    if (!a || !ImGui::BeginItemTooltip())
        return;
    const tgs::AnimationInfo& in = a->Info();
    ImGui::Text("%s", name);
    ImGui::TextDisabled("%dx%d  %.0f fps  %d frames  %.2f s", in.width, in.height, in.fps, in.frame_count, in.duration);
    ImGui::TextDisabled("click: open in Player");
    ImGui::EndTooltip();
}

void WindowEmbedded(DemoState& s)
{
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760, 470), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Embedded stickers (inside tgs_demo.dll)"))
    {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("%d sticker(s), %llu bytes compiled into the DLL via tgs_embed()",
                        tgs_assets::count, tgs_assets::total_bytes);

    ImGui::PushItemWidth(170);
    ImGui::SliderFloat("size", &s.size, 24.0f, 320.0f, "%.0f px");
    ImGui::SameLine();
    ImGui::SliderFloat("speed", &s.opt.speed, -2.0f, 3.0f, "%.2fx");
    ImGui::PopItemWidth();
    ImGui::Checkbox("paused", &s.opt.paused);
    ImGui::SameLine();
    ImGui::Checkbox("play on hover", &s.opt.play_on_hover);
    ImGui::SameLine();
    ImGui::Checkbox("loop", &s.opt.loop);
    ImGui::SameLine();
    ImGui::Checkbox("cache frames", &s.opt.cache_frames);
    ImGui::SameLine();
    ImGui::ColorEdit4("tint", &s.opt.tint.x, ImGuiColorEditFlags_NoInputs);
    ImGui::Separator();

    ImGui::BeginChild("grid", ImVec2(0, 0), 0, ImGuiWindowFlags_HorizontalScrollbar);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float cell = s.size + ImGui::GetStyle().ItemSpacing.x;
    const int cols = std::max(1, int(avail / cell));
    // Iterate the runtime registry (works for any DLL); tgs_assets::all[] is the compile-time list.
    for (size_t i = 0; i < tgs::EmbeddedCount(); ++i)
    {
        const char* name = tgs::EmbeddedAt(i)->name;
        if (i % size_t(cols) != 0)
            ImGui::SameLine();
        ImGui::BeginGroup();
        if (tgs::ui::StickerButton(name, ImVec2(s.size, s.size), s.opt))
            SelectForPlayer(s, tgs::Animation::FromEmbedded(name), name);
        StickerTooltip(tgs::Animation::FromEmbedded(name), name);
        ImGui::TextDisabled("%s", name);
        ImGui::EndGroup();
    }
    ImGui::EndChild();
    ImGui::End();
}

void WindowPlayer(DemoState& s)
{
    ImGui::SetNextWindowPos(ImVec2(792, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Player"))
    {
        ImGui::End();
        return;
    }
    if (!s.player.Valid())
    {
        ImGui::TextWrapped("Click a sticker to open it here.");
        ImGui::End();
        return;
    }
    const tgs::AnimationInfo& in = s.player.GetAnimation()->Info();
    ImGui::Text("%s", s.player_label.c_str());
    ImGui::TextDisabled("%dx%d  %.0f fps  %d frames  %.2f s  json %zu B", in.width, in.height, in.fps, in.frame_count,
                        in.duration, s.player.GetAnimation()->Json().size());

    const float side = std::min(ImGui::GetContentRegionAvail().x, 300.0f);
    s.player.Draw(ImVec2(side, side), ImVec4(1, 1, 1, 1), ImVec4(0.12f, 0.12f, 0.14f, 1.0f));

    if (ImGui::Button(s.player.IsPlaying() ? "Pause" : "Play", ImVec2(70, 0)))
        s.player.TogglePlay();
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(70, 0)))
        s.player.Stop();
    ImGui::SameLine();
    bool loop = s.player.GetLoop();
    if (ImGui::Checkbox("loop", &loop))
        s.player.SetLoop(loop);

    float speed = s.player.GetSpeed();
    if (ImGui::SliderFloat("speed", &speed, -3.0f, 3.0f, "%.2fx"))
        s.player.SetSpeed(speed);
    int frame = s.player.GetFrame();
    if (ImGui::SliderInt("frame", &frame, 0, s.player.GetFrameCount() - 1))
    {
        s.player.Pause();
        s.player.SetFrame(frame);
    }
    ImGui::ProgressBar(s.player.GetProgress(), ImVec2(-1, 0));
    if (ImGui::SliderInt("texture px", &s.player_res, 0, 1024, s.player_res ? "%d" : "auto"))
        s.player.SetResolution(s.player_res, s.player_res);
    ImGui::Checkbox("cache frames##player", &s.player.Options().cache_frames);
    if (s.player.IsFinished())
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "finished");
    ImGui::End();
}

void WindowLoad(DemoState& s)
{
    ImGui::SetNextWindowPos(ImVec2(16, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760, 250), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Load .tgs at runtime"))
    {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("Drag & drop .tgs / Lottie .json files onto the window, or type a path:");
    ImGui::SetNextItemWidth(-90);
    const bool enter = ImGui::InputText("##path", s.path, sizeof(s.path), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Load", ImVec2(-1, 0)) || enter)
    {
        std::string err;
        std::string p = s.path;
        p.erase(std::remove(p.begin(), p.end(), '"'), p.end());
        tgs::Animation::Ptr a = tgs::Animation::FromFile(p, &err);
        if (a)
        {
            g_loaded.push_back({a->Name(), a});
            g_status = "Loaded: " + p;
        }
        else
        {
            g_status = "Failed: " + err;
        }
    }
    if (!g_status.empty())
        ImGui::TextWrapped("%s", g_status.c_str());
    ImGui::Separator();
    for (size_t i = 0; i < g_loaded.size(); ++i)
    {
        if (i)
            ImGui::SameLine();
        ImGui::PushID(int(i));
        ImGui::BeginGroup();
        if (tgs::ui::StickerButton("file", g_loaded[i].anim, ImVec2(96, 96), s.opt))
            SelectForPlayer(s, g_loaded[i].anim, g_loaded[i].label);
        StickerTooltip(g_loaded[i].anim, g_loaded[i].label.c_str());
        if (ImGui::SmallButton("remove"))
        {
            g_loaded.erase(g_loaded.begin() + long(i));
            ImGui::EndGroup();
            ImGui::PopID();
            break;
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::End();
}

void WindowStats(DemoState& s)
{
    ImGui::SetNextWindowPos(ImVec2(792, 590), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 200), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Stats / backend"))
    {
        ImGui::End();
        return;
    }
    ImGui::Text("Dear ImGui %s | texture mode: %s", IMGUI_VERSION, tgs::GetTextureModeName());
#if TGS_IMGUI_HAS_TEXTURES
    if (ImGui::Checkbox("force DX11 callbacks (legacy path)", &s.force_callbacks))
    {
        if (s.force_callbacks)
        {
            tgs::TextureCallbacks cb = tgs::MakeDX11TextureCallbacks(g_device);
            tgs::SetTextureCallbacks(&cb);
        }
        else
        {
            tgs::SetTextureCallbacks(nullptr);
        }
    }
#endif
    ImGui::Checkbox("overlay sticker (foreground draw list)", &s.show_overlay);
    ImGui::Checkbox("ImGui demo window", &s.show_imgui_demo);
    const tgs::Stats st = tgs::GetStats();
    ImGui::Text("players %d | widgets %d | textures %d (+%d dying)", st.players, st.cached_widgets, st.textures, st.textures_dying);
    ImGui::Text("rasterized last frame: %d frame(s), %.2f ms", st.frames_rendered, st.render_ms);
    ImGui::Text("frame cache: %.1f MB", double(st.frame_cache_bytes) / (1024.0 * 1024.0));
    ImGui::Text("%.1f FPS", double(ImGui::GetIO().Framerate));
    ImGui::End();
}

void Overlay(DemoState& s)
{
    if (!s.show_overlay)
        return;
    // Overlay-style drawing (like a game HUD): no window, straight into the foreground draw list.
    static tgs::Player overlay(tgs::Animation::FromEmbedded(tgs_assets::orb_ring));
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float sz = 96.0f;
    const ImVec2 a(ds.x - sz - 12.0f, ds.y - sz - 12.0f);
    overlay.DrawTo(ImGui::GetForegroundDrawList(), a, ImVec2(a.x + sz, a.y + sz));
}

} // namespace

// ===========================================================================
// Exports
// ===========================================================================
TGS_DEMO_API int tgs_demo_sticker_count() { return int(tgs::EmbeddedCount()); }

TGS_DEMO_API const char* tgs_demo_sticker_name(int i)
{
    const tgs::EmbeddedAsset* a = tgs::EmbeddedAt(size_t(i));
    return a ? a->name : nullptr;
}

TGS_DEMO_API int tgs_demo_run()
{
#if IMGUI_VERSION_NUM >= 19200
    ImGui_ImplWin32_EnableDpiAwareness();
    const float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
#else
    const float dpi = 1.0f;
#endif
    HINSTANCE inst = g_module ? g_module : GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0, inst, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"tgs_demo_wnd", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"tgs-parse-imgui demo (DLL + DX11)", WS_OVERLAPPEDWINDOW, 100, 100,
                              int(1240 * dpi), int(820 * dpi), nullptr, nullptr, inst, nullptr);
    if (!hwnd || !CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        if (hwnd)
            DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, inst);
        return 1;
    }
    DragAcceptFiles(hwnd, TRUE);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // don't drop imgui.ini next to the host exe
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 4.0f;
    style.WindowRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.ScaleAllSizes(dpi);
#if IMGUI_VERSION_NUM >= 19200
    style.FontScaleDpi = dpi;
#endif

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    // ImGui >= 1.92 + official DX11 backend -> textures are managed by ImGui automatically.
    // Older ImGui (TGS_IMGUI_DIR pointing to e.g. 1.89) -> use the built-in DX11 callbacks.
    if (tgs::GetTextureMode() == tgs::TextureMode::None)
    {
        tgs::TextureCallbacks cb = tgs::MakeDX11TextureCallbacks(g_device);
        tgs::SetTextureCallbacks(&cb);
    }

    DemoState state;
    // open "duck" in the Player if it was embedded (assets/duck.tgs), otherwise a sample sticker
    const char* first = tgs::FindEmbedded("duck") ? "duck" : tgs_assets::heart_beat;
    SelectForPlayer(state, tgs::Animation::FromEmbedded(first), first);

    const float clear[4] = {0.08f, 0.09f, 0.11f, 1.0f};
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;
        if (g_occluded && g_swapchain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            Sleep(10);
            continue;
        }
        g_occluded = false;
        if (g_resize_w != 0 && g_resize_h != 0)
        {
            CleanupRenderTarget();
            g_swapchain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        WindowEmbedded(state);
        WindowPlayer(state);
        WindowLoad(state);
        WindowStats(state);
        Overlay(state);
        if (state.show_imgui_demo)
            ImGui::ShowDemoWindow(&state.show_imgui_demo);

        ImGui::Render();
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        HRESULT hr = g_swapchain->Present(1, 0);
        g_occluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    // Order matters: backend shutdown -> tgs::Shutdown -> DestroyContext -> release device
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    tgs::Shutdown();
    ImGui::DestroyContext();
    state.player = tgs::Player();
    g_loaded.clear();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
