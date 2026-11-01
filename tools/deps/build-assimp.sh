#!/bin/bash
set -e
A=$1
B=/home/claude/deps/build/assimp-$A
I=/home/claude/deps/install/$A
args=(-S /home/claude/deps/assimp -B $B -G Ninja -DCMAKE_TOOLCHAIN_FILE=/home/claude/deps/cmake/$A.cmake -DCMAKE_BUILD_TYPE=Release
 -DCMAKE_INSTALL_PREFIX=$I -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF
 -DASSIMP_INSTALL_PDB=OFF -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF -DASSIMP_NO_EXPORT=ON
 -DASSIMP_BUILD_ZLIB=ON -DASSIMP_BUILD_DRACO=OFF -DASSIMP_DOUBLE_PRECISION=OFF -DASSIMP_BUILD_USD_IMPORTER=OFF)
for f in OBJ PLY GLTF FBX 3MF COLLADA 3DS STL AMF OFF; do args+=(-DASSIMP_BUILD_${f}_IMPORTER=ON); done
cmake "${args[@]}" > /home/claude/deps/assimp-$A-config.log 2>&1
cmake --build $B -j2 > /home/claude/deps/assimp-$A-build.log 2>&1
cmake --install $B > /home/claude/deps/assimp-$A-install.log 2>&1
echo DONE-assimp-$A
