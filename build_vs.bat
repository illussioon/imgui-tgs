@echo off
rem ---------------------------------------------------------------------------
rem  tgs-parse-imgui: build with Visual Studio (2019/2022/2026) via CMake
rem
rem    build_vs.bat                         x64, bundled Dear ImGui 1.93.0
rem    build_vs.bat Win32                   32-bit
rem    build_vs.bat x64 C:\dev\my\imgui     build against YOUR imgui (recommended: same
rem                                         version + imconfig.h as in your DLL project)
rem
rem  Result:  dist-<arch>\lib\tgs-parse-imgui.lib      (Release, /MT)
rem           dist-<arch>\lib\tgs-parse-imgui-d.lib    (Debug,   /MTd)
rem           dist-<arch>\include\tgs\*.h
rem           dist-<arch>\bin\tgs-tool.exe, tgs_demo_host.exe, tgs_demo.dll
rem ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

set ARCH=%~1
if "%ARCH%"=="" set ARCH=x64
set IMGUI_ARG=
if not "%~2"=="" set IMGUI_ARG=-DTGS_IMGUI_DIR="%~2"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [!] cmake not found. Install "C++ CMake tools for Windows" in Visual Studio Installer
    echo     or run this from "x64 Native Tools Command Prompt for VS".
    exit /b 1
)

set BUILD=build-%ARCH%
set DIST=dist-%ARCH%

cmake -S . -B %BUILD% -A %ARCH% %IMGUI_ARG% || goto :fail
cmake --build %BUILD% --config Release --parallel || goto :fail
cmake --build %BUILD% --config Debug --parallel --target tgs-parse-imgui || goto :fail

ctest --test-dir %BUILD% -C Release --output-on-failure || goto :fail

cmake --install %BUILD% --config Release --prefix %DIST% || goto :fail
copy /y "%BUILD%\Debug\tgs-parse-imgui-d.lib" "%DIST%\lib\" >nul

echo.
echo ==== OK ====
echo   lib:      %CD%\%DIST%\lib\tgs-parse-imgui.lib  (+ -d.lib for Debug)
echo   headers:  %CD%\%DIST%\include
echo   demo:     %CD%\%DIST%\bin\tgs_demo_host.exe
echo   solution: %CD%\%BUILD%\tgs_parse_imgui.sln
exit /b 0

:fail
echo.
echo ==== BUILD FAILED ====
exit /b 1
