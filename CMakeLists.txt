# DeepImage Viewer — native Windows build (x64 / ARM64)
# Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
cmake_minimum_required(VERSION 3.20)
project(DeepImageViewer VERSION 1.0.1 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(DEPS "" CACHE PATH "Prefix with OpenCASCADE and Assimp (static)")

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000B)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wno-unknown-pragmas -Wno-missing-braces -Wno-unused-function)
    set(STATIC_RT -static -static-libgcc -static-libstdc++)
    set(GC_SECTIONS -Wl,--gc-sections -s)
endif()

# ------------------------------------------------------------------ model engine (lazy-loaded DLL)
set(OpenCASCADE_DIR "${DEPS}/cmake")
find_package(OpenCASCADE CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED PATHS "${DEPS}/lib/cmake")

add_library(miniz STATIC third_party/miniz/miniz.c third_party/miniz/miniz_zip.c third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_tdef.c)
target_include_directories(miniz PUBLIC third_party/miniz)
target_compile_options(miniz PRIVATE -w)
add_library(dvmodel SHARED model/dvmodel.cpp model/model_load.cpp model/three_mf.cpp model/thumb_render.cpp)
target_include_directories(dvmodel PRIVATE ${OpenCASCADE_INCLUDE_DIR} "${DEPS}/include")
target_link_libraries(dvmodel PRIVATE
    TKDESTEP TKDEIGES TKXCAF TKMesh TKBO TKPrim TKShHealing TKTopAlgo TKGeomAlgo TKBRep TKGeomBase TKG3d TKG2d TKMath TKernel
    miniz assimp::assimp d3d11 d3dcompiler_47 windowscodecs ole32 oleaut32 uuid ${STATIC_RT} ${GC_SECTIONS})
set_target_properties(dvmodel PROPERTIES PREFIX "")

# ------------------------------------------------------------------ viewer
add_executable(DeepImage WIN32
    app/main.cpp app/window.cpp app/draw.cpp app/actions.cpp app/common.cpp app/i18n.cpp app/settings.cpp app/gfx.cpp app/ui.cpp
    app/image.cpp app/imageview.cpp app/model3d.cpp app/thumbs.cpp app/info.cpp app/edit.cpp app/worker.cpp
    app/resource.rc)
target_link_libraries(DeepImage PRIVATE miniz d2d1 d3d11 dxgi dwrite windowscodecs d3dcompiler_47 dwmapi shlwapi shell32 ole32
    oleaut32 uuid propsys comdlg32 uxtheme ${STATIC_RT} ${GC_SECTIONS} -municode)

# ------------------------------------------------------------------ Explorer thumbnail bridge
add_library(DeepImageThumbs SHARED shell/thumb_provider.cpp shell/process_runner.cpp shell/stream_thumbnail.cpp shell/thumb_provider.def)
target_include_directories(DeepImageThumbs PRIVATE shell)
target_link_libraries(DeepImageThumbs PRIVATE ole32 windowscodecs advapi32 shell32 gdi32 uuid ${STATIC_RT} ${GC_SECTIONS})
set_target_properties(DeepImageThumbs PROPERTIES PREFIX "")

# ------------------------------------------------------------------ installer stub
add_executable(DeepImageSetup WIN32 setup/setup.cpp app/common.cpp app/gfx.cpp app/ui.cpp app/i18n.cpp app/settings.cpp setup/setup.rc)
target_include_directories(DeepImageSetup PRIVATE third_party/miniz)
target_link_libraries(DeepImageSetup PRIVATE miniz d2d1 d3d11 dxgi dwrite windowscodecs dwmapi shlwapi shell32 ole32 oleaut32 uuid propsys
    ${STATIC_RT} ${GC_SECTIONS} -municode)
