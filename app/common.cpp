// DeepImage Viewer — helpers
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "common.h"

#include <cstdarg>
#include <cwchar>
#include <fstream>
#include <filesystem>

namespace dv {

Palette P;

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}
std::wstring extOf(const std::wstring& path) {
    auto dot = path.find_last_of(L'.');
    auto slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return L"";
    return lower(path.substr(dot + 1));
}
std::wstring fileName(const std::wstring& path) {
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}
std::wstring dirName(const std::wstring& path) {
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}
std::wstring fmtf(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 511, fmt, ap);
    va_end(ap);
    buf[511] = 0;
    return buf;
}
std::wstring fmtBytes(uint64_t n) {
    const wchar_t* u[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double v = (double)n;
    int i = 0;
    while (v >= 1024 && i < 4) { v /= 1024; ++i; }
    if (i == 0) return fmtf(L"%llu B", (unsigned long long)n);
    return fmtf(v < 10 ? L"%.2f %s" : L"%.1f %s", v, u[i]);
}
std::wstring fmtNum(uint64_t n) {
    std::wstring s = std::to_wstring(n), out;
    int c = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        out.insert(out.begin(), s[i]);
        if (++c % 3 == 0 && i > 0) out.insert(out.begin(), L',');
    }
    return out;
}
std::wstring fmtLen(double v) {
    double a = std::fabs(v);
    if (!std::isfinite(v)) return L"—";
    if (a >= 1000) return fmtf(L"%.0f", v);
    if (a >= 100) return fmtf(L"%.1f", v);
    if (a >= 1) return fmtf(L"%.2f", v);
    return fmtf(L"%.3g", v);
}
std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
std::wstring exeDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return dirName(std::wstring(buf, n));
}
bool readFileBytes(const std::wstring& path, std::vector<uint8_t>& out, uint64_t limit) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && (uint64_t)sz.QuadPart <= limit;
    if (ok) {
        out.resize((size_t)sz.QuadPart);
        size_t got = 0;
        while (got < out.size()) {
            DWORD r = 0;
            DWORD want = (DWORD)std::min<size_t>(out.size() - got, 1u << 30);
            if (!ReadFile(h, out.data() + got, want, &r, nullptr) || r == 0) { ok = false; break; }
            got += r;
        }
    }
    CloseHandle(h);
    return ok;
}

const std::vector<std::wstring>& imageExts() {
    static const std::vector<std::wstring> v = {
        L"jpg", L"jpeg", L"jpe", L"jfif", L"png", L"apng", L"gif", L"webp", L"bmp", L"dib", L"ico", L"cur", L"tif", L"tiff",
        L"svg", L"avif", L"heic", L"heif", L"jxr", L"wdp", L"hdp", L"dds", L"tga", L"psd", L"ppm", L"pgm", L"pnm",
        L"dng", L"cr2", L"cr3", L"nef", L"arw", L"orf", L"rw2", L"raf", L"srw", L"pef"};
    return v;
}
const std::vector<std::wstring>& modelExts() {
    static const std::vector<std::wstring> v = {
        L"stl", L"3mf", L"obj", L"ply", L"gltf", L"glb", L"fbx", L"step", L"stp", L"iges", L"igs", L"brep", L"brp",
        L"dae", L"3ds", L"amf", L"off"};
    return v;
}
Kind kindOfExt(const std::wstring& e) {
    for (auto& x : imageExts()) if (x == e) return Kind::Image;
    for (auto& x : modelExts()) if (x == e) return Kind::Model;
    return Kind::None;
}

void buildPalette(bool dark, uint32_t accentRgb, bool useSystemAccent) {
    Palette p{};
    p.dark = dark;
    if (!dark) {
        p.bg = rgba(0xF4F4F6); p.canvas = rgba(0xE8E9ED); p.canvas2 = rgba(0xDFE0E5);
        p.surface = rgba(0xFFFFFF); p.surface2 = rgba(0xF6F6F8);
        p.hover = rgba(0x12141C, .06f); p.press = rgba(0x12141C, .10f);
        p.line = rgba(0x12141C, .09f); p.line2 = rgba(0x12141C, .14f);
        p.text = rgba(0x15171C); p.text2 = rgba(0x565B67); p.text3 = rgba(0x8D919C);
        p.glass = rgba(0xFFFFFF, .92f); p.glassLine = rgba(0x12141C, .08f);
        p.shadow = rgba(0x12141C, .10f);
        p.chkA = rgba(0xE1E2E6); p.chkB = rgba(0xEDEEF1);
    } else {
        p.bg = rgba(0x121316); p.canvas = rgba(0x0C0D0F); p.canvas2 = rgba(0x16171B);
        p.surface = rgba(0x1A1B1F); p.surface2 = rgba(0x202126);
        p.hover = rgba(0xFFFFFF, .06f); p.press = rgba(0xFFFFFF, .10f);
        p.line = rgba(0xFFFFFF, .07f); p.line2 = rgba(0xFFFFFF, .12f);
        p.text = rgba(0xECEDF0); p.text2 = rgba(0xA4A7B0); p.text3 = rgba(0x6B6F79);
        p.glass = rgba(0x1E1F24, .94f); p.glassLine = rgba(0xFFFFFF, .08f);
        p.shadow = rgba(0x000000, .35f);
        p.chkA = rgba(0x16171A); p.chkB = rgba(0x1D1E22);
    }
    uint32_t acc = useSystemAccent && accentRgb ? accentRgb : 0x5B6CFF;
    p.accent = rgba(acc);
    float lum = 0.2126f * p.accent.r + 0.7152f * p.accent.g + 0.0722f * p.accent.b;
    p.accentFg = lum > 0.62f ? rgba(0x111111) : rgba(0xFFFFFF);
    p.accentSoft = withA(p.accent, dark ? .20f : .14f);
    p.danger = rgba(0xE5484D);
    p.tooltip = rgba(0x1D1E23);
    p.tooltipText = rgba(0xF2F3F5);
    P = p;
}

} // namespace dv
