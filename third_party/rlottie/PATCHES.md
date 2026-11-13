# Local patches applied to vendored rlottie

1. `inc/rlottie.h`, `inc/rlottiecommon.h`, `src/vector/stb/stb_image.cpp`:
   added `RLOTTIE_STATIC` branch -> `RLOTTIE_API` expands to nothing, so linking
   rlottie statically into your DLL does NOT add rlottie symbols to the DLL export table.
2. `src/vector/stb/stb_image.cpp`: `#define STB_IMAGE_STATIC` so `stbi_*` symbols stay
   internal and do not clash with a stb_image copy in your own project.
3. Removed: build files (CMake/meson), C API header, wasm/binding folders, tests, examples.
   The library is compiled by `cmake/TgsRlottie.cmake` from the top-level project.
