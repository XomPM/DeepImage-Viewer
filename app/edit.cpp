// DeepImage Viewer — image editing implementation
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "edit.h"
#include "gfx.h"
#include <d2d1effects_2.h>
#include <shlwapi.h>
#include <wincodecsdk.h>

namespace dv {

bool Effects::build(const Adjust& a) {
    head_ = tail_ = nullptr;
    if (a.identity()) return false;
    auto dc = G.dc.Get();
    auto make = [&](ComPtr<ID2D1Effect>& e, REFCLSID id) { if (!e) dc->CreateEffect(id, &e); return e.Get(); };
    std::vector<ID2D1Effect*> chain;
    if (a.exposure) {
        auto e = make(exposure_, CLSID_D2D1Exposure);
        e->SetValue(D2D1_EXPOSURE_PROP_EXPOSURE_VALUE, a.exposure * 2.f);
        chain.push_back(e);
    }
    if (a.contrast) {
        auto e = make(contrast_, CLSID_D2D1Contrast);
        e->SetValue(D2D1_CONTRAST_PROP_CONTRAST, a.contrast);
        e->SetValue(D2D1_CONTRAST_PROP_CLAMP_INPUT, TRUE);
        chain.push_back(e);
    }
    if (a.highlights || a.shadows) {
        auto e = make(hs_, CLSID_D2D1HighlightsShadows);
        e->SetValue(D2D1_HIGHLIGHTSANDSHADOWS_PROP_HIGHLIGHTS, a.highlights);
        e->SetValue(D2D1_HIGHLIGHTSANDSHADOWS_PROP_SHADOWS, a.shadows);
        e->SetValue(D2D1_HIGHLIGHTSANDSHADOWS_PROP_MASK_BLUR_RADIUS, 2.0f);
        chain.push_back(e);
    }
    if (a.temperature || a.tint) {
        auto e = make(temp_, CLSID_D2D1TemperatureTint);
        e->SetValue(D2D1_TEMPERATUREANDTINT_PROP_TEMPERATURE, a.temperature);
        e->SetValue(D2D1_TEMPERATUREANDTINT_PROP_TINT, a.tint);
        chain.push_back(e);
    }
    if (a.saturation) {
        // colour-matrix saturation so it can also boost (the built-in effect only reduces)
        auto e = make(sat_, CLSID_D2D1ColorMatrix);
        float s = 1 + a.saturation;
        const float lr = 0.2126f, lg = 0.7152f, lb = 0.0722f;
        D2D1_MATRIX_5X4_F m = D2D1::Matrix5x4F(
            lr * (1 - s) + s, lr * (1 - s), lr * (1 - s), 0,
            lg * (1 - s), lg * (1 - s) + s, lg * (1 - s), 0,
            lb * (1 - s), lb * (1 - s), lb * (1 - s) + s, 0,
            0, 0, 0, 1,
            0, 0, 0, 0);
        e->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, m);
        e->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);
        chain.push_back(e);
    }
    if (chain.empty()) return false;
    for (size_t i = 1; i < chain.size(); ++i) chain[i]->SetInputEffect(0, chain[i - 1]);
    head_ = chain.front();
    tail_ = chain.back();
    return true;
}

ComPtr<IWICBitmap> bakeOrientation(IWICBitmap* src, int rot, float fx, float fy) {
    ComPtr<IWICBitmapSource> cur = src;
    auto apply = [&](WICBitmapTransformOptions t) {
        ComPtr<IWICBitmapFlipRotator> r;
        if (SUCCEEDED(G.wic->CreateBitmapFlipRotator(&r)) && SUCCEEDED(r->Initialize(cur.Get(), t))) cur = r;
    };
    if (fx < 0) apply(WICBitmapTransformFlipHorizontal);
    if (fy < 0) apply(WICBitmapTransformFlipVertical);
    if (rot == 90) apply(WICBitmapTransformRotate90);
    else if (rot == 180) apply(WICBitmapTransformRotate180);
    else if (rot == 270) apply(WICBitmapTransformRotate270);
    ComPtr<IWICBitmap> out;
    G.wic->CreateBitmapFromSource(cur.Get(), WICBitmapCacheOnLoad, &out);
    return out;
}

ComPtr<IWICBitmap> cropBitmap(IWICBitmap* src, UINT x, UINT y, UINT w, UINT h) {
    ComPtr<IWICBitmapClipper> c;
    ComPtr<IWICBitmap> out;
    WICRect rc{(INT)x, (INT)y, (INT)w, (INT)h};
    if (FAILED(G.wic->CreateBitmapClipper(&c)) || FAILED(c->Initialize(src, &rc))) return nullptr;
    G.wic->CreateBitmapFromSource(c.Get(), WICBitmapCacheOnLoad, &out);
    return out;
}

ComPtr<IWICBitmap> resizeBitmap(IWICBitmap* src, UINT w, UINT h) {
    ComPtr<IWICBitmapScaler> s;
    ComPtr<IWICBitmap> out;
    if (FAILED(G.wic->CreateBitmapScaler(&s))) return nullptr;
    if (FAILED(s->Initialize(src, w, h, (WICBitmapInterpolationMode)4)) &&
        FAILED(s->Initialize(src, w, h, WICBitmapInterpolationModeFant))) return nullptr;
    G.wic->CreateBitmapFromSource(s.Get(), WICBitmapCacheOnLoad, &out);
    return out;
}

void transformShapes(std::vector<std::shared_ptr<Shape>>& shapes, const std::function<D2D1_POINT_2F(D2D1_POINT_2F)>& f, float ws) {
    std::vector<std::shared_ptr<Shape>> out;
    for (auto& s : shapes) {
        auto n = std::make_shared<Shape>(*s);
        for (auto& p : n->pts) p = f(p);
        n->a = f(n->a);
        n->b = f(n->b);
        n->width *= ws;
        out.push_back(n);
    }
    shapes.swap(out);
}

std::vector<EncoderInfo> availableEncoders() {
    std::vector<EncoderInfo> list;
    struct Known { GUID g; const wchar_t* name; const wchar_t* exts; };
    const Known known[] = {
        {GUID_ContainerFormatPng, L"PNG", L"*.png"},
        {GUID_ContainerFormatJpeg, L"JPEG", L"*.jpg;*.jpeg"},
        {GUID_ContainerFormatWebp, L"WebP", L"*.webp"},
        {GUID_ContainerFormatHeif, L"HEIF", L"*.heic;*.heif"},
        {GUID_ContainerFormatTiff, L"TIFF", L"*.tif;*.tiff"},
        {GUID_ContainerFormatBmp, L"BMP", L"*.bmp"},
        {GUID_ContainerFormatGif, L"GIF", L"*.gif"},
        {GUID_ContainerFormatWmp, L"JPEG XR", L"*.jxr;*.wdp"},
    };
    for (auto& k : known) {
        ComPtr<IWICBitmapEncoder> e;
        if (SUCCEEDED(G.wic->CreateEncoder(k.g, nullptr, &e))) list.push_back({k.g, k.name, k.exts});
    }
    return list;
}

bool saveBitmap(const std::wstring& path, const GUID& container, IWICBitmapSource* pixels, float quality,
                const std::wstring& metaSrc, bool keepMeta) {
    auto wic = G.wic.Get();
    std::wstring tmp = path + L".dvtmp";
    bool ok = false;
    {
        ComPtr<IWICStream> st;
        ComPtr<IWICBitmapEncoder> enc;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> bag;
        if (FAILED(wic->CreateStream(&st)) || FAILED(st->InitializeFromFilename(tmp.c_str(), GENERIC_WRITE)) ||
            FAILED(wic->CreateEncoder(container, nullptr, &enc)) || FAILED(enc->Initialize(st.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(enc->CreateNewFrame(&frame, &bag)))
            goto done;
        if (container == GUID_ContainerFormatJpeg || container == GUID_ContainerFormatHeif || container == GUID_ContainerFormatWmp ||
            container == GUID_ContainerFormatWebp) {
            PROPBAG2 opt{};
            opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_R4;
            v.fltVal = clampf(quality, 0.05f, 1.f);
            bag->Write(1, &opt, &v);
        }
        if (FAILED(frame->Initialize(bag.Get()))) goto done;
        UINT w, h;
        pixels->GetSize(&w, &h);
        frame->SetSize(w, h);
        frame->SetResolution(96, 96);
        // metadata (best effort, same container only)
        if (keepMeta && !metaSrc.empty()) {
            ComPtr<IWICBitmapDecoder> dec;
            ComPtr<IWICBitmapFrameDecode> f;
            GUID srcFmt{};
            if (SUCCEEDED(wic->CreateDecoderFromFilename(metaSrc.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
                SUCCEEDED(dec->GetContainerFormat(&srcFmt)) && srcFmt == container && SUCCEEDED(dec->GetFrame(0, &f))) {
                ComPtr<IWICMetadataBlockReader> br;
                ComPtr<IWICMetadataBlockWriter> bw;
                if (SUCCEEDED(f.As(&br)) && SUCCEEDED(frame.As(&bw)) && SUCCEEDED(bw->InitializeFromBlockReader(br.Get()))) {
                    ComPtr<IWICMetadataQueryWriter> qw;
                    if (SUCCEEDED(frame->GetMetadataQueryWriter(&qw))) {
                        PROPVARIANT pv;
                        PropVariantInit(&pv);
                        pv.vt = VT_UI2;
                        pv.uiVal = 1; // pixels are stored upright now
                        qw->SetMetadataByName(container == GUID_ContainerFormatJpeg ? L"/app1/ifd/{ushort=274}" : L"/ifd/{ushort=274}", &pv);
                    }
                }
            }
        }
        WICPixelFormatGUID want = GUID_WICPixelFormat32bppBGRA;
        if (container == GUID_ContainerFormatJpeg || container == GUID_ContainerFormatBmp) want = GUID_WICPixelFormat24bppBGR;
        if (container == GUID_ContainerFormatGif) want = GUID_WICPixelFormat8bppIndexed;
        WICPixelFormatGUID neg = want;
        frame->SetPixelFormat(&neg);
        ComPtr<IWICBitmapSource> src = pixels;
        ComPtr<IWICPalette> pal;
        if (neg == GUID_WICPixelFormat8bppIndexed) {
            wic->CreatePalette(&pal);
            pal->InitializeFromBitmap(pixels, 256, TRUE);
            frame->SetPalette(pal.Get());
        }
        ComPtr<IWICFormatConverter> conv;
        if (FAILED(wic->CreateFormatConverter(&conv)) ||
            FAILED(conv->Initialize(pixels, neg, WICBitmapDitherTypeErrorDiffusion, pal.Get(), 0,
                                    pal ? WICBitmapPaletteTypeCustom : WICBitmapPaletteTypeMedianCut)))
            goto done;
        src = conv;
        if (FAILED(frame->WriteSource(src.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(enc->Commit())) goto done;
        ok = true;
    }
done:
    if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp.c_str());
    return ok;
}

bool copyToClipboard(HWND hwnd, IWICBitmapSource* pixels) {
    UINT w, h;
    pixels->GetSize(&w, &h);
    std::vector<uint8_t> px((size_t)w * h * 4);
    if (FAILED(pixels->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) return false;
    // PNG for modern apps (keeps transparency)
    std::vector<uint8_t> png;
    {
        ComPtr<IStream> ms;
        ms.Attach(SHCreateMemStream(nullptr, 0));
        ComPtr<IWICBitmapEncoder> enc;
        ComPtr<IWICBitmapFrameEncode> fr;
        if (ms && SUCCEEDED(G.wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(ms.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr))) {
            fr->SetSize(w, h);
            WICPixelFormatGUID f = GUID_WICPixelFormat32bppBGRA;
            fr->SetPixelFormat(&f);
            ComPtr<IWICFormatConverter> c;
            G.wic->CreateFormatConverter(&c);
            c->Initialize(pixels, f, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
            if (SUCCEEDED(fr->WriteSource(c.Get(), nullptr)) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit())) {
                STATSTG s{};
                ms->Stat(&s, STATFLAG_NONAME);
                png.resize((size_t)s.cbSize.QuadPart);
                LARGE_INTEGER z{};
                ms->Seek(z, STREAM_SEEK_SET, nullptr);
                ULONG rd = 0;
                ms->Read(png.data(), (ULONG)png.size(), &rd);
            }
        }
    }
    // 24-bit DIB composited on white for legacy apps
    const UINT stride = (w * 3 + 3) & ~3u;
    HGLOBAL hd = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + (size_t)stride * h);
    if (!hd) return false;
    auto* bi = (BITMAPINFOHEADER*)GlobalLock(hd);
    *bi = BITMAPINFOHEADER{sizeof(BITMAPINFOHEADER), (LONG)w, (LONG)h, 1, 24, BI_RGB};
    uint8_t* dst = (uint8_t*)(bi + 1);
    for (UINT y = 0; y < h; ++y) {
        const uint8_t* s = &px[(size_t)(h - 1 - y) * w * 4];
        uint8_t* d = dst + (size_t)y * stride;
        for (UINT x = 0; x < w; ++x) {
            unsigned a = s[x * 4 + 3];
            for (int k = 0; k < 3; ++k) d[x * 3 + k] = (uint8_t)std::min(255u, s[x * 4 + k] + (255 - a));
        }
    }
    GlobalUnlock(hd);
    if (!OpenClipboard(hwnd)) { GlobalFree(hd); return false; }
    EmptyClipboard();
    SetClipboardData(CF_DIB, hd);
    if (!png.empty()) {
        HGLOBAL hp = GlobalAlloc(GMEM_MOVEABLE, png.size());
        if (hp) {
            memcpy(GlobalLock(hp), png.data(), png.size());
            GlobalUnlock(hp);
            SetClipboardData(RegisterClipboardFormatW(L"PNG"), hp);
        }
    }
    CloseClipboard();
    return true;
}

} // namespace dv
