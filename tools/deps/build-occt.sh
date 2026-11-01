#!/bin/bash
set -e
A=$1
B=/home/claude/deps/build/occt-$A
I=/home/claude/deps/install/$A
args=(-S /home/claude/deps/occt -B $B -G Ninja -DCMAKE_TOOLCHAIN_FILE=/home/claude/deps/cmake/$A.cmake -DCMAKE_BUILD_TYPE=Release
 -DCMAKE_INSTALL_PREFIX=$I -DINSTALL_DIR_BIN=bin -DINSTALL_DIR_LIB=lib -DINSTALL_DIR_INCLUDE=include/opencascade
 -DBUILD_LIBRARY_TYPE=Static -DBUILD_ADDITIONAL_TOOLKITS="TKDESTEP;TKDEIGES;TKMesh;TKPrim;TKBO;TKBool;TKXCAF"
 -DBUILD_USE_PCH=ON -DUSE_MMGR_TYPE=NATIVE -DBUILD_DOC_Overview=OFF -DBUILD_SAMPLES_QT=OFF -DBUILD_Inspector=OFF)
for m in FoundationClasses ModelingData ModelingAlgorithms Visualization ApplicationFramework DataExchange DETools Draw; do args+=(-DBUILD_MODULE_$m=OFF); done
for f in TK FREETYPE FREEIMAGE FFMPEG OPENVR RAPIDJSON DRACO TBB EIGEN D3D OPENGL GLES2 VTK; do args+=(-DUSE_$f=OFF); done
cmake "${args[@]}" > /home/claude/deps/occt-$A-config.log 2>&1
cmake --build $B -j2 > /home/claude/deps/occt-$A-build.log 2>&1
cmake --install $B > /home/claude/deps/occt-$A-install.log 2>&1
echo DONE-$A
