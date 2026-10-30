// DeepImage Viewer — image decoding and GPU upload
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "image.h"
#include "gfx.h"
#include <shlwapi.h>
#include <propvarutil.h>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_TGA
#define STBI_ONLY_PSD
#define STBI_ONLY_PNM
#define STBI_ONLY_HDR
#include "../third_party/stb_image.h"

namespace dv {

namespace {

struct ComInit {
    HRESULT h;
    ComInit() : h(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComInit() { if (SUCCEEDED(h)) CoUninitialize(); }
};

std::wstring containerName(const GUID& g) {
    if (g == GUID_ContainerFormatJpeg) return L"JPEG";
    if (g == GUID_ContainerFormatPng) return L"PNG";
    if (g == GUID_ContainerFormatGif) return L"GIF";
    if (g == GUID_ContainerFormatBmp) return L"BMP";
    if (g == GUID_ContainerFormatTiff) return L"TIFF";
    if (g == GUID_ContainerFormatIco) return L"ICO";
    if (g == GUID_ContainerFormatWmp) return L"JPEG XR";
    if (g == GUID_ContainerFormatDds) return L"DDS";
    if (g == GUID_ContainerFormatHeif) return L"HEIF";
    if (g == GUID_ContainerFormatWebp) return L"WebP";
    if (g == GUID_ContainerFormatAdng) return L"DNG";
    if (g == GUID_ContainerFormatRaw) return L"RAW";
    return L"";
}

int readOrientation(IWICBitmapFrameDecode* f) {
    ComPtr<IWICMetadataQueryReader> q;
    if (FAILED(f->GetMetadataQueryReader(&q))) return 1;
    const wchar_t* paths[] = {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}", L"System.Photo.Orientation"};
    for (auto p : paths) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(q->GetMetadataByName(p, &v))) {
            int o = 1;
            if (v.vt == VT_UI2) o = v.uiVal;
            else if (v.vt == VT_UI4) o = (int)v.ulVal;
            else if (v.vt == VT_I4) o = v.lVal;
            PropVariantClear(&v);
            if (o >= 1 && o <= 8) return o;
        }
    }
    return 1;
}

ComPtr<IWICBitmapSource> orient(IWICImagingFactory* wic, IWICBitmapSource* src, int o) {
    ComPtr<IWICBitmapSource> cur = src;
    auto apply = [&](WICBitmapTransformOptions t) {
        ComPtr<IWICBitmapFlipRotator> r;
        if (SUCCEEDED(wic->CreateBitmapFlipRotator(&r)) && SUCCEEDED(r->Initialize(cur.Get(), t))) cur = r;
    };
    switch (o) {
    case 2: apply(WICBitmapTransformFlipHorizontal); break;
    case 3: apply(WICBitmapTransformRotate180); break;
    case 4: apply(WICBitmapTransformFlipVertical); break;
    case 5: apply(WICBitmapTransformRotate90); apply(WICBitmapTransformFlipHorizontal); break;
    case 6: apply(WICBitmapTransformRotate90); break;
    case 7: apply(WICBitmapTransformRotate270); apply(WICBitmapTransformFlipHorizontal); break;
    case 8: apply(WICBitmapTransformRotate270); break;
    }
    return cur;
}

ComPtr<IWICBitmap> toPbgra(IWICImagingFactory* wic, IWICBitmapSource* src) {
    ComPtr<IWICFormatConverter> conv;
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)) ||
        FAILED(wic->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnLoad, &bmp)))
        return nullptr;
    return bmp;
}

UINT getUShort(IWICMetadataQueryReader* q, const wchar_t* name, UINT def) {
    PROPVARIANT v;
    PropVariantInit(&v);
    UINT r = def;
    if (SUCCEEDED(q->GetMetadataByName(name, &v))) {
        if (v.vt == VT_UI2) r = v.uiVal;
        else if (v.vt == VT_UI1) r = v.bVal;
        else if (v.vt == VT_UI4) r = v.ulVal;
        else if (v.vt == VT_BOOL) r = v.boolVal ? 1 : 0;
    }
    PropVariantClear(&v);
    return r;
}

// Full GIF frame composition (disposal + offsets) so animations play correctly.
bool decodeGif(IWICImagingFactory* wic, IWICBitmapDecoder* dec, DecodedImage& out) {
    UINT n = 0;
    dec->GetFrameCount(&n);
    if (n <= 1) return false;
    ComPtr<IWICMetadataQueryReader> gq;
    UINT W = 0, H = 0;
    if (SUCCEEDED(dec->GetMetadataQueryReader(&gq))) {
        W = getUShort(gq.Get(), L"/logscrdesc/Width", 0);
        H = getUShort(gq.Get(), L"/logscrdesc/Height", 0);
    }
    ComPtr<IWICBitmapFrameDecode> f0;
    if (FAILED(dec->GetFrame(0, &f0))) return false;
    if (!W || !H) f0->GetSize(&W, &H);
    if (!W || !H || (uint64_t)W * H * 4 * n > (1ull << 30)) n = 1; // memory guard: first frame only
    std::vector<uint8_t> canvas((size_t)W * H * 4, 0), saved;
    for (UINT i = 0; i < n && i < 1000; ++i) {
        ComPtr<IWICBitmapFrameDecode> f;
        if (FAILED(dec->GetFrame(i, &f))) break;
        ComPtr<IWICMetadataQueryReader> q;
        UINT left = 0, top = 0, fw = 0, fh = 0, delay = 10, disposal = 0;
        f->GetSize(&fw, &fh);
        if (SUCCEEDED(f->GetMetadataQueryReader(&q))) {
            left = getUShort(q.Get(), L"/imgdesc/Left", 0);
            top = getUShort(q.Get(), L"/imgdesc/Top", 0);
            delay = getUShort(q.Get(), L"/grctlext/Delay", 10);
            disposal = getUShort(q.Get(), L"/grctlext/Disposal", 0);
        }
        if (disposal == 3) saved = canvas;
        auto bmp = toPbgra(wic, f.Get());
        if (!bmp) break;
        std::vector<uint8_t> px((size_t)fw * fh * 4);
        bmp->CopyPixels(nullptr, fw * 4, (UINT)px.size(), px.data());
        for (UINT y = 0; y < fh && top + y < H; ++y)
            for (UINT x = 0; x < fw && left + x < W; ++x) {
                const uint8_t* s = &px[((size_t)y * fw + x) * 4];
                if (s[3] == 0) continue;
                uint8_t* d = &canvas[((size_t)(top + y) * W + left + x) * 4];
                memcpy(d, s, 4);
            }
        ComPtr<IWICBitmap> frame;
        if (FAILED(wic->CreateBitmapFromMemory(W, H, GUID_WICPixelFormat32bppPBGRA, W * 4, (UINT)canvas.size(), canvas.data(), &frame))) break;
        out.frames.push_back(frame);
        out.delays.push_back(std::max<UINT>(20, delay * 10));
        if (disposal == 2) {
            for (UINT y = 0; y < fh && top + y < H; ++y)
                memset(&canvas[((size_t)(top + y) * W + left) * 4], 0, (size_t)std::min(fw, W - left) * 4);
        } else if (disposal == 3 && !saved.empty()) {
            canvas = saved;
        }
    }
    out.width = W;
    out.height = H;
    return !out.frames.empty();
}

bool decodeSvg(const std::wstring& path, DecodedImage& out, UINT maxEdge) {
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes, 64ull << 20)) return false;
    ComPtr<ID2D1Factory6> f;
    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory6), &fo, (void**)f.GetAddressOf()))) return false;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    // A 1x1 software target is enough to parse the document and read its intrinsic size.
    auto makeTarget = [&](UINT w, UINT h, ComPtr<IWICBitmap>& bmp, ComPtr<ID2D1DeviceContext5>& dc) -> bool {
        if (FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return false;
        ComPtr<ID2D1RenderTarget> rt;
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                  D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (FAILED(f->CreateWicBitmapRenderTarget(bmp.Get(), &props, &rt))) return false;
        return SUCCEEDED(rt.As(&dc));
    };
    ComPtr<IWICBitmap> probeBmp;
    ComPtr<ID2D1DeviceContext5> probe;
    if (!makeTarget(1, 1, probeBmp, probe)) return false;
    ComPtr<IStream> st;
    st.Attach(SHCreateMemStream(bytes.data(), (UINT)bytes.size()));
    ComPtr<ID2D1SvgDocument> doc;
    if (!st || FAILED(probe->CreateSvgDocument(st.Get(), D2D1::SizeF(1024, 1024), &doc))) return false;
    ComPtr<ID2D1SvgElement> root;
    doc->GetRoot(&root);
    float w = 0, h = 0;
    D2D1_SVG_LENGTH lw{}, lh{};
    if (SUCCEEDED(root->GetAttributeValue(L"width", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &lw, sizeof lw)) && lw.units == D2D1_SVG_LENGTH_UNITS_NUMBER) w = lw.value;
    if (SUCCEEDED(root->GetAttributeValue(L"height", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &lh, sizeof lh)) && lh.units == D2D1_SVG_LENGTH_UNITS_NUMBER) h = lh.value;
    D2D1_SVG_VIEWBOX vb{};
    bool hasVb = SUCCEEDED(root->GetAttributeValue(L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &vb, sizeof vb)) && vb.width > 0 && vb.height > 0;
    if ((w <= 0 || h <= 0) && hasVb) {
        if (w > 0) h = w * vb.height / vb.width;
        else if (h > 0) w = h * vb.width / vb.height;
        else { w = vb.width; h = vb.height; }
    }
    if (w <= 0 || h <= 0) { w = 1024; h = 1024; }
    float k = std::max(1.f, 2048.f / std::max(w, h));
    k = std::min(k, (float)maxEdge / std::max(w, h));
    UINT pw = std::max(1u, (UINT)std::lround(w * k)), ph = std::max(1u, (UINT)std::lround(h * k));
    ComPtr<IWICBitmap> bmp;
    ComPtr<ID2D1DeviceContext5> dc;
    if (!makeTarget(pw, ph, bmp, dc)) return false;
    // Re-parse against the real target, with width/height forced to the raster size.
    st.Reset();
    st.Attach(SHCreateMemStream(bytes.data(), (UINT)bytes.size()));
    doc.Reset();
    if (FAILED(dc->CreateSvgDocument(st.Get(), D2D1::SizeF((float)pw, (float)ph), &doc))) return false;
    doc->GetRoot(&root);
    D2D1_SVG_LENGTH full{100.f, D2D1_SVG_LENGTH_UNITS_PERCENTAGE};
    root->SetAttributeValue(L"width", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &full, sizeof full);
    root->SetAttributeValue(L"height", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &full, sizeof full);
    if (!hasVb) {
        D2D1_SVG_VIEWBOX v{0, 0, w, h};
        root->SetAttributeValue(L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &v, sizeof v);
    }
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0));
    dc->DrawSvgDocument(doc.Get());
    if (FAILED(dc->EndDraw())) return false;
    out.frames.push_back(bmp);
    out.delays.push_back(0);
    out.width = pw;
    out.height = ph;
    out.format = L"SVG";
    out.fromSvg = true;
    return true;
}

bool decodeStb(IWICImagingFactory* wic, const std::wstring& path, DecodedImage& out) {
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes, 1ull << 30)) return false;
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
    if (!px) return false;
    std::vector<uint8_t> bgra((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        uint8_t r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2], a = px[i * 4 + 3];
        bgra[i * 4 + 0] = (uint8_t)(b * a / 255);
        bgra[i * 4 + 1] = (uint8_t)(g * a / 255);
        bgra[i * 4 + 2] = (uint8_t)(r * a / 255);
        bgra[i * 4 + 3] = a;
    }
    stbi_image_free(px);
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA, w * 4, (UINT)bgra.size(), bgra.data(), &bmp))) return false;
    out.frames.push_back(bmp);
    out.delays.push_back(0);
    out.width = w;
    out.height = h;
    out.bitDepth = comp * 8;
    std::wstring e = extOf(path);
    out.format = e == L"tga" ? L"TGA" : e == L"psd" ? L"PSD" : e == L"hdr" ? L"HDR" : L"PNM";
    return true;
}

} // namespace

bool decodeImageFile(const std::wstring& path, DecodedImage& out, std::string& err, UINT maxSvgEdge) {
    ComInit com;
    out = DecodedImage{};
    err = "load.failed";
    const std::wstring ext = extOf(path);
    if (ext == L"svg") return decodeSvg(path, out, maxSvgEdge);
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    if (ext == L"tga" || ext == L"psd" || ext == L"ppm" || ext == L"pgm" || ext == L"pnm" || ext == L"hdr")
        return decodeStb(wic.Get(), path, out);
    ComPtr<IWICBitmapDecoder> dec;
    HRESULT h = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(h)) {
        if (h == WINCODEC_ERR_COMPONENTNOTFOUND) err = "load.codec";
        return false;
    }
    dec->GetContainerFormat(&out.container);
    out.format = containerName(out.container);
    if (out.format.empty()) out.format = lower(ext) == L"cr3" ? L"RAW" : [&] { auto u = ext; for (auto& c : u) c = (wchar_t)towupper(c); return u; }();
    if (out.container == GUID_ContainerFormatGif && decodeGif(wic.Get(), dec.Get(), out)) {
        out.bitDepth = 8;
        return true;
    }
    ComPtr<IWICBitmapFrameDecode> f;
    if (FAILED(dec->GetFrame(0, &f))) return false;
    // pick the largest frame of icons / multi-page TIFF
    UINT nf = 0;
    dec->GetFrameCount(&nf);
    if (nf > 1 && (out.container == GUID_ContainerFormatIco || out.container == GUID_ContainerFormatTiff)) {
        UINT best = 0, bestArea = 0;
        for (UINT i = 0; i < nf && i < 64; ++i) {
            ComPtr<IWICBitmapFrameDecode> fi;
            UINT w = 0, hh = 0;
            if (SUCCEEDED(dec->GetFrame(i, &fi)) && SUCCEEDED(fi->GetSize(&w, &hh)) && w * hh > bestArea) { best = i; bestArea = w * hh; }
        }
        f.Reset();
        dec->GetFrame(best, &f);
    }
    WICPixelFormatGUID pf{};
    f->GetPixelFormat(&pf);
    ComPtr<IWICComponentInfo> ci;
    ComPtr<IWICPixelFormatInfo> pfi;
    if (SUCCEEDED(wic->CreateComponentInfo(pf, &ci)) && SUCCEEDED(ci.As(&pfi))) pfi->GetBitsPerPixel(&out.bitDepth);
    f->GetResolution(&out.dpiX, &out.dpiY);
    out.orientation = readOrientation(f.Get());
    auto oriented = orient(wic.Get(), f.Get(), out.orientation);
    auto bmp = toPbgra(wic.Get(), oriented.Get());
    if (!bmp) return false;
    bmp->GetSize(&out.width, &out.height);
    out.frames.push_back(bmp);
    out.delays.push_back(0);
    return true;
}

// ======================================================================== ImageDoc
void ImageDoc::clear() {
    path.clear();
    img = DecodedImage{};
    gpu.clear();
    frame = 0;
}

static std::vector<Tile> uploadFrame(ID2D1DeviceContext* dc, IWICBitmap* src, UINT W, UINT H) {
    std::vector<Tile> tiles;
    const UINT maxSize = std::min<UINT>(dc->GetMaximumBitmapSize(), 8192);
    auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (W <= maxSize && H <= maxSize) {
        ComPtr<ID2D1Bitmap1> b;
        if (SUCCEEDED(dc->CreateBitmapFromWicBitmap(src, &props, &b))) tiles.push_back({R(0, 0, (float)W, (float)H), b});
        return tiles;
    }
    for (UINT y = 0; y < H; y += maxSize)
        for (UINT x = 0; x < W; x += maxSize) {
            UINT w = std::min(maxSize, W - x), h = std::min(maxSize, H - y);
            WICRect rc{(INT)x, (INT)y, (INT)w, (INT)h};
            ComPtr<IWICBitmapLock> lock;
            if (FAILED(src->Lock(&rc, WICBitmapLockRead, &lock))) continue;
            UINT stride = 0, size = 0;
            BYTE* data = nullptr;
            lock->GetStride(&stride);
            lock->GetDataPointer(&size, &data);
            ComPtr<ID2D1Bitmap1> b;
            if (SUCCEEDED(dc->CreateBitmap(D2D1::SizeU(w, h), data, stride, &props, &b)))
                tiles.push_back({R((float)x, (float)y, (float)(x + w), (float)(y + h)), b});
        }
    return tiles;
}

void ImageDoc::upload(ID2D1DeviceContext* dc) {
    gpu.clear();
    for (auto& f : img.frames) gpu.push_back(uploadFrame(dc, f.Get(), img.width, img.height));
    frame = 0;
    nextFrameAt = GetTickCount() + (img.delays.empty() ? 0 : img.delays[0]);
}

void ImageDoc::replace(ComPtr<IWICBitmap> bmp) {
    img.frames.assign(1, bmp);
    img.delays.assign(1, 0);
    bmp->GetSize(&img.width, &img.height);
    upload(G.dc.Get());
}

bool ImageDoc::tick() {
    if (img.frames.size() < 2) return false;
    DWORD now = GetTickCount();
    if ((int)(now - nextFrameAt) < 0) return false;
    frame = (frame + 1) % (int)img.frames.size();
    nextFrameAt = now + img.delays[frame];
    return true;
}

void ImageDoc::draw(ID2D1DeviceContext* dc, float scale, ID2D1Effect* head, ID2D1Effect* tail, float opacity) const {
    if (gpu.empty()) return;
    const auto& tiles = gpu[std::min<size_t>(frame, gpu.size() - 1)];
    D2D1_INTERPOLATION_MODE mode = scale >= 3.f ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                   : scale < 0.98f ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
                                                   : D2D1_INTERPOLATION_MODE_LINEAR;
    for (auto& t : tiles) {
        if (head && tail) {
            head->SetInput(0, t.bmp.Get());
            ComPtr<ID2D1Image> out;
            tail->GetOutput(&out);
            D2D1_MATRIX_3X2_F old;
            dc->GetTransform(&old);
            dc->SetTransform(D2D1::Matrix3x2F::Translation(t.rect.left, t.rect.top) * old);
            D2D1_RECT_F src = R(0, 0, rw(t.rect), rh(t.rect));
            dc->DrawImage(out.Get(), nullptr, &src, mode == D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC ? D2D1_INTERPOLATION_MODE_LINEAR : mode);
            dc->SetTransform(old);
        } else {
            dc->DrawBitmap(t.bmp.Get(), t.rect, opacity, mode, nullptr, nullptr);
        }
    }
}

ComPtr<IWICBitmap> renderToWic(UINT w, UINT h, const std::function<void(ID2D1DeviceContext*)>& draw, bool opaqueWhite) {
    auto dc = G.dc.Get();
    ComPtr<IWICBitmap> out;
    if (FAILED(G.wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &out))) return nullptr;
    const UINT T = std::min<UINT>(dc->GetMaximumBitmapSize(), 4096);
    ComPtr<ID2D1Image> oldTarget;
    dc->GetTarget(&oldTarget);
    float odx, ody;
    dc->GetDpi(&odx, &ody);
    dc->SetDpi(96, 96);
    auto propsT = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    auto propsR = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                          D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    bool ok = true;
    for (UINT y = 0; y < h && ok; y += T)
        for (UINT x = 0; x < w && ok; x += T) {
            UINT tw = std::min(T, w - x), th = std::min(T, h - y);
            ComPtr<ID2D1Bitmap1> tgt, rd;
            if (FAILED(dc->CreateBitmap(D2D1::SizeU(tw, th), nullptr, 0, &propsT, &tgt)) ||
                FAILED(dc->CreateBitmap(D2D1::SizeU(tw, th), nullptr, 0, &propsR, &rd))) { ok = false; break; }
            dc->SetTarget(tgt.Get());
            dc->BeginDraw();
            dc->Clear(opaqueWhite ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0, 0));
            dc->SetTransform(D2D1::Matrix3x2F::Translation(-(float)x, -(float)y));
            draw(dc);
            dc->SetTransform(D2D1::Matrix3x2F::Identity());
            if (FAILED(dc->EndDraw())) { ok = false; break; }
            D2D1_POINT_2U p{0, 0};
            D2D1_RECT_U src{0, 0, tw, th};
            rd->CopyFromBitmap(&p, tgt.Get(), &src);
            D2D1_MAPPED_RECT m{};
            if (FAILED(rd->Map(D2D1_MAP_OPTIONS_READ, &m))) { ok = false; break; }
            WICRect rc{(INT)x, (INT)y, (INT)tw, (INT)th};
            ComPtr<IWICBitmapLock> lock;
            if (SUCCEEDED(out->Lock(&rc, WICBitmapLockWrite, &lock))) {
                UINT stride = 0, size = 0;
                BYTE* data = nullptr;
                lock->GetStride(&stride);
                lock->GetDataPointer(&size, &data);
                for (UINT r = 0; r < th; ++r) memcpy(data + (size_t)r * stride, m.bits + (size_t)r * m.pitch, (size_t)tw * 4);
            }
            rd->Unmap();
        }
    dc->SetTarget(oldTarget.Get());
    dc->SetDpi(odx, ody);
    return ok ? out : nullptr;
}

} // namespace dv
