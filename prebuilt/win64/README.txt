Prebuilt Windows x64 demo (built with MinGW-w64 GCC 13, statically linked, stripped).
Just run tgs_demo_host.exe (tgs_demo.dll must be next to it). Windows 10/11.

  tgs_demo_host.exe  - loads tgs_demo.dll by full path, prints the stickers embedded in it, opens the demo
  tgs_demo.dll       - Dear ImGui 1.93 + DX11 + tgs-parse-imgui + 5 .tgs stickers compiled in
  tgs-tool.exe       - CLI: info / check / embed / png / sheet / unpack

For your own MSVC project build the .lib with build_vs.bat (MinGW .a is not linkable with MSVC).
