// DeepImage Viewer — 3D model engine (dvmodel.dll)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
//
// Loads 3D files into GPU-ready triangle meshes and renders thumbnails.
// Exposes a small C ABI so the viewer can load this (large) DLL lazily.
#pragma once
#include <stdint.h>

#ifdef DVMODEL_BUILD
#define DV_API extern "C" __declspec(dllexport)
#else
#define DV_API extern "C" __declspec(dllimport)
#endif

struct DvSubmesh {
    const float* positions;   // xyz * vertexCount
    const float* normals;     // xyz * vertexCount
    const float* colors;      // rgba * vertexCount, or null
    const float* uvs;         // uv * vertexCount, or null
    const uint32_t* indices;  // triangle list (or point list when isPoints)
    uint32_t vertexCount;
    uint32_t indexCount;
    float baseColor[4];       // linear-ish sRGB 0..1
    const uint8_t* texture;   // encoded image (PNG/JPEG...) for base colour, or null
    uint32_t textureSize;
    int isPoints;
};

struct DvModelInfo {
    uint32_t submeshCount;
    const DvSubmesh* submeshes;
    int upAxis;               // 1 = +Y, 2 = +Z
    int units;                // 0 = unknown, 1 = millimetre
    uint32_t materialCount;
    uint64_t triangleCount;
    uint64_t vertexCount;
    float boundsMin[3];
    float boundsMax[3];
};

struct DvThumbOptions {
    uint32_t size;            // output edge in pixels (32..1024)
    uint32_t background;      // 0xAARRGGBB, alpha 0 = transparent
    int preferEmbeddedPreview;// 3MF package thumbnail first
};

typedef struct DvModel DvModel;

DV_API DvModel* dvLoadModel(const wchar_t* path, char* error, int errorLength);
DV_API const DvModelInfo* dvModelInfo(const DvModel* model);
DV_API void dvFreeModel(DvModel* model);
// Writes size*size*4 bytes of premultiplied BGRA (ready for HBITMAP / D2D).
DV_API int dvRenderThumbnail(const wchar_t* path, const DvThumbOptions* options, uint8_t* bgra, char* error, int errorLength);
DV_API const wchar_t* dvSupportedExtensions(); // ";stl;3mf;..." style list
