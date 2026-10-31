// DeepImage Viewer — dvmodel.dll public API
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#define DVMODEL_BUILD
#include "dvmodel.h"
#include "model_internal.h"

#include <windows.h>
#include <cstring>
#include <exception>
#include <new>

struct DvModel {
    dv::MeshModel mesh;
    std::vector<DvSubmesh> views;
    DvModelInfo info{};
};

namespace {
void setError(char* error, int len, const char* msg) {
    if (!error || len <= 0) return;
    strncpy(error, msg ? msg : "error", (size_t)len - 1);
    error[len - 1] = 0;
}
}

DV_API DvModel* dvLoadModel(const wchar_t* path, char* error, int errorLength) {
    if (!path) { setError(error, errorLength, "No path"); return nullptr; }
    try {
        auto* m = new DvModel();
        try {
            dv::loadModelFile(path, m->mesh, true);
        } catch (...) { delete m; throw; }
        for (auto& s : m->mesh.submeshes) {
            DvSubmesh v{};
            v.positions = s.positions.data();
            v.normals = s.normals.data();
            v.colors = s.colors.empty() ? nullptr : s.colors.data();
            v.uvs = s.uvs.empty() ? nullptr : s.uvs.data();
            v.indices = s.indices.data();
            v.vertexCount = (uint32_t)(s.positions.size() / 3);
            v.indexCount = (uint32_t)s.indices.size();
            memcpy(v.baseColor, s.baseColor, sizeof v.baseColor);
            v.texture = s.texture.empty() ? nullptr : s.texture.data();
            v.textureSize = (uint32_t)s.texture.size();
            v.isPoints = s.isPoints;
            m->views.push_back(v);
        }
        auto& i = m->info;
        i.submeshCount = (uint32_t)m->views.size();
        i.submeshes = m->views.data();
        i.upAxis = m->mesh.upAxis;
        i.units = m->mesh.units;
        i.materialCount = m->mesh.materialCount;
        i.triangleCount = m->mesh.triangleCount;
        i.vertexCount = m->mesh.vertexCount;
        memcpy(i.boundsMin, m->mesh.boundsMin, sizeof i.boundsMin);
        memcpy(i.boundsMax, m->mesh.boundsMax, sizeof i.boundsMax);
        return m;
    } catch (const std::bad_alloc&) {
        setError(error, errorLength, "Out of memory");
    } catch (const std::exception& e) {
        setError(error, errorLength, e.what());
    } catch (...) {
        setError(error, errorLength, "Unknown error while reading the model");
    }
    return nullptr;
}

DV_API const DvModelInfo* dvModelInfo(const DvModel* m) { return m ? &m->info : nullptr; }
DV_API void dvFreeModel(DvModel* m) { delete m; }

DV_API int dvRenderThumbnail(const wchar_t* path, const DvThumbOptions* o, uint8_t* bgra, char* error, int errorLength) {
    if (!path || !o || !bgra || o->size < 16 || o->size > 2048) { setError(error, errorLength, "Invalid arguments"); return 0; }
    try {
        if (o->preferEmbeddedPreview && dv::lowerExt(path) == L"3mf" && dv::read3mfPreview(path, o->size, bgra)) return 1; // slicer / author picture first
        dv::MeshModel m;
        dv::loadModelFile(path, m, false);
        dv::renderThumbnail(m, o->size, o->background, bgra, o->modelColor, o->colorAllModels != 0);
        return 1;
    } catch (const std::bad_alloc&) {
        setError(error, errorLength, "Out of memory");
    } catch (const std::exception& e) {
        setError(error, errorLength, e.what());
    } catch (...) {
        setError(error, errorLength, "Unknown error while rendering");
    }
    return 0;
}

DV_API const wchar_t* dvSupportedExtensions() {
    return L";stl;3mf;obj;ply;gltf;glb;fbx;step;stp;iges;igs;brep;brp;dae;3ds;amf;off;";
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
