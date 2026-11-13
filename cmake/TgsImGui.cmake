# Provides target `imgui` (core Dear ImGui, no backends).
#  - If your project already defines a target named `imgui`, it is used as is.
#  - Else sources are taken from TGS_IMGUI_DIR (your copy) or bundled third_party/imgui.
# Backends are NOT part of this target - the example adds the ones it needs.

if(TARGET imgui)
    message(STATUS "tgs: using existing `imgui` target")
    return()
endif()

if(TGS_IMGUI_DIR)
    set(_tgs_imgui_dir "${TGS_IMGUI_DIR}")
else()
    set(_tgs_imgui_dir "${CMAKE_CURRENT_LIST_DIR}/../third_party/imgui")
endif()
get_filename_component(_tgs_imgui_dir "${_tgs_imgui_dir}" ABSOLUTE)

if(NOT EXISTS "${_tgs_imgui_dir}/imgui.h")
    message(FATAL_ERROR "tgs: imgui.h not found in '${_tgs_imgui_dir}'. Set TGS_IMGUI_DIR.")
endif()

set(TGS_IMGUI_SOURCE_DIR "${_tgs_imgui_dir}" CACHE INTERNAL "")

set(_tgs_imgui_src
    "${_tgs_imgui_dir}/imgui.cpp"
    "${_tgs_imgui_dir}/imgui_draw.cpp"
    "${_tgs_imgui_dir}/imgui_tables.cpp"
    "${_tgs_imgui_dir}/imgui_widgets.cpp"
    "${_tgs_imgui_dir}/imgui_demo.cpp"
)
foreach(_f IN LISTS _tgs_imgui_src)
    if(NOT EXISTS "${_f}")
        list(REMOVE_ITEM _tgs_imgui_src "${_f}")   # very old imgui has no imgui_tables.cpp
    endif()
endforeach()

add_library(imgui STATIC ${_tgs_imgui_src})
target_include_directories(imgui PUBLIC "${_tgs_imgui_dir}")
target_compile_features(imgui PUBLIC cxx_std_11)
set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
message(STATUS "tgs: Dear ImGui from ${_tgs_imgui_dir}")
