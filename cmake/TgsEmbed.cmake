# tgs_embed(<target>
#     FILES  a.tgs b.tgs ...        # .tgs (gzip Lottie) or plain Lottie .json
#     [PREFIX  "pack/"]             # optional name prefix -> "pack/duck"
# )
#
# At build time converts every file into a byte array and compiles it INTO <target>
# (your .dll / .exe). Each asset is registered under its file name without extension
# (+ PREFIX) and can be played via:
#     tgs::ui::Sticker("duck", ImVec2(128,128));
#     tgs::Animation::FromEmbedded("duck");
#
# Also generates header <tgs_assets.h> (added to <target> include path):
#     tgs_assets::duck            -> "duck"   (compile-time checked names)
#     tgs_assets::all[] / count
#     TGS_REGISTER_ASSETS()       -> explicit registration (only needed if <target> is a STATIC lib)
#
# Re-runs automatically when any .tgs file changes.

set(_TGS_EMBED_GEN_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/TgsEmbedGen.cmake" CACHE INTERNAL "")

function(tgs_embed target)
    cmake_parse_arguments(ARG "" "PREFIX" "FILES" ${ARGN})
    if(NOT TARGET ${target})
        message(FATAL_ERROR "tgs_embed: '${target}' is not a target")
    endif()
    if(NOT ARG_FILES)
        message(FATAL_ERROR "tgs_embed(${target}): no FILES given")
    endif()

    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/tgs_embed/${target}")
    set(out_cpp "${out_dir}/tgs_embedded_${target}.cpp")
    set(out_h   "${out_dir}/tgs_assets.h")
    set(manifest "${out_dir}/manifest.txt")
    file(MAKE_DIRECTORY "${out_dir}")

    set(manifest_text "")
    set(abs_files "")
    set(seen_names "")
    foreach(f IN LISTS ARG_FILES)
        get_filename_component(abs "${f}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT EXISTS "${abs}")
            message(FATAL_ERROR "tgs_embed(${target}): file not found: ${abs}")
        endif()
        get_filename_component(stem "${abs}" NAME_WE)
        set(name "${ARG_PREFIX}${stem}")
        if(name IN_LIST seen_names)
            message(FATAL_ERROR "tgs_embed(${target}): duplicate asset name '${name}' (${abs})")
        endif()
        list(APPEND seen_names "${name}")
        list(APPEND abs_files "${abs}")
        string(APPEND manifest_text "${name}|${abs}\n")
    endforeach()

    # rewrite manifest only when it actually changes (no needless rebuilds)
    file(WRITE "${manifest}.tmp" "${manifest_text}")
    configure_file("${manifest}.tmp" "${manifest}" COPYONLY)

    list(LENGTH abs_files n_files)
    add_custom_command(
        OUTPUT  "${out_cpp}" "${out_h}"
        COMMAND "${CMAKE_COMMAND}"
                "-DTGS_MANIFEST=${manifest}"
                "-DTGS_OUT_CPP=${out_cpp}"
                "-DTGS_OUT_H=${out_h}"
                "-DTGS_TARGET=${target}"
                -P "${_TGS_EMBED_GEN_SCRIPT}"
        DEPENDS ${abs_files} "${manifest}" "${_TGS_EMBED_GEN_SCRIPT}"
        COMMENT "tgs: embedding ${n_files} sticker(s) into ${target}"
        VERBATIM
    )
    target_sources(${target} PRIVATE "${out_cpp}" "${out_h}")
    target_include_directories(${target} PRIVATE "${out_dir}")
    source_group("tgs_embedded" FILES "${out_cpp}" "${out_h}")
endfunction()
