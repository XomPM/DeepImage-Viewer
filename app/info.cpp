// DeepImage Viewer — EXIF + histogram
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "info.h"
#include "gfx.h"
#include "i18n.h"
#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl.h>

namespace dv {

std::wstring formatFileTime(const FILETIME& ft) {
    FILETIME lft;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &lft);
    FileTimeToSystemTime(&lft, &st);
    const wchar_t* loc = lang() == Lang::Ko ? L"ko-KR" : L"en-US";
    wchar_t d[96] = {}, t[64] = {};
    GetDateFormatEx(loc, DATE_LONGDATE, &st, nullptr, d, 96, nullptr);
    GetTimeFormatEx(loc, TIME_NOSECONDS, &st, nullptr, t, 64);
    return std::wstring(d) + L" " + t;
}

namespace {
bool getString(IPropertyStore* ps, const PROPERTYKEY& k, std::wstring& out) {
    PROPVARIANT v;
    PropVariantInit(&v);
    bool ok = false;
    if (SUCCEEDED(ps->GetValue(k, &v)) && v.vt != VT_EMPTY) {
        PWSTR s = nullptr;
        if (SUCCEEDED(PropVariantToStringAlloc(v, &s)) && s && *s) { out = s; ok = true; }
        CoTaskMemFree(s);
    }
    PropVariantClear(&v);
    while (!out.empty() && (out.back() == L' ' || out.back() == 0)) out.pop_back();
    return ok && !out.empty();
}
bool getDouble(IPropertyStore* ps, const PROPERTYKEY& k, double& out) {
    PROPVARIANT v;
    PropVariantInit(&v);
    bool ok = SUCCEEDED(ps->GetValue(k, &v)) && v.vt != VT_EMPTY && SUCCEEDED(PropVariantToDouble(v, &out));
    PropVariantClear(&v);
    return ok;
}
bool getUInt(IPropertyStore* ps, const PROPERTYKEY& k, ULONG& out) {
    PROPVARIANT v;
    PropVariantInit(&v);
    bool ok = SUCCEEDED(ps->GetValue(k, &v)) && v.vt != VT_EMPTY && SUCCEEDED(PropVariantToUInt32(v, &out));
    PropVariantClear(&v);
    return ok;
}
bool getDms(IPropertyStore* ps, const PROPERTYKEY& k, double& deg) {
    PROPVARIANT v;
    PropVariantInit(&v);
    bool ok = false;
    if (SUCCEEDED(ps->GetValue(k, &v)) && v.vt == (VT_VECTOR | VT_R8) && v.cadbl.cElems >= 3) {
        deg = v.cadbl.pElems[0] + v.cadbl.pElems[1] / 60 + v.cadbl.pElems[2] / 3600;
        ok = true;
    }
    PropVariantClear(&v);
    return ok;
}
} // namespace

ExifInfo readExif(const std::wstring& path) {
    ExifInfo e;
    e.loaded = true;
    ComPtr<IPropertyStore> ps;
    if (FAILED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_BESTEFFORT, IID_PPV_ARGS(&ps)))) return e;
    std::wstring s;
    double d;
    ULONG u;
    if (getString(ps.Get(), PKEY_Photo_CameraManufacturer, s)) e.rows.push_back({"info.make", s});
    if (getString(ps.Get(), PKEY_Photo_CameraModel, s)) e.rows.push_back({"info.cammodel", s});
    if (getString(ps.Get(), PKEY_Photo_LensModel, s)) e.rows.push_back({"info.lens", s});
    if (getDouble(ps.Get(), PKEY_Photo_FocalLength, d) && d > 0) {
        std::wstring f = fmtf(L"%.0f mm", d);
        ULONG f35;
        if (getUInt(ps.Get(), PKEY_Photo_FocalLengthInFilm, f35) && f35 && std::fabs(f35 - d) > 0.5) f += fmtf(L"  (35mm %lu mm)", f35);
        e.rows.push_back({"info.focal", f});
    }
    if (getDouble(ps.Get(), PKEY_Photo_FNumber, d) && d > 0) e.rows.push_back({"info.aperture", fmtf(L"f/%.1f", d)});
    if (getDouble(ps.Get(), PKEY_Photo_ExposureTime, d) && d > 0)
        e.rows.push_back({"info.shutter", d < 0.5 ? fmtf(L"1/%.0f s", 1 / d) : fmtf(L"%.1f s", d)});
    if (getUInt(ps.Get(), PKEY_Photo_ISOSpeed, u) && u) e.rows.push_back({"info.iso", fmtf(L"ISO %lu", u)});
    if (getDouble(ps.Get(), PKEY_Photo_ExposureBias, d) && std::fabs(d) > 0.01) e.rows.push_back({"info.exposure", fmtf(L"%+.1f EV", d)});
    if (getUInt(ps.Get(), PKEY_Photo_Flash, u)) e.rows.push_back({"info.flash", T((u & 1) ? "info.flash.on" : "info.flash.off")});
    if (getUInt(ps.Get(), PKEY_Photo_WhiteBalance, u)) e.rows.push_back({"info.wb", T(u == 0 ? "info.wb.auto" : "info.wb.manual")});
    {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_Photo_DateTaken, &v)) && v.vt == VT_FILETIME) e.rows.push_back({"info.taken", formatFileTime(v.filetime)});
        PropVariantClear(&v);
    }
    double lat, lon;
    if (getDms(ps.Get(), PKEY_GPS_Latitude, lat) && getDms(ps.Get(), PKEY_GPS_Longitude, lon)) {
        std::wstring lr, lo;
        getString(ps.Get(), PKEY_GPS_LatitudeRef, lr);
        getString(ps.Get(), PKEY_GPS_LongitudeRef, lo);
        if (!lr.empty() && (lr[0] == L'S' || lr[0] == L's')) lat = -lat;
        if (!lo.empty() && (lo[0] == L'W' || lo[0] == L'w')) lon = -lon;
        e.rows.push_back({"info.gps", fmtf(L"%.5f°%c, %.5f°%c", std::fabs(lat), lat >= 0 ? L'N' : L'S', std::fabs(lon), lon >= 0 ? L'E' : L'W')});
    }
    if (getString(ps.Get(), PKEY_SoftwareUsed, s)) e.rows.push_back({"info.software", s});
    return e;
}

Histogram computeHistogram(IWICBitmapSource* src) {
    Histogram h;
    if (!src) return h;
    UINT w = 0, ht = 0;
    src->GetSize(&w, &ht);
    if (!w || !ht) return h;
    float k = std::min(1.f, 512.f / std::max(w, ht));
    UINT sw = std::max(1u, (UINT)(w * k)), sh = std::max(1u, (UINT)(ht * k));
    ComPtr<IWICBitmapScaler> sc;
    if (FAILED(G.wic->CreateBitmapScaler(&sc)) || FAILED(sc->Initialize(src, sw, sh, WICBitmapInterpolationModeLinear))) return h;
    std::vector<uint8_t> px((size_t)sw * sh * 4);
    if (FAILED(sc->CopyPixels(nullptr, sw * 4, (UINT)px.size(), px.data()))) return h;
    uint32_t r[256]{}, g[256]{}, b[256]{}, l[256]{};
    for (size_t i = 0; i < (size_t)sw * sh; ++i) {
        const uint8_t* p = &px[i * 4];
        unsigned a = p[3];
        if (a < 8) continue;
        unsigned B = a == 255 ? p[0] : std::min(255u, p[0] * 255u / a), Gc = a == 255 ? p[1] : std::min(255u, p[1] * 255u / a),
                 Rc = a == 255 ? p[2] : std::min(255u, p[2] * 255u / a);
        r[Rc]++; g[Gc]++; b[B]++;
        l[(Rc * 54 + Gc * 183 + B * 19) >> 8]++;
    }
    // scale with a soft clip so a single spike (pure black/white) does not flatten the rest
    std::vector<uint32_t> all;
    for (int i = 1; i < 255; ++i) { all.push_back(r[i]); all.push_back(g[i]); all.push_back(b[i]); }
    std::sort(all.begin(), all.end());
    float ref = (float)std::max<uint32_t>(1, all[(size_t)(all.size() * 0.995)]);
    for (int i = 0; i < 256; ++i) {
        h.r[i] = std::min(1.f, r[i] / ref);
        h.g[i] = std::min(1.f, g[i] / ref);
        h.b[i] = std::min(1.f, b[i] / ref);
        h.l[i] = std::min(1.f, l[i] / ref);
    }
    h.valid = true;
    return h;
}

void drawHistogram(const Histogram& h, const D2D1_RECT_F& r) {
    G.fill(r, P.dark ? rgba(0x0E0F12) : rgba(0xF1F2F4), 10);
    G.stroke(r, P.line, 10, 1);
    if (!h.valid) return;
    D2D1_RECT_F in = R(r.left + 8, r.top + 8, r.right - 8, r.bottom - 8);
    auto area = [&](const float* v, D2D1_COLOR_F c) {
        ComPtr<ID2D1PathGeometry> g;
        G.d2f->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> s;
        g->Open(&s);
        s->BeginFigure(D2D1::Point2F(in.left, in.bottom), D2D1_FIGURE_BEGIN_FILLED);
        for (int i = 0; i < 256; ++i) s->AddLine(D2D1::Point2F(in.left + rw(in) * i / 255.f, in.bottom - rh(in) * v[i]));
        s->AddLine(D2D1::Point2F(in.right, in.bottom));
        s->EndFigure(D2D1_FIGURE_END_CLOSED);
        s->Close();
        G.dc->FillGeometry(g.Get(), G.brush(c));
    };
    area(h.l, P.dark ? rgba(0xFFFFFF, .14f) : rgba(0x000000, .10f));
    area(h.r, rgba(0xFF4D4D, .42f));
    area(h.g, rgba(0x3DDC6B, .38f));
    area(h.b, rgba(0x3D8BFF, .42f));
}

} // namespace dv
