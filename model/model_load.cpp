// DeepImage Viewer — model loading (OpenCascade for CAD, Assimp for meshes)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
// CAD tessellation approach follows DMIND 3D Thumbnail (step_reader.cpp), extended with XCAF colours.
#include "model_internal.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

// ---- OpenCascade
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

// ---- Assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/material.h>

namespace dv {

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring lowerExt(const std::wstring& path) {
    auto dot = path.find_last_of(L'.');
    auto slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return {};
    std::wstring e = path.substr(dot + 1);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

// ======================================================================== OCCT
namespace {

struct ColorKey {
    float r, g, b;
    bool operator<(const ColorKey& o) const { return std::tie(r, g, b) < std::tie(o.r, o.g, o.b); }
};

class CadBuilder {
public:
    explicit CadBuilder(MeshModel& out) : out_(out) {}

    Submesh& bucket(const ColorKey& c) {
        auto it = buckets_.find(c);
        if (it != buckets_.end()) return out_.submeshes[it->second];
        out_.submeshes.emplace_back();
        auto& s = out_.submeshes.back();
        s.baseColor[0] = c.r; s.baseColor[1] = c.g; s.baseColor[2] = c.b; s.baseColor[3] = 1;
        buckets_[c] = out_.submeshes.size() - 1;
        return s;
    }

    // Tessellate (once per shape) and append faces with location + colour.
    void addShape(const TopoDS_Shape& shape, const TopLoc_Location& loc, const ColorKey* color,
                  const Handle(XCAFDoc_ShapeTool)& st, const Handle(XCAFDoc_ColorTool)& ct, const TDF_Label& part) {
        mesh(shape);
        const gp_Trsf trsf = loc.Transformation();
        for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face face = TopoDS::Face(ex.Current());
            TopLoc_Location fl;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, fl);
            if (tri.IsNull() || tri->NbTriangles() == 0) continue;
            ColorKey key = color ? *color : defaultColor();
            if (!ct.IsNull() && !part.IsNull()) {
                TDF_Label sub;
                Quantity_Color q;
                if (st->FindSubShape(part, face, sub) &&
                    (ct->GetColor(sub, XCAFDoc_ColorSurf, q) || ct->GetColor(sub, XCAFDoc_ColorGen, q))) {
                    key = toKey(q);
                }
            }
            if (!tri->HasNormals()) tri->ComputeNormals();
            Submesh& sm = bucket(key);
            const gp_Trsf ft = trsf * fl.Transformation();
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            const uint32_t base = (uint32_t)(sm.positions.size() / 3);
            for (int i = 1; i <= tri->NbNodes(); ++i) {
                gp_Pnt p = tri->Node(i).Transformed(ft);
                sm.positions.insert(sm.positions.end(), {(float)p.X(), (float)p.Y(), (float)p.Z()});
                gp_Dir n = tri->HasNormals() ? gp_Dir(tri->Normal(i)) : gp_Dir(0, 0, 1);
                n.Transform(ft);
                if (reversed) n.Reverse();
                sm.normals.insert(sm.normals.end(), {(float)n.X(), (float)n.Y(), (float)n.Z()});
            }
            for (int i = 1; i <= tri->NbTriangles(); ++i) {
                int a, b, c;
                tri->Triangle(i).Get(a, b, c);
                if (reversed) std::swap(b, c);
                sm.indices.insert(sm.indices.end(), {base + a - 1, base + b - 1, base + c - 1});
            }
        }
    }

    void setDeflection(const TopoDS_Shape& whole) {
        Bnd_Box box;
        BRepBndLib::Add(whole, box, false);
        if (box.IsVoid()) throw std::runtime_error("CAD model has no geometry.");
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        double size = std::max({x1 - x0, y1 - y0, z1 - z0});
        if (!std::isfinite(size) || size <= 0) throw std::runtime_error("CAD model has invalid bounds.");
        deflection_ = std::max(1e-6, size * 0.0015);
    }

    static ColorKey toKey(const Quantity_Color& q) {
        // Quantity_Color stores linear RGB; convert to sRGB for display.
        double r, g, b;
        q.Values(r, g, b, Quantity_TOC_sRGB);
        // Many IGES/STEP exporters write pure black as "no colour"; keep shapes readable.
        if (r + g + b < 0.02) return defaultColor();
        return {(float)r, (float)g, (float)b};
    }
    static ColorKey defaultColor() { return {0.72f, 0.74f, 0.78f}; }

private:
    void mesh(const TopoDS_Shape& s) {
        if (meshed_.count(s.TShape().get())) return;
        BRepMesh_IncrementalMesh m(s, deflection_, false, 0.35, true);
        meshed_[s.TShape().get()] = true;
    }
    MeshModel& out_;
    std::map<ColorKey, size_t> buckets_;
    std::map<const void*, bool> meshed_;
    double deflection_ = 0.1;
};

void traverse(CadBuilder& b, const Handle(XCAFDoc_ShapeTool)& st, const Handle(XCAFDoc_ColorTool)& ct,
              const TDF_Label& label, const TopLoc_Location& loc, const ColorKey* inherited, int depth) {
    if (depth > 64) return;
    Quantity_Color q;
    ColorKey own;
    const ColorKey* color = inherited;
    if (ct->GetColor(label, XCAFDoc_ColorSurf, q) || ct->GetColor(label, XCAFDoc_ColorGen, q)) {
        own = CadBuilder::toKey(q);
        color = &own;
    }
    if (XCAFDoc_ShapeTool::IsAssembly(label)) {
        TDF_LabelSequence comps;
        XCAFDoc_ShapeTool::GetComponents(label, comps, false);
        for (int i = 1; i <= comps.Length(); ++i) {
            const TDF_Label comp = comps.Value(i);
            TDF_Label ref;
            if (!XCAFDoc_ShapeTool::GetReferredShape(comp, ref)) continue;
            ColorKey compColor;
            const ColorKey* cc = color;
            if (ct->GetColor(comp, XCAFDoc_ColorSurf, q) || ct->GetColor(comp, XCAFDoc_ColorGen, q)) {
                compColor = CadBuilder::toKey(q);
                cc = &compColor;
            }
            traverse(b, st, ct, ref, loc * XCAFDoc_ShapeTool::GetLocation(comp), cc, depth + 1);
        }
        return;
    }
    TDF_Label referred;
    if (XCAFDoc_ShapeTool::IsReference(label) && XCAFDoc_ShapeTool::GetReferredShape(label, referred)) {
        traverse(b, st, ct, referred, loc * XCAFDoc_ShapeTool::GetLocation(label), color, depth + 1);
        return;
    }
    TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(label);
    if (shape.IsNull()) return;
    // Shape stored on the label may carry its own location; the face query below needs the
    // unlocated part shape, so strip and fold it into the instance location.
    TopLoc_Location own_loc = shape.Location();
    shape.Location(TopLoc_Location());
    b.addShape(shape, loc * own_loc, color, st, ct, label);
}

void loadCadXcaf(const std::wstring& path, const std::wstring& ext, MeshModel& out) {
    Handle(TDocStd_Document) doc;
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    app->NewDocument("BinXCAF", doc);
    bool ok = false;
    if (ext == L"step" || ext == L"stp") {
        STEPCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(false);
        reader.SetLayerMode(false);
        reader.SetPropsMode(false);
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open STEP file.");
        if (reader.ReadStream("model.step", in) != IFSelect_RetDone) throw std::runtime_error("Cannot parse STEP file.");
        ok = reader.Transfer(doc);
    } else {
        IGESCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(false);
        reader.SetLayerMode(false);
        const std::string u = utf8(path);
        if (reader.ReadFile(u.c_str()) != IFSelect_RetDone) throw std::runtime_error("Cannot parse IGES file.");
        ok = reader.Transfer(doc);
    }
    if (!ok) throw std::runtime_error("CAD transfer failed.");
    Handle(XCAFDoc_ShapeTool) st = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) ct = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    TDF_LabelSequence roots;
    st->GetFreeShapes(roots);
    if (roots.IsEmpty()) throw std::runtime_error("CAD file contains no shapes.");
    // Deflection from the overall size.
    BRep_Builder bb;
    TopoDS_Compound all;
    bb.MakeCompound(all);
    for (int i = 1; i <= roots.Length(); ++i) {
        TopoDS_Shape s = XCAFDoc_ShapeTool::GetShape(roots.Value(i));
        if (!s.IsNull()) bb.Add(all, s);
    }
    CadBuilder builder(out);
    builder.setDeflection(all);
    for (int i = 1; i <= roots.Length(); ++i) traverse(builder, st, ct, roots.Value(i), TopLoc_Location(), nullptr, 0);
    app->Close(doc);
}

void loadBrep(const std::wstring& path, MeshModel& out) {
    TopoDS_Shape shape;
    BRep_Builder bb;
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open BREP file.");
    BRepTools::Read(shape, in, bb);
    if (shape.IsNull()) throw std::runtime_error("BREP file contains no shape.");
    CadBuilder builder(out);
    builder.setDeflection(shape);
    builder.addShape(shape, TopLoc_Location(), nullptr, nullptr, nullptr, TDF_Label());
}

} // namespace

void loadCad(const std::wstring& path, const std::wstring& ext, MeshModel& out) {
    try {
        if (ext == L"brep" || ext == L"brp") loadBrep(path, out);
        else loadCadXcaf(path, ext, out);
    } catch (const Standard_Failure& f) {
        const char* m = f.GetMessageString();
        throw std::runtime_error(std::string("CAD conversion failed: ") + (m && *m ? m : "OpenCascade error"));
    }
    out.upAxis = 2;
    out.units = 1;
}

// ======================================================================== Assimp
namespace {

void readTexture(const aiScene* scene, const aiMaterial* mat, const std::wstring& dir, Submesh& sm) {
    aiString tex;
    if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tex) != AI_SUCCESS &&
        mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex) != AI_SUCCESS) return;
    if (const aiTexture* emb = scene->GetEmbeddedTexture(tex.C_Str())) {
        if (emb->mHeight == 0) { // compressed (png/jpg) payload
            const uint8_t* p = reinterpret_cast<const uint8_t*>(emb->pcData);
            sm.texture.assign(p, p + emb->mWidth);
        }
        return; // raw ARGB embedded textures are rare; skip
    }
    std::string rel = tex.C_Str();
    if (rel.empty() || rel.find("://") != std::string::npos) return;
    for (auto& c : rel) if (c == '/') c = '\\';
    int n = MultiByteToWideChar(CP_UTF8, 0, rel.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, rel.c_str(), -1, w.data(), n);
    std::filesystem::path p(w);
    if (p.is_relative()) p = std::filesystem::path(dir) / p;
    std::error_code ec;
    auto size = std::filesystem::file_size(p, ec);
    if (ec || size == 0 || size > 64ull * 1024 * 1024) {
        // try the bare file name next to the model (common with exported packs)
        p = std::filesystem::path(dir) / p.filename();
        size = std::filesystem::file_size(p, ec);
        if (ec || size == 0 || size > 64ull * 1024 * 1024) return;
    }
    std::ifstream in(p, std::ios::binary);
    sm.texture.resize((size_t)size);
    in.read(reinterpret_cast<char*>(sm.texture.data()), (std::streamsize)size);
    if (!in) sm.texture.clear();
}

} // namespace

void loadMesh(const std::wstring& path, const std::wstring& ext, MeshModel& out, bool withTextures) {
    Assimp::Importer imp;
    imp.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE);
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    unsigned flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals |
                     aiProcess_PreTransformVertices | aiProcess_SortByPType | aiProcess_ValidateDataStructure;
    const aiScene* scene = imp.ReadFile(utf8(path), flags);
    if (!scene || !scene->mRootNode) {
        std::string e = imp.GetErrorString();
        throw std::runtime_error(e.empty() ? "Cannot read 3D file." : e);
    }
    const std::wstring dir = std::filesystem::path(path).parent_path().wstring();
    std::vector<int> texOfMaterial(scene->mNumMaterials, -1);
    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];
        if (!m->HasPositions() || m->mNumVertices == 0) continue;
        const bool points = (m->mPrimitiveTypes & aiPrimitiveType_POINT) && !(m->mPrimitiveTypes & aiPrimitiveType_TRIANGLE);
        if (!points && !(m->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) continue;
        Submesh sm;
        sm.isPoints = points;
        sm.positions.resize(m->mNumVertices * 3);
        sm.normals.resize(m->mNumVertices * 3);
        for (unsigned v = 0; v < m->mNumVertices; ++v) {
            sm.positions[v * 3 + 0] = m->mVertices[v].x;
            sm.positions[v * 3 + 1] = m->mVertices[v].y;
            sm.positions[v * 3 + 2] = m->mVertices[v].z;
            if (m->HasNormals()) {
                sm.normals[v * 3 + 0] = m->mNormals[v].x;
                sm.normals[v * 3 + 1] = m->mNormals[v].y;
                sm.normals[v * 3 + 2] = m->mNormals[v].z;
            } else {
                sm.normals[v * 3 + 2] = 1;
            }
        }
        if (m->HasVertexColors(0)) {
            sm.colors.resize(m->mNumVertices * 4);
            for (unsigned v = 0; v < m->mNumVertices; ++v) {
                const aiColor4D& c = m->mColors[0][v];
                sm.colors[v * 4 + 0] = c.r; sm.colors[v * 4 + 1] = c.g; sm.colors[v * 4 + 2] = c.b; sm.colors[v * 4 + 3] = c.a;
            }
        }
        if (points) {
            sm.indices.resize(m->mNumVertices);
            for (unsigned v = 0; v < m->mNumVertices; ++v) sm.indices[v] = v;
        } else {
            sm.indices.reserve(m->mNumFaces * 3);
            for (unsigned f = 0; f < m->mNumFaces; ++f) {
                const aiFace& face = m->mFaces[f];
                if (face.mNumIndices != 3) continue;
                sm.indices.insert(sm.indices.end(), {face.mIndices[0], face.mIndices[1], face.mIndices[2]});
            }
            if (sm.indices.empty()) continue;
        }
        // material
        sm.baseColor[0] = 0.72f; sm.baseColor[1] = 0.74f; sm.baseColor[2] = 0.78f; sm.baseColor[3] = 1;
        if (m->mMaterialIndex < scene->mNumMaterials) {
            const aiMaterial* mat = scene->mMaterials[m->mMaterialIndex];
            aiColor4D c;
            if (mat->Get(AI_MATKEY_BASE_COLOR, c) == AI_SUCCESS || mat->Get(AI_MATKEY_COLOR_DIFFUSE, c) == AI_SUCCESS) {
                // Many exporters write black diffuse for textured materials; keep grey then.
                const bool black = c.r + c.g + c.b < 0.001f;
                if (!black) { sm.baseColor[0] = c.r; sm.baseColor[1] = c.g; sm.baseColor[2] = c.b; }
                sm.baseColor[3] = c.a > 0 ? c.a : 1;
            }
            float opacity = 1;
            if (mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity > 0 && opacity < 1) sm.baseColor[3] = opacity;
            if (withTextures && m->HasTextureCoords(0)) {
                readTexture(scene, mat, dir, sm);
                if (!sm.texture.empty()) {
                    sm.uvs.resize(m->mNumVertices * 2);
                    for (unsigned v = 0; v < m->mNumVertices; ++v) {
                        sm.uvs[v * 2 + 0] = m->mTextureCoords[0][v].x;
                        sm.uvs[v * 2 + 1] = 1.0f - m->mTextureCoords[0][v].y;
                    }
                    if (sm.baseColor[0] + sm.baseColor[1] + sm.baseColor[2] < 0.01f) sm.baseColor[0] = sm.baseColor[1] = sm.baseColor[2] = 1;
                }
            }
        }
        out.submeshes.push_back(std::move(sm));
    }
    out.materialCount = scene->mNumMaterials;
    const bool zup = ext == L"stl" || ext == L"3mf" || ext == L"amf" || ext == L"off";
    out.upAxis = zup ? 2 : 1;
    out.units = (ext == L"stl" || ext == L"3mf" || ext == L"amf") ? 1 : 0;
    if (out.submeshes.empty()) throw std::runtime_error("The file contains no triangles.");
}

void finalize(MeshModel& m) {
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    m.triangleCount = 0;
    m.vertexCount = 0;
    for (auto& s : m.submeshes) {
        for (size_t i = 0; i + 2 < s.positions.size(); i += 3)
            for (int k = 0; k < 3; ++k) {
                float v = s.positions[i + k];
                if (!std::isfinite(v)) throw std::runtime_error("Model contains invalid coordinates.");
                mn[k] = std::min(mn[k], v);
                mx[k] = std::max(mx[k], v);
            }
        m.vertexCount += s.positions.size() / 3;
        if (!s.isPoints) m.triangleCount += s.indices.size() / 3;
        // normalise normals
        for (size_t i = 0; i + 2 < s.normals.size(); i += 3) {
            float x = s.normals[i], y = s.normals[i + 1], z = s.normals[i + 2];
            float l = std::sqrt(x * x + y * y + z * z);
            if (l > 1e-12f) { s.normals[i] = x / l; s.normals[i + 1] = y / l; s.normals[i + 2] = z / l; }
            else { s.normals[i] = 0; s.normals[i + 1] = 0; s.normals[i + 2] = 1; }
        }
    }
    if (m.vertexCount == 0) throw std::runtime_error("The file contains no geometry.");
    for (int k = 0; k < 3; ++k) { m.boundsMin[k] = mn[k]; m.boundsMax[k] = mx[k]; }
    if (m.materialCount == 0) {
        std::map<std::tuple<float, float, float>, int> u;
        for (auto& s : m.submeshes) u[{s.baseColor[0], s.baseColor[1], s.baseColor[2]}] = 1;
        m.materialCount = (uint32_t)u.size();
    }
}

void loadModelFile(const std::wstring& path, MeshModel& out, bool withTextures) {
    const std::wstring ext = lowerExt(path);
    if (ext == L"step" || ext == L"stp" || ext == L"iges" || ext == L"igs" || ext == L"brep" || ext == L"brp")
        loadCad(path, ext, out);
    else if (ext == L"3mf")
        load3mf(path, out);
    else
        loadMesh(path, ext, out, withTextures);
    finalize(out);
}

} // namespace dv
