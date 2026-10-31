#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dv {

struct Submesh {
    std::vector<float> positions, normals, colors, uvs;
    std::vector<uint32_t> indices;
    float baseColor[4]{0.72f, 0.74f, 0.78f, 1};
    std::vector<uint8_t> texture;
    bool isPoints = false;
};

struct MeshModel {
    std::vector<Submesh> submeshes;
    int upAxis = 1;
    int units = 0;
    uint32_t materialCount = 0;
    uint64_t triangleCount = 0, vertexCount = 0;
    float boundsMin[3]{}, boundsMax[3]{};
};

std::string utf8(const std::wstring& w);
std::wstring lowerExt(const std::wstring& path);
void load3mf(const std::wstring& path, MeshModel& out);
void loadModelFile(const std::wstring& path, MeshModel& out, bool withTextures);

// thumbnail rendering (WARP)
bool read3mfPreview(const std::wstring& path, uint32_t size, uint8_t* bgra);
void renderThumbnail(const MeshModel& model, uint32_t size, uint32_t background, uint8_t* bgra, uint32_t modelColor = 0, bool colorAll = false);

} // namespace dv
