// DeepImage Viewer — Direct3D 11 model viewport implementation
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "model3d.h"
#include "gfx.h"
#include <d3dcompiler.h>
#include <shlwapi.h>
#include <unordered_map>
#include <cstring>

namespace dv {

namespace {

constexpr char kShaders[] = R"(
cbuffer C : register(b0) {
    row_major float4x4 viewProj;
    row_major float4x4 model;
    float4 eye;
    float4 keyDir;
    float4 fillDir;
    float4 base;
    float4 flags;      // x clay, y texture, z vertex colours, w unlit
    float4 sky;
    float4 ground;
    float4 fade;       // xy centre (world xz), z radius (0 = off), w line alpha
};
Texture2D tex : register(t0);
SamplerState smp : register(s0);

struct VI { float3 p : POSITION; float3 n : NORMAL; float4 c : COLOR; float2 uv : TEXCOORD; };
struct VO { float4 p : SV_POSITION; float3 w : WPOS; float3 n : NORMAL; float4 c : COLOR; float2 uv : TEXCOORD; };
VO vsMesh(VI i) {
    VO o;
    float4 wp = mul(float4(i.p, 1), model);
    o.w = wp.xyz;
    o.p = mul(wp, viewProj);
    o.n = mul(i.n, (float3x3)model);
    o.c = i.c;
    o.uv = i.uv;
    return o;
}
float4 psMesh(VO i) : SV_TARGET {
    float3 albedo = base.rgb;
    if (flags.z > 0.5) albedo = i.c.rgb;
    if (flags.y > 0.5) albedo *= tex.Sample(smp, i.uv).rgb;
    if (flags.x > 0.5) albedo = float3(0.86, 0.83, 0.78);
    albedo = max(albedo, 0.06);
    if (flags.w > 0.5) return float4(albedo, 1);
    float3 n = normalize(i.n);
    float3 v = normalize(eye.xyz - i.w);
    if (dot(n, v) < 0) n = -n;
    float3 l = normalize(keyDir.xyz);
    float key = saturate(dot(n, l));
    float fillL = saturate(dot(n, normalize(fillDir.xyz)) * 0.5 + 0.5) * 0.22;
    float3 amb = lerp(ground.rgb, sky.rgb, n.y * 0.5 + 0.5);
    float3 h = normalize(l + v);
    float gloss = flags.x > 0.5 ? 0.25 : 1.0;
    float spec = (pow(saturate(dot(n, h)), 64) * 0.22 + pow(saturate(dot(n, h)), 10) * 0.05) * gloss;
    float fres = pow(1 - saturate(dot(n, v)), 4) * 0.22;
    float3 col = albedo * (amb + key * 0.82 + fillL) + spec + fres * sky.rgb;
    return float4(col, 1);
}

struct LI { float3 p : POSITION; float4 c : COLOR; };
struct LO { float4 p : SV_POSITION; float3 w : WPOS; float4 c : COLOR; };
LO vsLine(LI i) {
    LO o;
    o.w = i.p;
    o.p = mul(float4(i.p, 1), viewProj);
    o.c = i.c;
    return o;
}
float4 psLine(LO i) : SV_TARGET {
    float a = i.c.a * fade.w;
    if (fade.z > 0) a *= saturate(1.0 - length(i.w.xz - fade.xy) / fade.z);
    return float4(i.c.rgb * a, a);
}
float4 psShadow(LO i) : SV_TARGET {
    float2 q = i.c.xy;           // -1..1 across the footprint
    float d = dot(q, q);
    float a = exp(-d * 3.2) * i.c.a * fade.w;
    return float4(0, 0, 0, a);
}
)";

struct MV { float p[3], n[3], c[4], uv[2]; };
struct LV { float p[3], c[4]; };

struct CB {
    float viewProj[16];
    float model[16];
    float eye[4], keyDir[4], fillDir[4], base[4], flags[4], sky[4], ground[4], fade[4];
};

V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 mulv(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross3(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 norm(V3 a) { float l = std::sqrt(dot3(a, a)); return l > 1e-20f ? mulv(a, 1 / l) : V3{0, 1, 0}; }

void matMul(const float* a, const float* b, float* r) {
    float t[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            t[i * 4 + j] = s;
        }
    memcpy(r, t, sizeof t);
}
void lookAt(V3 eye, V3 target, float* m) {
    V3 z = norm(sub(eye, target));
    V3 up = std::fabs(z.y) > 0.999f ? V3{0, 0, -1} : V3{0, 1, 0};
    V3 x = norm(cross3(up, z));
    V3 y = cross3(z, x);
    float r[16] = {x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0, -dot3(x, eye), -dot3(y, eye), -dot3(z, eye), 1};
    memcpy(m, r, sizeof r);
}
void perspective(float fovY, float aspect, float zn, float zf, float* m) {
    float f = 1 / std::tan(fovY / 2);
    float r[16] = {f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, zf / (zn - zf), -1, 0, 0, zn * zf / (zn - zf), 0};
    memcpy(m, r, sizeof r);
}
constexpr float kFov = 35.f * 3.14159265f / 180.f;

ComPtr<ID3DBlob> compile(const char* entry, const char* profile) {
    ComPtr<ID3DBlob> code, err;
    if (FAILED(D3DCompile(kShaders, strlen(kShaders), "dv3d", nullptr, nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
        if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
        return nullptr;
    }
    return code;
}

ComPtr<ID3D11ShaderResourceView> makeTexture(const uint8_t* data, uint32_t size) {
    ComPtr<IStream> st;
    st.Attach(SHCreateMemStream(data, size));
    if (!st) return nullptr;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> f;
    if (FAILED(G.wic->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec)) || FAILED(dec->GetFrame(0, &f))) return nullptr;
    UINT w, h;
    f->GetSize(&w, &h);
    ComPtr<IWICBitmapSource> src = f;
    const UINT maxT = 4096;
    if (w > maxT || h > maxT) {
        float k = (float)maxT / std::max(w, h);
        ComPtr<IWICBitmapScaler> sc;
        G.wic->CreateBitmapScaler(&sc);
        UINT nw = std::max(1u, (UINT)(w * k)), nh = std::max(1u, (UINT)(h * k));
        sc->Initialize(f.Get(), nw, nh, WICBitmapInterpolationModeFant);
        src = sc;
        w = nw; h = nh;
    }
    ComPtr<IWICFormatConverter> conv;
    G.wic->CreateFormatConverter(&conv);
    if (FAILED(conv->Initialize(src.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return nullptr;
    std::vector<uint8_t> px((size_t)w * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) return nullptr;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h;
    td.MipLevels = 0; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ComPtr<ID3D11Texture2D> t;
    if (FAILED(G.d3d->CreateTexture2D(&td, nullptr, &t))) return nullptr;
    G.ctx->UpdateSubresource(t.Get(), 0, nullptr, px.data(), w * 4, 0);
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(G.d3d->CreateShaderResourceView(t.Get(), nullptr, &srv))) return nullptr;
    G.ctx->GenerateMips(srv.Get());
    return srv;
}

ComPtr<ID3D11Buffer> makeBuffer(const void* data, UINT bytes, UINT bind) {
    if (!bytes) return nullptr;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = bytes;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = bind;
    D3D11_SUBRESOURCE_DATA sd{data, 0, 0};
    ComPtr<ID3D11Buffer> b;
    G.d3d->CreateBuffer(&bd, &sd, &b);
    return b;
}

} // namespace

ModelView::~ModelView() { clear(); }

bool ModelView::initPipeline() {
    if (ready_) return true;
    auto d = G.d3d.Get();
    auto vsm = compile("vsMesh", "vs_4_0"), psm = compile("psMesh", "ps_4_0"), vsl = compile("vsLine", "vs_4_0"),
         psl = compile("psLine", "ps_4_0"), pss = compile("psShadow", "ps_4_0");
    if (!vsm || !psm || !vsl || !psl || !pss) return false;
    d->CreateVertexShader(vsm->GetBufferPointer(), vsm->GetBufferSize(), nullptr, &vsMesh_);
    d->CreatePixelShader(psm->GetBufferPointer(), psm->GetBufferSize(), nullptr, &psMesh_);
    d->CreateVertexShader(vsl->GetBufferPointer(), vsl->GetBufferSize(), nullptr, &vsLine_);
    d->CreatePixelShader(psl->GetBufferPointer(), psl->GetBufferSize(), nullptr, &psLine_);
    d->CreatePixelShader(pss->GetBufferPointer(), pss->GetBufferSize(), nullptr, &psShadow_);
    const D3D11_INPUT_ELEMENT_DESC me[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    d->CreateInputLayout(me, 4, vsm->GetBufferPointer(), vsm->GetBufferSize(), &ilMesh_);
    const D3D11_INPUT_ELEMENT_DESC le[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    d->CreateInputLayout(le, 2, vsl->GetBufferPointer(), vsl->GetBufferSize(), &ilLine_);
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(CB);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    d->CreateBuffer(&cbd, nullptr, &cb_);
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    rd.AntialiasedLineEnable = FALSE;
    rd.DepthBias = 0;
    d->CreateRasterizerState(&rd, &rsSolid_);
    rd.FillMode = D3D11_FILL_WIREFRAME;
    d->CreateRasterizerState(&rd, &rsWire_);
    rd.FillMode = D3D11_FILL_SOLID;
    rd.DepthBias = -200;
    rd.SlopeScaledDepthBias = -1.0f;
    d->CreateRasterizerState(&rd, &rsLine_);
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;           // premultiplied output from the shaders
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    d->CreateBlendState(&bd, &bsAlpha_);
    bd.RenderTarget[0].BlendEnable = FALSE;
    d->CreateBlendState(&bd, &bsOpaque_);
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    d->CreateDepthStencilState(&dd, &dsOn_);
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    d->CreateDepthStencilState(&dd, &dsNoWrite_);
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy = 8;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    d->CreateSamplerState(&sd, &sampler_);
    ready_ = true;
    return true;
}

void ModelView::clear() {
    parts_.clear();
    cpuPos_.clear();
    cpuIdx_.clear();
    cpuPoints_.clear();
    edgeVb_.Reset();
    edgeCount_ = 0;
    dirty_ = true;
}

bool ModelView::setModel(const DvModelInfo* info) {
    if (!initPipeline()) return false;
    clear();
    up_ = info->upAxis == 2 ? 2 : 1;
    for (int k = 0; k < 3; ++k) { bmin_[k] = info->boundsMin[k]; bmax_[k] = info->boundsMax[k]; }
    stats = ModelStats{};
    stats.meshes = info->submeshCount;
    stats.materials = info->materialCount;
    stats.vertices = info->vertexCount;
    stats.triangles = info->triangleCount;
    stats.units = info->units;
    for (int k = 0; k < 3; ++k) stats.extent[k] = bmax_[k] - bmin_[k];
    for (uint32_t i = 0; i < info->submeshCount; ++i) {
        const DvSubmesh& s = info->submeshes[i];
        std::vector<MV> v(s.vertexCount);
        for (uint32_t k = 0; k < s.vertexCount; ++k) {
            memcpy(v[k].p, s.positions + k * 3, 12);
            memcpy(v[k].n, s.normals + k * 3, 12);
            if (s.colors) memcpy(v[k].c, s.colors + k * 4, 16);
            else { v[k].c[0] = s.baseColor[0]; v[k].c[1] = s.baseColor[1]; v[k].c[2] = s.baseColor[2]; v[k].c[3] = 1; }
            if (s.uvs) { v[k].uv[0] = s.uvs[k * 2]; v[k].uv[1] = s.uvs[k * 2 + 1]; }
        }
        Part p;
        p.vb = makeBuffer(v.data(), (UINT)(v.size() * sizeof(MV)), D3D11_BIND_VERTEX_BUFFER);
        p.ib = makeBuffer(s.indices, s.indexCount * 4, D3D11_BIND_INDEX_BUFFER);
        p.indexCount = s.indexCount;
        memcpy(p.color, s.baseColor, sizeof p.color);
        p.points = s.isPoints != 0;
        p.vertexColors = s.colors != nullptr;
        if (s.texture && s.textureSize && s.uvs) p.tex = makeTexture(s.texture, s.textureSize);
        if (!p.vb || !p.ib) continue;
        parts_.push_back(p);
        cpuPos_.emplace_back(s.positions, s.positions + s.vertexCount * 3);
        cpuIdx_.emplace_back(s.indices, s.indices + s.indexCount);
        cpuPoints_.push_back(p.points);
    }
    computeFrame();
    view("iso", false);
    if (edges) buildEdges();
    dirty_ = true;
    return !parts_.empty();
}

void ModelView::computeFrame() {
    // rotation into a Y-up world
    float R[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (up_ == 2) { float z[16] = {1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1}; memcpy(R, z, sizeof R); }
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < 8; ++i) {
        float p[4] = {i & 1 ? bmax_[0] : bmin_[0], i & 2 ? bmax_[1] : bmin_[1], i & 4 ? bmax_[2] : bmin_[2], 1}, q[3];
        for (int c = 0; c < 3; ++c) q[c] = p[0] * R[c] + p[1] * R[4 + c] + p[2] * R[8 + c];
        for (int c = 0; c < 3; ++c) { mn[c] = std::min(mn[c], q[c]); mx[c] = std::max(mx[c], q[c]); }
    }
    float T[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -(mn[0] + mx[0]) / 2, -mn[1], -(mn[2] + mx[2]) / 2, 1};
    matMul(R, T, model_);
    float ex = mx[0] - mn[0], ey = mx[1] - mn[1], ez = mx[2] - mn[2];
    center_ = {0, ey / 2, 0};
    radius_ = std::max(1e-6f, 0.5f * std::sqrt(ex * ex + ey * ey + ez * ez));
    // shadow quad sized to the footprint
    float hx = std::max(ex, radius_ * 0.2f) * 0.75f, hz = std::max(ez, radius_ * 0.2f) * 0.75f, y = radius_ * 0.0005f;
    float a = dark_ ? 0.55f : 0.30f;
    LV q[6] = {{{-hx, y, -hz}, {-1, -1, 0, a}}, {{hx, y, -hz}, {1, -1, 0, a}}, {{hx, y, hz}, {1, 1, 0, a}},
               {{-hx, y, -hz}, {-1, -1, 0, a}}, {{hx, y, hz}, {1, 1, 0, a}}, {{-hx, y, hz}, {-1, 1, 0, a}}};
    shadowVb_ = makeBuffer(q, sizeof q, D3D11_BIND_VERTEX_BUFFER);
    buildGrid();
    buildBox();
}

void ModelView::buildGrid() {
    if (!ready_) return;
    float R = radius_;
    float step = std::pow(10.f, std::floor(std::log10(R * 0.5f)));
    float size = std::ceil(R * 6 / step) * step;
    int div = std::min(400, (int)std::lround(size / step));
    std::vector<LV> v;
    D2D1_COLOR_F c = dark_ ? rgba(0x9AA0AD) : rgba(0x5A6070);
    float aMajor = dark_ ? 0.20f : 0.22f, aMinor = dark_ ? 0.07f : 0.08f;
    auto addLine = [&](float x0, float z0, float x1, float z1, float a) {
        v.push_back({{x0, 0, z0}, {c.r, c.g, c.b, a}});
        v.push_back({{x1, 0, z1}, {c.r, c.g, c.b, a}});
    };
    float h = size / 2;
    for (int i = 0; i <= div; ++i) {
        float t = -h + i * step;
        addLine(t, -h, t, h, aMajor);
        addLine(-h, t, h, t, aMajor);
        if (div <= 60 && i < div)
            for (int k = 1; k < 5; ++k) {
                float u = t + k * step / 5;
                addLine(u, -h, u, h, aMinor);
                addLine(-h, u, h, u, aMinor);
            }
    }
    gridVb_ = makeBuffer(v.data(), (UINT)(v.size() * sizeof(LV)), D3D11_BIND_VERTEX_BUFFER);
    gridCount_ = (UINT)v.size();
}

void ModelView::buildBox() {
    if (!ready_ || parts_.empty()) return;
    // box corners in world space (model matrix applied)
    V3 c[8];
    for (int i = 0; i < 8; ++i) {
        float p[3] = {i & 1 ? bmax_[0] : bmin_[0], i & 2 ? bmax_[1] : bmin_[1], i & 4 ? bmax_[2] : bmin_[2]};
        c[i] = {p[0] * model_[0] + p[1] * model_[4] + p[2] * model_[8] + model_[12],
                p[0] * model_[1] + p[1] * model_[5] + p[2] * model_[9] + model_[13],
                p[0] * model_[2] + p[1] * model_[6] + p[2] * model_[10] + model_[14]};
    }
    const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    std::vector<LV> v;
    for (auto& k : e) {
        v.push_back({{c[k[0]].x, c[k[0]].y, c[k[0]].z}, {accent_.r, accent_.g, accent_.b, 1}});
        v.push_back({{c[k[1]].x, c[k[1]].y, c[k[1]].z}, {accent_.r, accent_.g, accent_.b, 1}});
    }
    boxVb_ = makeBuffer(v.data(), (UINT)(v.size() * sizeof(LV)), D3D11_BIND_VERTEX_BUFFER);
    boxCount_ = (UINT)v.size();
}

void ModelView::buildEdges() {
    edgeVb_.Reset();
    edgeCount_ = 0;
    if (stats.triangles > 2'000'000) { edges = false; return; }
    const float q = radius_ * 1e-5f;
    auto key = [&](const float* p) {
        int64_t x = (int64_t)std::llround(p[0] / q), y = (int64_t)std::llround(p[1] / q), z = (int64_t)std::llround(p[2] / q);
        return (uint64_t)(x * 73856093) ^ (uint64_t)(y * 19349663) ^ (uint64_t)(z * 83492791);
    };
    struct E { V3 n; int count; float a[3], b[3]; bool sharp; };
    std::unordered_map<uint64_t, E> map;
    map.reserve((size_t)std::min<uint64_t>(stats.triangles * 2, 6'000'000));
    const float cosT = std::cos(28.f * 3.14159265f / 180.f);
    for (size_t s = 0; s < cpuPos_.size(); ++s) {
        if (cpuPoints_[s]) continue;
        const auto& P = cpuPos_[s];
        const auto& I = cpuIdx_[s];
        for (size_t t = 0; t + 2 < I.size(); t += 3) {
            const float* v[3] = {&P[I[t] * 3], &P[I[t + 1] * 3], &P[I[t + 2] * 3]};
            V3 a{v[0][0], v[0][1], v[0][2]}, b{v[1][0], v[1][1], v[1][2]}, c{v[2][0], v[2][1], v[2][2]};
            V3 n = cross3(sub(b, a), sub(c, a));
            if (dot3(n, n) < 1e-30f) continue;
            n = norm(n);
            for (int k = 0; k < 3; ++k) {
                const float* p0 = v[k];
                const float* p1 = v[(k + 1) % 3];
                uint64_t k0 = key(p0), k1 = key(p1);
                if (k0 == k1) continue;
                uint64_t ek = k0 < k1 ? k0 * 1000003ull ^ k1 : k1 * 1000003ull ^ k0;
                auto it = map.find(ek);
                if (it == map.end()) {
                    E e{n, 1, {p0[0], p0[1], p0[2]}, {p1[0], p1[1], p1[2]}, false};
                    map.emplace(ek, e);
                } else {
                    it->second.count++;
                    if (std::fabs(dot3(it->second.n, n)) < cosT) it->second.sharp = true;
                }
            }
        }
    }
    std::vector<LV> lines;
    D2D1_COLOR_F c = dark_ ? rgba(0x0B0C0E) : rgba(0x1D2027);
    for (auto& [k, e] : map) {
        if (!(e.sharp || e.count == 1)) continue;
        float w0[3], w1[3];
        for (int i = 0; i < 3; ++i) {
            w0[i] = e.a[0] * model_[i] + e.a[1] * model_[4 + i] + e.a[2] * model_[8 + i] + model_[12 + i];
            w1[i] = e.b[0] * model_[i] + e.b[1] * model_[4 + i] + e.b[2] * model_[8 + i] + model_[12 + i];
        }
        lines.push_back({{w0[0], w0[1], w0[2]}, {c.r, c.g, c.b, 0.6f}});
        lines.push_back({{w1[0], w1[1], w1[2]}, {c.r, c.g, c.b, 0.6f}});
    }
    edgeVb_ = makeBuffer(lines.data(), (UINT)(lines.size() * sizeof(LV)), D3D11_BIND_VERTEX_BUFFER);
    edgeCount_ = (UINT)lines.size();
}

void ModelView::setEdges(bool on) {
    edges = on;
    if (on && !edgeVb_) buildEdges();
    dirty_ = true;
}

void ModelView::toggleUp() {
    if (parts_.empty()) return;
    up_ = up_ == 2 ? 1 : 2;
    computeFrame();
    if (edges) buildEdges();
    view("iso", false);
    dirty_ = true;
}

void ModelView::view(const char* name, bool animate) {
    V3 d{1, 0.72f, 1.15f};
    if (!strcmp(name, "front")) d = {0, 0, 1};
    else if (!strcmp(name, "back")) d = {0, 0, -1};
    else if (!strcmp(name, "top")) d = {0, 1, 0.0001f};
    else if (!strcmp(name, "bottom")) d = {0, -1, 0.0001f};
    else if (!strcmp(name, "right")) d = {1, 0, 0};
    else if (!strcmp(name, "left")) d = {-1, 0, 0};
    viewDir(norm(d), animate);
}

void ModelView::viewDir(V3 d, bool animate) {
    d = norm(d);
    float aspect = std::max(0.3f, th_ ? (float)tw_ / th_ : 1.5f);
    float fit = radius_ / std::sin(std::min(kFov, kFov * aspect) / 2) * 1.06f;
    gyaw_ = std::atan2(d.x, d.z);
    gpitch_ = std::asin(clampf(d.y, -0.9999f, 0.9999f));
    gdist_ = fit;
    gtarget_ = center_;
    // shortest yaw path
    while (gyaw_ - yaw_ > 3.14159265f) gyaw_ -= 6.2831853f;
    while (gyaw_ - yaw_ < -3.14159265f) gyaw_ += 6.2831853f;
    if (!animate) { yaw_ = gyaw_; pitch_ = gpitch_; dist_ = gdist_; target_ = gtarget_; }
    dirty_ = true;
}

void ModelView::viewModelAxis(int axis, bool negative) {
    V3 d{model_[axis * 4 + 0], model_[axis * 4 + 1], model_[axis * 4 + 2]};
    if (negative) d = mulv(d, -1);
    if (std::fabs(d.y) > 0.999f) d.z += 0.0001f;
    viewDir(d, true);
}

void ModelView::orbit(float dx, float dy) {
    gyaw_ -= dx * 0.0085f;
    gpitch_ = clampf(gpitch_ + dy * 0.0085f, -1.553f, 1.553f);
    dirty_ = true;
}
void ModelView::pan(float dx, float dy, float viewH) {
    V3 eye = add(target_, mulv(V3{std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)}, dist_));
    V3 z = norm(sub(eye, target_));
    V3 x = norm(cross3(std::fabs(z.y) > 0.999f ? V3{0, 0, -1} : V3{0, 1, 0}, z));
    V3 y = cross3(z, x);
    float k = 2 * dist_ * std::tan(kFov / 2) / std::max(1.f, viewH);
    gtarget_ = add(gtarget_, add(mulv(x, -dx * k), mulv(y, dy * k)));
    dirty_ = true;
}
void ModelView::zoom(float delta) {
    gdist_ = clampf(gdist_ * std::exp(-delta * 0.0012f), radius_ * 0.05f, radius_ * 40);
    dirty_ = true;
}

void ModelView::updateCamera(float dt) {
    float k = 1 - std::exp(-dt * 14.f);
    if (autoRotate) gyaw_ += dt * 0.45f;
    yaw_ += (gyaw_ - yaw_) * k;
    pitch_ += (gpitch_ - pitch_) * k;
    dist_ += (gdist_ - dist_) * k;
    target_ = add(target_, mulv(sub(gtarget_, target_), k));
}

bool ModelView::animating() const {
    if (autoRotate && hasModel()) return true;
    float e = std::fabs(gyaw_ - yaw_) + std::fabs(gpitch_ - pitch_) + std::fabs(gdist_ - dist_) / std::max(radius_, 1e-6f) +
              std::sqrt(dot3(sub(gtarget_, target_), sub(gtarget_, target_))) / std::max(radius_, 1e-6f);
    return e > 1e-4f;
}

void ModelView::matrices(UINT w, UINT h, float* vp, V3& eye) const {
    eye = add(target_, mulv(V3{std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)}, dist_));
    float v[16], p[16];
    lookAt(eye, target_, v);
    perspective(kFov, (float)w / std::max(1u, h), std::max(radius_ * 0.002f, dist_ * 0.01f), dist_ + radius_ * 60, p);
    matMul(v, p, vp);
}

void ModelView::axisDirs(float out[3][3]) const {
    V3 eye = add(target_, mulv(V3{std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)}, dist_));
    V3 z = norm(sub(eye, target_));
    V3 x = norm(cross3(std::fabs(z.y) > 0.999f ? V3{0, 0, -1} : V3{0, 1, 0}, z));
    V3 y = cross3(z, x);
    for (int a = 0; a < 3; ++a) {
        // model axis a -> world via rotation part of model_
        V3 wv{model_[a * 4 + 0], model_[a * 4 + 1], model_[a * 4 + 2]};
        out[a][0] = dot3(wv, x);
        out[a][1] = -dot3(wv, y);
        out[a][2] = dot3(wv, z);
    }
}

void ModelView::ensureTargets(UINT w, UINT h) {
    if (w == tw_ && h == th_ && msColor_) return;
    tw_ = w; th_ = h;
    bmp_.Reset(); msRtv_.Reset(); msDsv_.Reset(); msColor_.Reset(); msDepth_.Reset(); resolved_.Reset();
    samples_ = 1;
    for (UINT s : {8u, 4u, 2u}) {
        UINT q = 0;
        if (SUCCEEDED(G.d3d->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, s, &q)) && q > 0) { samples_ = s; break; }
    }
    if (G.warp) samples_ = std::min(samples_, 4u);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = samples_;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    G.d3d->CreateTexture2D(&td, nullptr, &msColor_);
    G.d3d->CreateRenderTargetView(msColor_.Get(), nullptr, &msRtv_);
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    G.d3d->CreateTexture2D(&td, nullptr, &msDepth_);
    G.d3d->CreateDepthStencilView(msDepth_.Get(), nullptr, &msDsv_);
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    G.d3d->CreateTexture2D(&td, nullptr, &resolved_);
    ComPtr<IDXGISurface> surf;
    resolved_.As(&surf);
    auto bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                      G.dpi, G.dpi);
    G.dc->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &bmp_);
    dirty_ = true;
}

void ModelView::draw(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, UINT w, UINT h, UINT) {
    auto c = G.ctx.Get();
    const float clearC[4] = {0, 0, 0, 0};
    c->ClearRenderTargetView(rtv, clearC);
    c->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    c->OMSetRenderTargets(1, &rtv, dsv);
    D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
    c->RSSetViewports(1, &vp);
    CB cb{};
    V3 eye;
    matrices(w, h, cb.viewProj, eye);
    memcpy(cb.model, model_, sizeof cb.model);
    cb.eye[0] = eye.x; cb.eye[1] = eye.y; cb.eye[2] = eye.z;
    // key light rides with the camera (upper right), fill from the opposite side
    V3 z = norm(sub(eye, target_));
    V3 x = norm(cross3(std::fabs(z.y) > 0.999f ? V3{0, 0, -1} : V3{0, 1, 0}, z));
    V3 y = cross3(z, x);
    V3 key = norm(add(add(mulv(z, 0.75f), mulv(x, 0.45f)), mulv(y, 0.65f)));
    V3 fill = norm(add(add(mulv(z, 0.4f), mulv(x, -0.8f)), mulv(y, -0.1f)));
    cb.keyDir[0] = key.x; cb.keyDir[1] = key.y; cb.keyDir[2] = key.z;
    cb.fillDir[0] = fill.x; cb.fillDir[1] = fill.y; cb.fillDir[2] = fill.z;
    if (dark_) { cb.sky[0] = .42f; cb.sky[1] = .44f; cb.sky[2] = .50f; cb.ground[0] = .16f; cb.ground[1] = .15f; cb.ground[2] = .15f; }
    else { cb.sky[0] = .50f; cb.sky[1] = .52f; cb.sky[2] = .58f; cb.ground[0] = .22f; cb.ground[1] = .21f; cb.ground[2] = .20f; }
    auto upload = [&]() {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(c->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &cb, sizeof cb); c->Unmap(cb_.Get(), 0); }
    };
    c->VSSetConstantBuffers(0, 1, cb_.GetAddressOf());
    c->PSSetConstantBuffers(0, 1, cb_.GetAddressOf());
    c->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    const float bf[4] = {0, 0, 0, 0};

    // ground: shadow + grid (no depth writes)
    c->IASetInputLayout(ilLine_.Get());
    c->VSSetShader(vsLine_.Get(), nullptr, 0);
    c->OMSetBlendState(bsAlpha_.Get(), bf, 0xffffffff);
    c->OMSetDepthStencilState(dsNoWrite_.Get(), 0);
    c->RSSetState(rsSolid_.Get());
    UINT ls = sizeof(LV), off = 0;
    if (grid && shadowVb_ && !parts_.empty()) {
        cb.fade[3] = 1;
        upload();
        c->PSSetShader(psShadow_.Get(), nullptr, 0);
        c->IASetVertexBuffers(0, 1, shadowVb_.GetAddressOf(), &ls, &off);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->Draw(6, 0);
    }
    if (grid && gridVb_) {
        cb.fade[0] = 0; cb.fade[1] = 0; cb.fade[2] = radius_ * 3.2f; cb.fade[3] = 1;
        upload();
        c->PSSetShader(psLine_.Get(), nullptr, 0);
        c->IASetVertexBuffers(0, 1, gridVb_.GetAddressOf(), &ls, &off);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        c->Draw(gridCount_, 0);
    }

    // model
    c->IASetInputLayout(ilMesh_.Get());
    c->VSSetShader(vsMesh_.Get(), nullptr, 0);
    c->PSSetShader(psMesh_.Get(), nullptr, 0);
    c->OMSetBlendState(bsOpaque_.Get(), bf, 0xffffffff);
    c->OMSetDepthStencilState(dsOn_.Get(), 0);
    c->RSSetState(wire ? rsWire_.Get() : rsSolid_.Get());
    UINT ms = sizeof(MV);
    for (auto& p : parts_) {
        memcpy(cb.base, p.color, sizeof cb.base);
        cb.flags[0] = clay ? 1.f : 0.f;
        cb.flags[1] = p.tex && !clay ? 1.f : 0.f;
        cb.flags[2] = p.vertexColors && !clay ? 1.f : 0.f;
        cb.flags[3] = p.points ? 1.f : 0.f;
        upload();
        ID3D11ShaderResourceView* srv = p.tex.Get();
        c->PSSetShaderResources(0, 1, &srv);
        c->IASetVertexBuffers(0, 1, p.vb.GetAddressOf(), &ms, &off);
        c->IASetIndexBuffer(p.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        c->IASetPrimitiveTopology(p.points ? D3D11_PRIMITIVE_TOPOLOGY_POINTLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->DrawIndexed(p.indexCount, 0, 0);
    }
    ID3D11ShaderResourceView* none = nullptr;
    c->PSSetShaderResources(0, 1, &none);

    // overlays
    c->IASetInputLayout(ilLine_.Get());
    c->VSSetShader(vsLine_.Get(), nullptr, 0);
    c->PSSetShader(psLine_.Get(), nullptr, 0);
    c->OMSetBlendState(bsAlpha_.Get(), bf, 0xffffffff);
    c->OMSetDepthStencilState(dsNoWrite_.Get(), 0);
    c->RSSetState(rsLine_.Get());
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    cb.fade[2] = 0; cb.fade[3] = 1;
    upload();
    if (edges && edgeVb_) {
        c->IASetVertexBuffers(0, 1, edgeVb_.GetAddressOf(), &ls, &off);
        c->Draw(edgeCount_, 0);
    }
    if (bbox && boxVb_) {
        c->OMSetDepthStencilState(nullptr, 0);
        c->IASetVertexBuffers(0, 1, boxVb_.GetAddressOf(), &ls, &off);
        c->Draw(boxCount_, 0);
    }
}

bool ModelView::render(UINT w, UINT h) {
    if (!ready_ || parts_.empty() || !w || !h) return false;
    ensureTargets(w, h);
    DWORD now = GetTickCount();
    float dt = lastTick_ ? clampf((now - lastTick_) / 1000.f, 0.f, 0.1f) : 0.016f;
    lastTick_ = now;
    bool moving = animating();
    if (moving) updateCamera(dt);
    if (!dirty_ && !moving) return false;
    dirty_ = false;
    draw(msRtv_.Get(), msDsv_.Get(), w, h, samples_);
    if (samples_ > 1) G.ctx->ResolveSubresource(resolved_.Get(), 0, msColor_.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    else G.ctx->CopyResource(resolved_.Get(), msColor_.Get());
    return true;
}

ComPtr<IWICBitmap> ModelView::screenshot(UINT w, UINT h) {
    if (!hasModel()) return nullptr;
    dirty_ = true;
    render(w, h);
    D3D11_TEXTURE2D_DESC td{};
    resolved_->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> st;
    if (FAILED(G.d3d->CreateTexture2D(&td, nullptr, &st))) return nullptr;
    G.ctx->CopyResource(st.Get(), resolved_.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(G.ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) return nullptr;
    ComPtr<IWICBitmap> out;
    G.wic->CreateBitmapFromMemory(td.Width, td.Height, GUID_WICPixelFormat32bppPBGRA, m.RowPitch, m.RowPitch * td.Height, (BYTE*)m.pData, &out);
    G.ctx->Unmap(st.Get(), 0);
    return out;
}

} // namespace dv
