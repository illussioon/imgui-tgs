# Example: stickers embedded into a DLL + Dear ImGui (Win32 + DX11)
#   tgs_demo.dll       — ImGui + tgs + embedded .tgs (exports tgs_demo_run)
#   tgs_demo_host.exe  — loads the DLL and calls tgs_demo_run()

set(IMGUI_BACKENDS "${TGS_IMGUI_SOURCE_DIR}/backends")

add_library(tgs_demo SHARED
    tgs_demo_dll.cpp
    "${IMGUI_BACKENDS}/imgui_impl_win32.cpp"
    "${IMGUI_BACKENDS}/imgui_impl_dx11.cpp"
)
target_include_directories(tgs_demo PRIVATE "${IMGUI_BACKENDS}")
target_link_libraries(tgs_demo PRIVATE tgs::tgs d3d11 dxgi d3dcompiler dwmapi shell32)
target_compile_definitions(tgs_demo PRIVATE UNICODE _UNICODE)
set_target_properties(tgs_demo PROPERTIES PREFIX "")   # tgs_demo.dll (no "lib" prefix on MinGW)
if(MINGW)
    target_link_options(tgs_demo PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# >>> the important part: compile every .tgs from assets/ INTO the DLL <<<
file(GLOB TGS_DEMO_STICKERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.tgs")
tgs_embed(tgs_demo FILES ${TGS_DEMO_STICKERS})

add_executable(tgs_demo_host host.cpp)
add_dependencies(tgs_demo_host tgs_demo)
if(MINGW)
    target_link_options(tgs_demo_host PRIVATE -static -static-libgcc -static-libstdc++)
endif()

foreach(t tgs_demo tgs_demo_host)
    set_target_properties(${t} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endforeach()
set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT tgs_demo_host)

install(TARGETS tgs_demo tgs_demo_host RUNTIME DESTINATION bin LIBRARY DESTINATION bin)
