// tgs_demo_host.exe — loads tgs_demo.dll (from its own folder, full path -> no DLL search-order
// hijacking), lists the stickers that were embedded into the DLL at compile time and runs the demo.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <string>

typedef int(__cdecl* RunFn)();
typedef int(__cdecl* CountFn)();
typedef const char*(__cdecl* NameFn)(int);

int main()
{
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    const std::wstring dll = dir + L"tgs_demo.dll";

    HMODULE m = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m)
    {
        std::fprintf(stderr, "LoadLibrary(tgs_demo.dll) failed, error %lu\n", GetLastError());
        return 1;
    }
    auto count = reinterpret_cast<CountFn>(GetProcAddress(m, "tgs_demo_sticker_count"));
    auto name = reinterpret_cast<NameFn>(GetProcAddress(m, "tgs_demo_sticker_name"));
    auto run = reinterpret_cast<RunFn>(GetProcAddress(m, "tgs_demo_run"));
    if (!count || !name || !run)
    {
        std::fprintf(stderr, "tgs_demo.dll: exports not found\n");
        FreeLibrary(m);
        return 1;
    }
    std::printf("tgs_demo.dll: %d embedded sticker(s):\n", count());
    for (int i = 0; i < count(); ++i)
        std::printf("  - %s\n", name(i));
    std::printf("running demo...\n");
    const int rc = run();
    FreeLibrary(m);
    std::printf("done (%d)\n", rc);
    return rc;
}
