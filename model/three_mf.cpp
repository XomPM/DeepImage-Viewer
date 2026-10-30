// DeepImage Viewer — native 3MF reader
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
// Supports core spec meshes, components, build transforms, units, basematerials, colorgroups,
// texture2dgroups and the production extension (multi-file packages from Bambu Studio / PrusaSlicer).
#include "model_internal.h"
#include "miniz.h"

#include <windows.h>
#include <charconv>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace dv {
namespace {

struct Tag {
    std::string_view name;      // local name (prefix stripped)
    bool closing = false, selfClosing = false;
    std::vector<std::pair<std::string_view, std::string_view>> attrs;
    std::string_view attr(std::string_view n) const {
        for (auto& [k, v] : attrs) {
            auto c = k.find(':');
            std::string_view local = c == std::string_view::npos ? k : k.substr(c + 1);
            if (local == n) return v;
        }
        return {};
    }
};

// Minimal pull tokenizer for machine-written XML (no DTD, entities only in attribute text we do not need).
class XmlScan {
public:
    explicit XmlScan(std::string_view s) : s_(s) {}
    bool next(Tag& t) {
        for (;;) {
            size_t lt = s_.find('<', p_);
            if (lt == std::string_view::npos) return false;
            p_ = lt + 1;
            if (p_ >= s_.size()) return false;
            char c = s_[p_];
            if (c == '?' || c == '!') { // declaration / comment / CDATA
                size_t end = s_.find(c == '!' && s_.substr(p_, 3) == "!--" ? "-->" : ">", p_);
                if (end == std::string_view::npos) return false;
                p_ = end + 1;
                continue;
            }
            t.attrs.clear();
            t.closing = c == '/';
            if (t.closing) ++p_;
            size_t ns = p_;
            while (p_ < s_.size() && !isspace((unsigned char)s_[p_]) && s_[p_] != '>' && s_[p_] != '/') ++p_;
            std::string_view full = s_.substr(ns, p_ - ns);
            auto colon = full.find(':');
            t.name = colon == std::string_view::npos ? full : full.substr(colon + 1);
            t.selfClosing = false;
            for (;;) {
                while (p_ < s_.size() && isspace((unsigned char)s_[p_])) ++p_;
                if (p_ >= s_.size()) return false;
                if (s_[p_] == '>') { ++p_; break; }
                if (s_[p_] == '/') { t.selfClosing = true; ++p_; continue; }
                size_t ks = p_;
                while (p_ < s_.size() && s_[p_] != '=' && !isspace((unsigned char)s_[p_]) && s_[p_] != '>') ++p_;
                std::string_view key = s_.substr(ks, p_ - ks);
                while (p_ < s_.size() && (isspace((unsigned char)s_[p_]) || s_[p_] == '=')) ++p_;
                if (p_ >= s_.size()) return false;
                char q = s_[p_];
                if (q != '"' && q != '\'') continue;
                size_t vs = ++p_;
                size_t ve = s_.find(q, vs);
                if (ve == std::string_view::npos) return false;
                t.attrs.emplace_back(key, s_.substr(vs, ve - vs));
                p_ = ve + 1;
            }
            return true;
        }
    }
private:
    std::string_view s_;
    size_t p_ = 0;
};

float toF(std::string_view s) {
    float v = 0;
    while (!s.empty() && isspace((unsigned char)s.front())) s.remove_prefix(1);
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}
int toI(std::string_view s, int def = -1) {
    if (s.empty()) return def;
    int v = def;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}
bool parseColor(std::string_view s, float c[4]) {
    if (s.size() < 7 || s[0] != '#') return false;
    auto hx = [&](size_t i) { int v = 0; std::from_chars(s.data() + i, s.data() + i + 2, v, 16); return v / 255.f; };
    c[0] = hx(1); c[1] = hx(3); c[2] = hx(5);
    c[3] = s.size() >= 9 ? hx(7) : 1.f;
    return true;
}

struct Xf { float m[12]{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}; };
Xf parseXf(std::string_view s) {
    Xf x;
    if (s.empty()) return x;
    int i = 0;
    size_t p = 0;
    while (i < 12 && p < s.size()) {
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        size_t e = p;
        while (e < s.size() && !isspace((unsigned char)s[e])) ++e;
        if (e > p) x.m[i++] = toF(s.substr(p, e - p));
        p = e;
    }
    return x;
}
Xf mul(const Xf& a, const Xf& b) { // apply a then b
    Xf r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 3; ++col) {
            float v = (row == 3 ? b.m[9 + col] : 0);
            for (int k = 0; k < 3; ++k) v += a.m[row * 3 + k] * b.m[k * 3 + col];
            r.m[row * 3 + col] = v;
        }
    return r;
}

struct Tri { int v[3]; int pid, p[3]; };
struct Object {
    std::vector<float> verts;
    std::vector<Tri> tris;
    int pid = -1, pindex = -1;
    struct Comp { int id; Xf xf; std::string path; };
    std::vector<Comp> comps;
};
struct TexGroup { int texId = -1; std::vector<float> uv; };
struct ModelFile {
    float unit = 1;
    std::unordered_map<int, Object> objects;
    std::unordered_map<int, std::vector<std::array<float, 4>>> bases, colors;
    std::unordered_map<int, TexGroup> texGroups;
    std::unordered_map<int, std::string> textures; // id -> package path
    struct Item { int id; Xf xf; std::string path; };
    std::vector<Item> build;
};

class Package {
public:
    explicit Package(const std::wstring& path) {
        if (!readFile(path)) throw std::runtime_error("Cannot open 3MF file.");
        memset(&zip_, 0, sizeof zip_);
        if (!mz_zip_reader_init_mem(&zip_, data_.data(), data_.size(), 0)) throw std::runtime_error("3MF is not a valid ZIP package.");
        open_ = true;
    }
    ~Package() { if (open_) mz_zip_reader_end(&zip_); }
    bool get(std::string name, std::string& out) {
        if (!name.empty() && name[0] == '/') name.erase(0, 1);
        int idx = mz_zip_reader_locate_file(&zip_, name.c_str(), nullptr, 0);
        if (idx < 0) {
            // case-insensitive fallback
            for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip_); ++i) {
                char fn[512];
                mz_zip_reader_get_filename(&zip_, i, fn, sizeof fn);
                if (_stricmp(fn, name.c_str()) == 0) { idx = (int)i; break; }
            }
        }
        if (idx < 0) return false;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip_, idx, &st) || st.m_uncomp_size > (1ull << 31)) return false;
        out.resize((size_t)st.m_uncomp_size);
        return mz_zip_reader_extract_to_mem(&zip_, idx, out.data(), out.size(), 0) != 0;
    }
private:
    bool readFile(const std::wstring& p) {
        HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{};
        bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (1ll << 31);
        if (ok) {
            data_.resize((size_t)sz.QuadPart);
            DWORD rd = 0;
            ok = ReadFile(h, data_.data(), (DWORD)data_.size(), &rd, nullptr) && rd == data_.size();
        }
        CloseHandle(h);
        return ok;
    }
    std::vector<uint8_t> data_;
    mz_zip_archive zip_;
    bool open_ = false;
};

float unitScale(std::string_view u) {
    if (u == "micron") return 0.001f;
    if (u == "centimeter") return 10.f;
    if (u == "inch") return 25.4f;
    if (u == "foot") return 304.8f;
    if (u == "meter") return 1000.f;
    return 1.f;
}

void parseModel(const std::string& xml, ModelFile& mf) {
    XmlScan sc(xml);
    Tag t;
    Object* obj = nullptr;
    int objId = -1;
    std::vector<std::array<float, 4>>* curList = nullptr;
    TexGroup* curTex = nullptr;
    bool inMesh = false;
    while (sc.next(t)) {
        const auto& n = t.name;
        if (t.closing) {
            if (n == "object") obj = nullptr;
            else if (n == "basematerials" || n == "colorgroup") curList = nullptr;
            else if (n == "texture2dgroup") curTex = nullptr;
            else if (n == "mesh") inMesh = false;
            continue;
        }
        if (n == "model") mf.unit = unitScale(t.attr("unit"));
        else if (n == "object") {
            objId = toI(t.attr("id"));
            obj = &mf.objects[objId];
            obj->pid = toI(t.attr("pid"));
            obj->pindex = toI(t.attr("pindex"));
            if (t.selfClosing) obj = nullptr;
        } else if (n == "mesh") inMesh = true;
        else if (n == "vertex" && obj && inMesh) {
            obj->verts.push_back(toF(t.attr("x")));
            obj->verts.push_back(toF(t.attr("y")));
            obj->verts.push_back(toF(t.attr("z")));
        } else if (n == "triangle" && obj && inMesh) {
            Tri tr;
            tr.v[0] = toI(t.attr("v1"), 0); tr.v[1] = toI(t.attr("v2"), 0); tr.v[2] = toI(t.attr("v3"), 0);
            tr.pid = toI(t.attr("pid"));
            tr.p[0] = toI(t.attr("p1"));
            tr.p[1] = toI(t.attr("p2"), tr.p[0]);
            tr.p[2] = toI(t.attr("p3"), tr.p[0]);
            obj->tris.push_back(tr);
        } else if (n == "component" && obj) {
            obj->comps.push_back({toI(t.attr("objectid")), parseXf(t.attr("transform")), std::string(t.attr("path"))});
        } else if (n == "basematerials") {
            curList = &mf.bases[toI(t.attr("id"))];
        } else if (n == "base" && curList) {
            std::array<float, 4> c{0.72f, 0.74f, 0.78f, 1};
            parseColor(t.attr("displaycolor"), c.data());
            curList->push_back(c);
        } else if (n == "colorgroup") {
            curList = &mf.colors[toI(t.attr("id"))];
        } else if (n == "color" && curList) {
            std::array<float, 4> c{0.72f, 0.74f, 0.78f, 1};
            parseColor(t.attr("color"), c.data());
            curList->push_back(c);
        } else if (n == "texture2d") {
            mf.textures[toI(t.attr("id"))] = std::string(t.attr("path"));
        } else if (n == "texture2dgroup") {
            curTex = &mf.texGroups[toI(t.attr("id"))];
            curTex->texId = toI(t.attr("texid"));
        } else if (n == "tex2coord" && curTex) {
            curTex->uv.push_back(toF(t.attr("u")));
            curTex->uv.push_back(1.f - toF(t.attr("v")));
        } else if (n == "item") {
            mf.build.push_back({toI(t.attr("objectid")), parseXf(t.attr("transform")), std::string(t.attr("path"))});
        }
    }
}

class Builder {
public:
    Builder(Package& pkg, MeshModel& out) : pkg_(pkg), out_(out) {}

    ModelFile& file(const std::string& path) {
        auto it = files_.find(path);
        if (it != files_.end()) return it->second;
        auto& mf = files_[path];
        std::string xml;
        if (pkg_.get(path, xml)) parseModel(xml, mf);
        return mf;
    }

    void emit(const std::string& path, int id, const Xf& xf, int depth) {
        if (depth > 32) return;
        ModelFile& mf = file(path);
        auto it = mf.objects.find(id);
        if (it == mf.objects.end()) return;
        const Object& o = it->second;
        Xf scaled = xf;
        if (!o.tris.empty()) emitMesh(mf, o, scaled);
        for (auto& c : o.comps) emit(c.path.empty() ? path : c.path, c.id, mul(c.xf, xf), depth + 1);
    }

    void emitMesh(ModelFile& mf, const Object& o, const Xf& xf) {
        const size_t nv = o.verts.size() / 3;
        auto P = [&](int i, float* w) {
            const float* v = &o.verts[(size_t)i * 3];
            for (int c = 0; c < 3; ++c) w[c] = (v[0] * xf.m[c] + v[1] * xf.m[3 + c] + v[2] * xf.m[6 + c] + xf.m[9 + c]) * mf.unit;
        };
        for (const Tri& t : o.tris) {
            if (t.v[0] < 0 || t.v[1] < 0 || t.v[2] < 0 || (size_t)t.v[0] >= nv || (size_t)t.v[1] >= nv || (size_t)t.v[2] >= nv) continue;
            int pid = t.pid >= 0 ? t.pid : o.pid;
            int p[3] = {t.p[0], t.p[1], t.p[2]};
            if (t.pid < 0) p[0] = p[1] = p[2] = o.pindex;
            if (p[0] < 0) p[0] = o.pindex >= 0 ? o.pindex : 0;
            for (int k = 1; k < 3; ++k) if (p[k] < 0) p[k] = p[0];
            float col[3][4];
            bool perVertex = false;
            int texId = -1;
            const TexGroup* tg = nullptr;
            std::array<float, 4> def{0.72f, 0.74f, 0.78f, 1};
            for (int k = 0; k < 3; ++k) memcpy(col[k], def.data(), 16);
            if (auto b = mf.bases.find(pid); b != mf.bases.end()) {
                if ((size_t)p[0] < b->second.size()) for (int k = 0; k < 3; ++k) memcpy(col[k], b->second[p[0]].data(), 16);
            } else if (auto c = mf.colors.find(pid); c != mf.colors.end()) {
                for (int k = 0; k < 3; ++k) if ((size_t)p[k] < c->second.size()) memcpy(col[k], c->second[p[k]].data(), 16);
                perVertex = memcmp(col[0], col[1], 16) || memcmp(col[0], col[2], 16);
            } else if (auto g = mf.texGroups.find(pid); g != mf.texGroups.end()) {
                tg = &g->second;
                texId = tg->texId;
            }
            Submesh& sm = bucket(mf, texId, perVertex ? nullptr : col[0], perVertex);
            float w[3][3];
            for (int k = 0; k < 3; ++k) P(t.v[k], w[k]);
            float ux = w[1][0] - w[0][0], uy = w[1][1] - w[0][1], uz = w[1][2] - w[0][2];
            float vx = w[2][0] - w[0][0], vy = w[2][1] - w[0][1], vz = w[2][2] - w[0][2];
            float n[3] = {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
            const uint32_t base = (uint32_t)(sm.positions.size() / 3);
            for (int k = 0; k < 3; ++k) {
                sm.positions.insert(sm.positions.end(), {w[k][0], w[k][1], w[k][2]});
                sm.normals.insert(sm.normals.end(), {n[0], n[1], n[2]});
                if (perVertex) sm.colors.insert(sm.colors.end(), {col[k][0], col[k][1], col[k][2], 1});
                if (tg) {
                    size_t ui = (size_t)p[k] * 2;
                    float u = ui + 1 < tg->uv.size() ? tg->uv[ui] : 0, v = ui + 1 < tg->uv.size() ? tg->uv[ui + 1] : 0;
                    sm.uvs.insert(sm.uvs.end(), {u, v});
                }
                sm.indices.push_back(base + k);
            }
        }
    }

private:
    Submesh& bucket(ModelFile& mf, int texId, const float* color, bool perVertex) {
        char key[96];
        if (texId >= 0) snprintf(key, sizeof key, "t%p:%d", (void*)&mf, texId);
        else if (perVertex) snprintf(key, sizeof key, "vc");
        else snprintf(key, sizeof key, "c%.4f,%.4f,%.4f", color[0], color[1], color[2]);
        auto it = buckets_.find(key);
        if (it != buckets_.end()) return out_.submeshes[it->second];
        out_.submeshes.emplace_back();
        Submesh& s = out_.submeshes.back();
        if (color) { for (int k = 0; k < 3; ++k) s.baseColor[k] = color[k]; s.baseColor[3] = 1; }
        if (texId >= 0) {
            s.baseColor[0] = s.baseColor[1] = s.baseColor[2] = 1;
            auto tp = mf.textures.find(texId);
            std::string bytes;
            if (tp != mf.textures.end() && pkg_.get(tp->second, bytes)) s.texture.assign(bytes.begin(), bytes.end());
        }
        buckets_[key] = out_.submeshes.size() - 1;
        return s;
    }
    Package& pkg_;
    MeshModel& out_;
    std::map<std::string, ModelFile> files_;
    std::map<std::string, size_t> buckets_;
};

std::string rootModelPath(Package& pkg) {
    std::string rels;
    if (pkg.get("_rels/.rels", rels)) {
        XmlScan sc(rels);
        Tag t;
        while (sc.next(t))
            if (t.name == "Relationship" && t.attr("Type").find("3dmodel") != std::string_view::npos) return std::string(t.attr("Target"));
    }
    return "/3D/3dmodel.model";
}

} // namespace

void load3mf(const std::wstring& path, MeshModel& out) {
    Package pkg(path);
    Builder b(pkg, out);
    const std::string root = rootModelPath(pkg);
    ModelFile& mf = b.file(root);
    if (mf.objects.empty() && mf.build.empty()) throw std::runtime_error("3MF contains no model.");
    if (!mf.build.empty()) {
        for (auto& item : mf.build) b.emit(item.path.empty() ? root : item.path, item.id, item.xf, 0);
    } else {
        for (auto& [id, o] : mf.objects) if (!o.tris.empty()) b.emit(root, id, Xf{}, 0);
    }
    // drop empty buckets
    out.submeshes.erase(std::remove_if(out.submeshes.begin(), out.submeshes.end(), [](const Submesh& s) { return s.indices.empty(); }),
                        out.submeshes.end());
    if (out.submeshes.empty()) throw std::runtime_error("3MF contains no triangles.");
    std::map<std::tuple<float, float, float>, int> mats;
    for (auto& s : out.submeshes) mats[{s.baseColor[0], s.baseColor[1], s.baseColor[2]}] = 1;
    out.materialCount = (uint32_t)mats.size();
    out.upAxis = 2;
    out.units = 1;
}

} // namespace dv
