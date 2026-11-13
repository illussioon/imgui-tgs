find_package(Threads)

add_executable(tgs-tool
    tgs_tool.cpp
    $<TARGET_OBJECTS:tgs_core_obj>
    $<TARGET_OBJECTS:tgs_rlottie_obj>
)
target_include_directories(tgs-tool PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
target_compile_features(tgs-tool PRIVATE cxx_std_17)
if(Threads_FOUND)
    target_link_libraries(tgs-tool PRIVATE Threads::Threads)
endif()
if(WIN32)
    target_link_libraries(tgs-tool PRIVATE shlwapi)
    target_compile_definitions(tgs-tool PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
install(TARGETS tgs-tool RUNTIME DESTINATION bin)
if(MINGW)
    target_link_options(tgs-tool PRIVATE -static -static-libgcc -static-libstdc++)
endif()
