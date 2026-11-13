cmake_minimum_required(VERSION 3.16)

# MSVC: embed debug info into .obj/.lib (/Z7) -> no missing-PDB warnings when linking the Debug .lib
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()

project(tgs_parse_imgui VERSION 1.0.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(TGS_TOP_LEVEL ON)
else()
    set(TGS_TOP_LEVEL OFF)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(TGS_BUILD_EXAMPLES      "Build example DLL + host (Windows/DX11)"          ${TGS_TOP_LEVEL})
option(TGS_BUILD_TOOLS         "Build tgs-tool (info / embed / validate)"         ${TGS_TOP_LEVEL})
option(TGS_BUILD_TESTS         "Build self-tests"                                 ${TGS_TOP_LEVEL})
option(TGS_MSVC_STATIC_RUNTIME "MSVC: link CRT statically (/MT) - typical for DLLs" ON)
option(TGS_BACKEND_DX11        "Compile built-in DX11 texture callbacks"          ${WIN32})
option(TGS_BACKEND_DX9         "Compile built-in DX9 texture callbacks"           ${WIN32})
option(TGS_BACKEND_OPENGL      "Compile built-in OpenGL texture callbacks"        ON)
set(TGS_IMGUI_DIR "" CACHE PATH
    "Path to YOUR Dear ImGui sources (folder with imgui.h). Empty = bundled third_party/imgui (v1.93.0)")

if(MSVC AND TGS_MSVC_STATIC_RUNTIME AND TGS_TOP_LEVEL)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(TGS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TgsImGui)     # -> target `imgui` (unless you already have one)
include(TgsRlottie)   # -> object library `tgs_rlottie_obj`
include(TgsEmbed)     # -> function tgs_embed(<target> FILES ...)

# ---------------------------------------------------------------------------
# Core (no ImGui dependency): gzip/inflate, Lottie loading, software rendering,
# embedded asset registry. Used by the main lib and by tgs-tool.
# ---------------------------------------------------------------------------
add_library(tgs_core_obj OBJECT
    src/tgs_inflate.cpp
    src/tgs_inflate.h
    src/tgs_animation.cpp
    src/tgs_embed_registry.cpp
    include/tgs/tgs.h
    include/tgs/tgs_embed.h
)
target_compile_features(tgs_core_obj PUBLIC cxx_std_17)
target_include_directories(tgs_core_obj
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rlottie/inc"
)
target_compile_definitions(tgs_core_obj PRIVATE RLOTTIE_STATIC)
set_target_properties(tgs_core_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# tgs-parse-imgui  (static .lib = core + ImGui layer + backends + rlottie)
# ImGui itself is NOT inside the .lib - you link your own imgui.
# ---------------------------------------------------------------------------
set(TGS_IMGUI_SOURCES
    src/tgs_imgui.cpp
    include/tgs/tgs_imgui.h
)
if(TGS_BACKEND_DX11)
    list(APPEND TGS_IMGUI_SOURCES src/backends/tgs_dx11.cpp include/tgs/backends/tgs_dx11.h)
endif()
if(TGS_BACKEND_DX9)
    list(APPEND TGS_IMGUI_SOURCES src/backends/tgs_dx9.cpp include/tgs/backends/tgs_dx9.h)
endif()
if(TGS_BACKEND_OPENGL AND NOT WIN32)
    find_package(OpenGL QUIET)
    if(NOT OPENGL_FOUND)
        message(STATUS "tgs: OpenGL headers not found -> TGS_BACKEND_OPENGL disabled")
        set(TGS_BACKEND_OPENGL OFF)
    endif()
endif()
if(TGS_BACKEND_OPENGL)
    list(APPEND TGS_IMGUI_SOURCES src/backends/tgs_opengl.cpp include/tgs/backends/tgs_opengl.h)
endif()

add_library(tgs-parse-imgui STATIC
    ${TGS_IMGUI_SOURCES}
    $<TARGET_OBJECTS:tgs_core_obj>
    $<TARGET_OBJECTS:tgs_rlottie_obj>
)
add_library(tgs::tgs ALIAS tgs-parse-imgui)
target_compile_features(tgs-parse-imgui PUBLIC cxx_std_17)
target_include_directories(tgs-parse-imgui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(tgs-parse-imgui PUBLIC imgui)
find_package(Threads)
if(Threads_FOUND)
    target_link_libraries(tgs-parse-imgui PUBLIC Threads::Threads)
endif()
if(WIN32)
    target_link_libraries(tgs-parse-imgui PUBLIC shlwapi)   # rlottie: PathCanonicalizeW
endif()
if(TGS_BACKEND_OPENGL AND OPENGL_INCLUDE_DIR)
    target_include_directories(tgs-parse-imgui PRIVATE "${OPENGL_INCLUDE_DIR}")
endif()
set_target_properties(tgs-parse-imgui PROPERTIES
    OUTPUT_NAME "tgs-parse-imgui"
    DEBUG_POSTFIX "-d"
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(tgs-parse-imgui PRIVATE /W4 /utf-8)
    target_compile_options(tgs_core_obj PRIVATE /W4 /utf-8)
else()
    target_compile_options(tgs-parse-imgui PRIVATE -Wall -Wextra)
    target_compile_options(tgs_core_obj PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    target_compile_definitions(tgs-parse-imgui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ---------------------------------------------------------------------------
# Install: `cmake --install build --prefix dist`
#   dist/lib/tgs-parse-imgui.lib, dist/include/tgs/*.h, dist/cmake/TgsEmbed*.cmake, dist/bin/tgs-tool
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS tgs-parse-imgui ARCHIVE DESTINATION lib)
install(DIRECTORY include/tgs DESTINATION include)
install(FILES cmake/TgsEmbed.cmake cmake/TgsEmbedGen.cmake DESTINATION cmake)
install(FILES README.md DESTINATION .)

if(TGS_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
if(TGS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(TGS_BUILD_EXAMPLES AND WIN32)
    add_subdirectory(examples/dll_dx11)
endif()
