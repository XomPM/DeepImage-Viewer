// DeepImage Viewer — shared definitions
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A00000B
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1_3.h>
#include <d2d1_3helper.h>
#include <d3d11_1.h>
#include <dwrite_3.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace dv {

constexpr const wchar_t* kAppName = L"DeepImage Viewer";
constexpr const wchar_t* kVersion = L"1.0.2";
constexpr const wchar_t* kRegKey = L"Software\\DMIND\\DeepImageViewer";

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline D2D1_RECT_F R(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
inline D2D1_RECT_F RW(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
inline bool inside(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline float rw(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float rh(const D2D1_RECT_F& r) { return r.bottom - r.top; }
inline D2D1_RECT_F inset(const D2D1_RECT_F& r, float d) { return R(r.left + d, r.top + d, r.right - d, r.bottom - d); }

inline D2D1_COLOR_F rgba(uint32_t rgb, float a = 1.f) {
    return D2D1::ColorF(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a);
}
inline D2D1_COLOR_F withA(D2D1_COLOR_F c, float a) { c.a *= a; return c; }
inline D2D1_COLOR_F mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

std::wstring lower(std::wstring s);
std::wstring extOf(const std::wstring& path);
std::wstring fileName(const std::wstring& path);
std::wstring dirName(const std::wstring& path);
std::wstring fmtBytes(uint64_t n);
std::wstring fmtNum(uint64_t n);
std::wstring fmtLen(double v);
std::wstring fmtf(const wchar_t* fmt, ...);
std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);
std::wstring exeDir();
bool readFileBytes(const std::wstring& path, std::vector<uint8_t>& out, uint64_t limit = 1ull << 31);

enum class Kind { None, Image, Model };
Kind kindOfExt(const std::wstring& ext);
// Microsoft Store (MSIX) install: returns the app user model ID ("PFN!App"), empty when running unpackaged
std::wstring packagedAumid();
const std::vector<std::wstring>& imageExts();
const std::vector<std::wstring>& modelExts();

// Palette tokens (switch with light/dark)
struct Palette {
    D2D1_COLOR_F bg, canvas, canvas2, surface, surface2, hover, press, line, line2, text, text2, text3, glass, glassLine,
        accent, accentFg, accentSoft, danger, shadow, chkA, chkB, tooltip, tooltipText;
    bool dark;
};
extern Palette P;
void buildPalette(bool dark, uint32_t accentRgb, bool useSystemAccent);

} // namespace dv
