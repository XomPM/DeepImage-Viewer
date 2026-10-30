// DeepImage Viewer — graphics implementation
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "gfx.h"
#include <shlwapi.h>
#include <cstring>

namespace dv {

#include "icons.inc"

Gfx G;

bool Gfx::createDevice() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 4, D3D11_SDK_VERSION, &d3d, nullptr, &ctx);
    if (FAILED(h)) h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, 3, D3D11_SDK_VERSION, &d3d, nullptr, &ctx);
    if (FAILED(h)) {
        warp = true;
        h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels + 1, 3, D3D11_SDK_VERSION, &d3d, nullptr, &ctx);
    }
    if (FAILED(h)) return false;
    ComPtr<IDXGIDevice1> dxgi;
    d3d.As(&dxgi);
    dxgi->SetMaximumFrameLatency(1);
    ComPtr<ID2D1Device> dev;
    if (FAILED(d2f->CreateDevice(dxgi.Get(), &dev)) || FAILED(dev.As(&d2dev))) return false;
    ComPtr<ID2D1DeviceContext5> c;
    if (FAILED(d2dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, c.GetAddressOf()))) return false;
    dc = c;
    dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    ComPtr<IDXGIAdapter> adapter;
    dxgi->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> fac;
    adapter->GetParent(IID_PPV_ARGS(&fac));
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    h = fac->CreateSwapChainForHwnd(d3d.Get(), hwnd, &sd, nullptr, nullptr, &swap);
    if (FAILED(h)) { // older systems: sequential flip with stretch
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.Scaling = DXGI_SCALING_STRETCH;
        h = fac->CreateSwapChainForHwnd(d3d.Get(), hwnd, &sd, nullptr, nullptr, &swap);
    }
    if (FAILED(h)) return false;
    fac->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    brushes_.clear();
    icons_.clear();
    createTarget();
    return true;
}

void Gfx::createTarget() {
    dc->SetTarget(nullptr);
    target.Reset();
    ComPtr<IDXGISurface> surf;
    if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&surf)))) return;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    dc->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &target);
    DXGI_SURFACE_DESC d{};
    surf->GetDesc(&d);
    pxW = d.Width;
    pxH = d.Height;
}

void Gfx::loadFonts() {
    family = L"Segoe UI";
    ComPtr<IDWriteFontSetBuilder1> b;
    if (FAILED(dw->CreateFontSetBuilder(&b))) return;
    const wchar_t* files[] = {L"Pretendard-Regular.otf", L"Pretendard-Medium.otf", L"Pretendard-SemiBold.otf"};
    int added = 0;
    for (auto f : files) {
        std::wstring p = exeDir() + L"\\fonts\\" + f;
        ComPtr<IDWriteFontFile> ff;
        if (SUCCEEDED(dw->CreateFontFileReference(p.c_str(), nullptr, &ff)) && SUCCEEDED(b->AddFontFile(ff.Get()))) ++added;
    }
    if (!added) return;
    ComPtr<IDWriteFontSet> set;
    if (FAILED(b->CreateFontSet(&set))) return;
    if (SUCCEEDED(dw->CreateFontCollectionFromFontSet(set.Get(), &fonts))) family = L"Pretendard";
}

bool Gfx::init(HWND h) {
    hwnd = h;
    dpi = (float)GetDpiForWindow(hwnd);
    scale = dpi / 96.f;
    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory6), &fo, (void**)d2f.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5), (IUnknown**)dw.GetAddressOf()))) return false;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    loadFonts();
    return createDevice();
}

bool Gfx::resize() {
    if (!swap) return false;
    dpi = (float)GetDpiForWindow(hwnd);
    scale = dpi / 96.f;
    dc->SetTarget(nullptr);
    target.Reset();
    RECT rc;
    GetClientRect(hwnd, &rc);
    UINT w = std::max<LONG>(1, rc.right - rc.left), h = std::max<LONG>(1, rc.bottom - rc.top);
    HRESULT hr = swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) { handleDeviceLost(); return true; }
    createTarget();
    dc->SetDpi(dpi, dpi);
    return true;
}

void Gfx::handleDeviceLost() {
    dc.Reset(); target.Reset(); swap.Reset(); ctx.Reset(); d3d.Reset(); d2dev.Reset();
    brushes_.clear(); icons_.clear();
    createDevice();
}

bool Gfx::beginFrame() {
    if (!target) createTarget();
    if (!target) return false;
    dc->SetTarget(target.Get());
    dc->SetDpi(dpi, dpi);
    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

void Gfx::endFrame() {
    HRESULT h = dc->EndDraw();
    if (h == D2DERR_RECREATE_TARGET) { handleDeviceLost(); return; }
    DXGI_PRESENT_PARAMETERS pp{};
    h = swap->Present1(1, 0, &pp);
    if (h == DXGI_ERROR_DEVICE_REMOVED || h == DXGI_ERROR_DEVICE_RESET) handleDeviceLost();
}

ID2D1SolidColorBrush* Gfx::brush(const D2D1_COLOR_F& c) {
    uint32_t key = ((uint32_t)(clampf(c.a, 0, 1) * 255) << 24) | ((uint32_t)(clampf(c.r, 0, 1) * 255) << 16) |
                   ((uint32_t)(clampf(c.g, 0, 1) * 255) << 8) | (uint32_t)(clampf(c.b, 0, 1) * 255);
    auto it = brushes_.find(key);
    if (it != brushes_.end()) return it->second.Get();
    if (brushes_.size() > 512) brushes_.clear();
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(c, &b);
    brushes_[key] = b;
    return b.Get();
}

IDWriteTextFormat* Gfx::format(float size, int weight, Align a, bool center, bool wrap) {
    uint64_t key = ((uint64_t)(size * 10) << 20) | ((uint64_t)weight << 8) | (a << 2) | (center << 1) | (wrap ? 1 : 0);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> f;
    dw->CreateTextFormat(family.c_str(), fonts.Get(), (DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                         size, L"", &f);
    if (!f) dw->CreateTextFormat(L"Segoe UI", nullptr, (DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
    f->SetTextAlignment(a == AC ? DWRITE_TEXT_ALIGNMENT_CENTER : a == AR ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    f->SetParagraphAlignment(center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    formats_[key] = f;
    return f.Get();
}

ComPtr<IDWriteTextLayout> Gfx::layout(const std::wstring& s, float size, int weight, float maxW, float maxH, Align a, bool wrap) {
    ComPtr<IDWriteTextLayout> l;
    dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), format(size, weight, a, false, wrap), maxW, maxH, &l);
    return l;
}

float Gfx::textWidth(const std::wstring& s, float size, int weight) {
    auto l = layout(s, size, weight);
    DWRITE_TEXT_METRICS m{};
    if (l) l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void Gfx::text(const std::wstring& s, const D2D1_RECT_F& r, float size, const D2D1_COLOR_F& c, int weight, Align a, bool wrap, bool vcenter) {
    if (s.empty()) return;
    dc->DrawText(s.c_str(), (UINT32)s.size(), format(size, weight, a, vcenter, wrap), r, brush(c),
                 D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

void Gfx::textEllipsis(const std::wstring& s, const D2D1_RECT_F& r, float size, const D2D1_COLOR_F& c, int weight, Align a) {
    if (s.empty()) return;
    auto l = layout(s, size, weight, std::max(1.f, rw(r)), rh(r), a, false);
    if (!l) return;
    DWRITE_TRIMMING tr{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> ell;
    dw->CreateEllipsisTrimmingSign(l.Get(), &ell);
    l->SetTrimming(&tr, ell.Get());
    l->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dc->DrawTextLayout(D2D1::Point2F(r.left, r.top), l.Get(), brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Gfx::fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius) {
    if (radius > 0) dc->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush(c));
    else dc->FillRectangle(r, brush(c));
}
void Gfx::stroke(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius, float width) {
    D2D1_RECT_F q = inset(r, width / 2);
    if (radius > 0) dc->DrawRoundedRectangle(D2D1::RoundedRect(q, radius, radius), brush(c), width);
    else dc->DrawRectangle(q, brush(c), width);
}
void Gfx::line(float x0, float y0, float x1, float y1, const D2D1_COLOR_F& c, float w) {
    dc->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush(c), w);
}
void Gfx::circle(float cx, float cy, float r, const D2D1_COLOR_F& c, bool filled, float w) {
    auto e = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);
    if (filled) dc->FillEllipse(e, brush(c)); else dc->DrawEllipse(e, brush(c), w);
}
void Gfx::shadow(const D2D1_RECT_F& r, float radius, float strength) {
    // layered soft shadow (cheap approximation of a blurred drop shadow)
    const float a = (P.dark ? 0.16f : 0.045f) * strength;
    for (int i = 1; i <= 6; ++i) {
        float g = i * 2.2f;
        D2D1_RECT_F q = R(r.left - g * .6f, r.top - g * .2f + 4, r.right + g * .6f, r.bottom + g + 4);
        fill(q, rgba(0, a / (1 + i * .5f)), radius + g);
    }
}

void Gfx::icon(const char* name, float cx, float cy, float size, const D2D1_COLOR_F& c, float sw) {
    char hex[16];
    snprintf(hex, sizeof hex, "#%02X%02X%02X", (int)(clampf(c.r, 0, 1) * 255), (int)(clampf(c.g, 0, 1) * 255), (int)(clampf(c.b, 0, 1) * 255));
    char key[128];
    snprintf(key, sizeof key, "%s|%s|%.2f|%.1f|%.2f", name, hex, c.a, size, sw);
    auto it = icons_.find(key);
    ID2D1SvgDocument* doc = nullptr;
    if (it == icons_.end()) {
        const char* body = nullptr;
        for (auto& d : kIcons) if (!strcmp(d.name, name)) { body = d.body; break; }
        if (!body) return;
        std::string b(body);
        for (size_t p; (p = b.find("currentColor")) != std::string::npos;) b.replace(p, 12, hex);
        char head[400];
        snprintf(head, sizeof head,
                 "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%s\" "
                 "stroke-opacity=\"%.3f\" stroke-width=\"%.2f\" stroke-linecap=\"round\" stroke-linejoin=\"round\">",
                 hex, c.a, sw);
        std::string svg = std::string(head) + b + "</svg>";
        ComPtr<IStream> st;
        st.Attach(SHCreateMemStream((const BYTE*)svg.data(), (UINT)svg.size()));
        ComPtr<ID2D1SvgDocument> d;
        if (!st || FAILED(dc->CreateSvgDocument(st.Get(), D2D1::SizeF(size, size), &d))) return;
        if (icons_.size() > 600) icons_.clear();
        icons_[key] = d;
        doc = d.Get();
    } else {
        doc = it->second.Get();
    }
    D2D1_MATRIX_3X2_F old;
    dc->GetTransform(&old);
    dc->SetTransform(D2D1::Matrix3x2F::Translation(std::round((cx - size / 2) * scale) / scale, std::round((cy - size / 2) * scale) / scale) * old);
    dc->DrawSvgDocument(doc);
    dc->SetTransform(old);
}

} // namespace dv
