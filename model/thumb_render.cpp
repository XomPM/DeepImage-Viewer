// DeepImage Viewer — offscreen thumbnail renderer (Direct3D 11 WARP)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
// Based on DMIND 3D Thumbnail renderer.cpp; adds per-pixel lighting, model colours and isometric fit.
#include "model_internal.h"

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <msopc.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace dv {
namespace {

void hr(HRESULT h, const char* what) {
    if (FAILED(h)) {
        char buf[160];
        snprintf(buf, sizeof buf, "%s failed (0x%08lx)", what, (unsigned long)h);
        throw std::runtime_error(buf);
    }
}

constexpr char kShader[] = R"(
cbuffer C : register(b0) { float4x4 viewProj; float4 lightDir; float4 viewDir; };
struct VI { float3 p : POSITION; float3 n : NORMAL; float4 c : COLOR; };
struct VO { float4 p : SV_POSITION; float3 n : NORMAL; float4 c : COLOR; };
VO vs(VI i) { VO o; o.p = mul(float4(i.p, 1), viewProj); o.n = i.n; o.c = i.c; return o; }
float4 ps(VO i) : SV_TARGET {
    float3 n = normalize(i.n);
    float3 v = normalize(viewDir.xyz);
    if (dot(n, v) < 0) n = -n;
    float3 l = normalize(lightDir.xyz);
    float key = saturate(dot(n, l));
    float fill = saturate(dot(n, normalize(float3(-0.6, 0.3, 0.5)))) * 0.35;
    float hemi = lerp(0.42, 0.62, n.y * 0.5 + 0.5);
    float3 h = normalize(l + v);
    float spec = pow(saturate(dot(n, h)), 48) * 0.18;
    float rim = pow(1 - saturate(dot(n, v)), 3) * 0.12;
    float3 alb = max(i.c.rgb, 0.06);
    float3 col = alb * (hemi + key * 0.62 + fill) + spec + rim;
    return float4(saturate(col), 1);
}
)";

struct V { float p[3], n[3], c[4]; };

struct M4 { float m[16]; };
M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
    return r;
}

} // namespace

void renderThumbnail(const MeshModel& model, uint32_t size, uint32_t background, uint8_t* bgra) {
    const uint32_t ss = 2, W = size * ss;
    // ---- build vertices in a Y-up world
    std::vector<V> verts;
    std::vector<uint32_t> tri, pts;
    auto toWorld = [&](const float* p, float* o) {
        if (model.upAxis == 2) { o[0] = p[0]; o[1] = p[2]; o[2] = -p[1]; }
        else { o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; }
    };
    for (const auto& s : model.submeshes) {
        const uint32_t base = (uint32_t)verts.size();
        const size_t n = s.positions.size() / 3;
        for (size_t i = 0; i < n; ++i) {
            V v{};
            toWorld(&s.positions[i * 3], v.p);
            toWorld(&s.normals[i * 3], v.n);
            for (int k = 0; k < 3; ++k) v.c[k] = s.colors.empty() ? s.baseColor[k] : s.colors[i * 4 + k] * (s.texture.empty() ? 1.f : 1.f);
            if (!s.texture.empty() && s.colors.empty()) {
                // No texture sampling in thumbnails: use a neutral tint instead of pure white.
                if (v.c[0] > 0.97f && v.c[1] > 0.97f && v.c[2] > 0.97f) { v.c[0] = 0.78f; v.c[1] = 0.76f; v.c[2] = 0.72f; }
            }
            v.c[3] = 1;
            verts.push_back(v);
        }
        auto& dst = s.isPoints ? pts : tri;
        for (uint32_t idx : s.indices) dst.push_back(base + idx);
    }
    if (verts.empty()) throw std::runtime_error("Nothing to render.");

    // ---- isometric camera fitted to the projected bounds
    const float eye[3] = {0.62f, 0.50f, 0.70f};
    float fz[3], len = std::sqrt(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    for (int k = 0; k < 3; ++k) fz[k] = eye[k] / len;               // towards viewer
    float up[3] = {0, 1, 0};
    float fx[3] = {up[1] * fz[2] - up[2] * fz[1], up[2] * fz[0] - up[0] * fz[2], up[0] * fz[1] - up[1] * fz[0]};
    float lx = std::sqrt(fx[0] * fx[0] + fx[1] * fx[1] + fx[2] * fx[2]);
    for (auto& v : fx) v /= lx;
    float fy[3] = {fz[1] * fx[2] - fz[2] * fx[1], fz[2] * fx[0] - fz[0] * fx[2], fz[0] * fx[1] - fz[1] * fx[0]};
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& v : verts) {
        float q[3] = {v.p[0] * fx[0] + v.p[1] * fx[1] + v.p[2] * fx[2],
                      v.p[0] * fy[0] + v.p[1] * fy[1] + v.p[2] * fy[2],
                      v.p[0] * fz[0] + v.p[1] * fz[1] + v.p[2] * fz[2]};
        for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], q[k]); mx[k] = std::max(mx[k], q[k]); }
    }
    const float cx = (mn[0] + mx[0]) / 2, cy = (mn[1] + mx[1]) / 2;
    float half = std::max(mx[0] - mn[0], mx[1] - mn[1]) / 2 * 1.08f;
    if (!(half > 0)) half = 1;
    const float depth = std::max(mx[2] - mn[2], 1e-6f);
    // row-major matrix for mul(float4, M): rows are basis transposed
    M4 view{{fx[0], fy[0], fz[0], 0, fx[1], fy[1], fz[1], 0, fx[2], fy[2], fz[2], 0, -cx, -cy, -mx[2] - depth * 0.01f, 1}};
    // orthographic: x,y in [-half,half] -> [-1,1]; z from 0 (near) to -depth*1.02 -> 0..1
    const float d = depth * 1.04f;
    M4 proj{{1 / half, 0, 0, 0, 0, 1 / half, 0, 0, 0, 0, -1 / d, 0, 0, 0, 0, 1}};
    struct CB { M4 vp; float light[4]; float view[4]; } cb{};
    cb.vp = mul(view, proj);
    // HLSL default is column-major packing; transpose for mul(float4, matrix).
    M4 t{};
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) t.m[i * 4 + j] = cb.vp.m[j * 4 + i];
    cb.vp = t;
    const float L[3] = {0.35f, 0.85f, 0.45f};
    for (int k = 0; k < 3; ++k) { cb.light[k] = L[k]; cb.view[k] = fz[k]; }

    // ---- device
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    hr(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &dev, nullptr, &ctx), "Create WARP device");
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = W;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> rt, ds, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    hr(dev->CreateTexture2D(&td, nullptr, &rt), "Create target");
    hr(dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv), "Create RTV");
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr(dev->CreateTexture2D(&td, nullptr, &ds), "Create depth");
    hr(dev->CreateDepthStencilView(ds.Get(), nullptr, &dsv), "Create DSV");

    ComPtr<ID3DBlob> vsb, psb, err;
    hr(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "vs", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &err), "Compile VS");
    hr(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "ps", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &err), "Compile PS");
    ComPtr<ID3D11VertexShader> vsh;
    ComPtr<ID3D11PixelShader> psh;
    hr(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vsh), "VS");
    hr(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &psh), "PS");
    const D3D11_INPUT_ELEMENT_DESC el[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    ComPtr<ID3D11InputLayout> layout;
    hr(dev->CreateInputLayout(el, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout), "Layout");

    auto makeBuf = [&](const void* data, UINT bytes, UINT bind) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = bytes;
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = bind;
        D3D11_SUBRESOURCE_DATA sd{data, 0, 0};
        ComPtr<ID3D11Buffer> b;
        hr(dev->CreateBuffer(&bd, &sd, &b), "Buffer");
        return b;
    };
    auto vb = makeBuf(verts.data(), (UINT)(verts.size() * sizeof(V)), D3D11_BIND_VERTEX_BUFFER);
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(CB);
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cbs{&cb, 0, 0};
    ComPtr<ID3D11Buffer> cbuf;
    hr(dev->CreateBuffer(&cbd, &cbs, &cbuf), "CB");

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rs;
    hr(dev->CreateRasterizerState(&rd, &rs), "Raster");

    const float clear[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(rtv.Get(), clear);
    ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
    D3D11_VIEWPORT vp{0, 0, (float)W, (float)W, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(rs.Get());
    UINT stride = sizeof(V), off = 0;
    ctx->IASetInputLayout(layout.Get());
    ctx->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &off);
    ctx->VSSetShader(vsh.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, cbuf.GetAddressOf());
    ctx->PSSetShader(psh.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, cbuf.GetAddressOf());
    if (!tri.empty()) {
        auto ib = makeBuf(tri.data(), (UINT)(tri.size() * 4), D3D11_BIND_INDEX_BUFFER);
        ctx->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->DrawIndexed((UINT)tri.size(), 0, 0);
    }
    if (!pts.empty()) {
        auto ib = makeBuf(pts.data(), (UINT)(pts.size() * 4), D3D11_BIND_INDEX_BUFFER);
        ctx->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        ctx->DrawIndexed((UINT)pts.size(), 0, 0);
    }

    // ---- readback + 2x2 box downsample into premultiplied BGRA over the background
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr(dev->CreateTexture2D(&td, nullptr, &staging), "Staging");
    ctx->CopyResource(staging.Get(), rt.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    hr(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "Map");
    const float bgA = ((background >> 24) & 255) / 255.f;
    const float bg[3] = {(background & 255) / 255.f, ((background >> 8) & 255) / 255.f, ((background >> 16) & 255) / 255.f}; // B,G,R
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (uint32_t dy = 0; dy < ss; ++dy)
                for (uint32_t dx = 0; dx < ss; ++dx) {
                    const uint8_t* s = (const uint8_t*)map.pData + (y * ss + dy) * map.RowPitch + (x * ss + dx) * 4;
                    // rendered pixels are opaque where geometry is, transparent elsewhere
                    const float a = s[3] / 255.f;
                    for (int k = 0; k < 3; ++k) acc[k] += s[k] / 255.f * a;
                    acc[3] += a;
                }
            for (auto& v : acc) v /= ss * ss;
            uint8_t* o = bgra + (y * size + x) * 4;
            const float a = acc[3] + bgA * (1 - acc[3]);
            for (int k = 0; k < 3; ++k) o[k] = (uint8_t)std::lround(std::min(1.f, acc[k] + bg[k] * bgA * (1 - acc[3])) * 255);
            o[3] = (uint8_t)std::lround(a * 255);
        }
    ctx->Unmap(staging.Get(), 0);
}

// ---------------------------------------------------------------- 3MF package preview (from DMIND 3D Thumbnail)
bool read3mfPreview(const std::wstring& path, uint32_t size, uint8_t* bgra) {
    static const wchar_t* kThumb = L"http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail";
    ComPtr<IOpcFactory> factory;
    if (FAILED(CoCreateInstance(__uuidof(OpcFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
    ComPtr<IStream> file;
    if (FAILED(factory->CreateStreamOnFile(path.c_str(), OPC_STREAM_IO_READ, nullptr, 0, &file))) return false;
    ComPtr<IOpcPackage> package;
    if (FAILED(factory->ReadPackageFromStream(file.Get(), OPC_CACHE_ON_ACCESS, &package))) return false;
    ComPtr<IOpcRelationshipSet> rels;
    ComPtr<IOpcRelationshipEnumerator> en;
    BOOL found = FALSE;
    if (FAILED(package->GetRelationshipSet(&rels)) || FAILED(rels->GetEnumeratorForType(kThumb, &en)) ||
        FAILED(en->MoveNext(&found)) || !found) return false;
    ComPtr<IOpcRelationship> rel;
    ComPtr<IUri> target;
    ComPtr<IOpcUri> source;
    ComPtr<IOpcPartUri> name;
    OPC_URI_TARGET_MODE mode{};
    if (FAILED(en->GetCurrent(&rel)) || FAILED(rel->GetTargetMode(&mode)) || mode != OPC_URI_TARGET_MODE_INTERNAL ||
        FAILED(rel->GetTargetUri(&target)) || FAILED(rel->GetSourceUri(&source)) ||
        FAILED(source->CombinePartUri(target.Get(), &name))) return false;
    ComPtr<IOpcPartSet> parts;
    ComPtr<IOpcPart> part;
    ComPtr<IStream> content;
    if (FAILED(package->GetPartSet(&parts)) || FAILED(parts->GetPart(name.Get(), &part)) || FAILED(part->GetContentStream(&content))) return false;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateDecoderFromStream(content.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec)) || FAILED(dec->GetFrame(0, &frame))) return false;
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (!w || !h || w > 8192 || h > 8192) return false;
    const UINT longest = std::max(w, h);
    const UINT sw = std::max(1u, w * size / longest), sh = std::max(1u, h * size / longest);
    ComPtr<IWICFormatConverter> conv;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(wic->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(conv.Get(), sw, sh, WICBitmapInterpolationModeFant))) return false;
    std::vector<uint8_t> px((size_t)sw * sh * 4);
    if (FAILED(scaler->CopyPixels(nullptr, sw * 4, (UINT)px.size(), px.data()))) return false;
    memset(bgra, 0, (size_t)size * size * 4);
    for (UINT y = 0; y < sh; ++y)
        memcpy(bgra + ((y + (size - sh) / 2) * size + (size - sw) / 2) * 4, px.data() + (size_t)y * sw * 4, (size_t)sw * 4);
    return true;
}

} // namespace dv
