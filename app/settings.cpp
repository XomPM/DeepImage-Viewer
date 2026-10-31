// DeepImage Viewer — settings (HKCU\Software\DMIND\DeepImageViewer)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "settings.h"
#include "common.h"

namespace dv {
Settings S;

namespace {
DWORD getDword(HKEY k, const wchar_t* n, DWORD def) {
    DWORD v = 0, sz = sizeof v, type = 0;
    if (RegQueryValueExW(k, n, nullptr, &type, (BYTE*)&v, &sz) == ERROR_SUCCESS && type == REG_DWORD) return v;
    return def;
}
void setDword(HKEY k, const wchar_t* n, DWORD v) { RegSetValueExW(k, n, 0, REG_DWORD, (const BYTE*)&v, sizeof v); }
}

void Settings::load() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    theme = (int)getDword(k, L"Theme", 0) % 3;
    language = (int)getDword(k, L"Language", 0) % 3;
    accent = (int)getDword(k, L"Accent", 0) % 2;
    wheel = (int)getDword(k, L"Wheel", 0) % 2;
    imageBg = (int)getDword(k, L"ImageBg", 0) % 2;
    filmstrip = getDword(k, L"Filmstrip", 1) != 0;
    infoPanel = getDword(k, L"InfoPanel", 0) != 0;
    grid = getDword(k, L"Grid", 1) != 0;
    fitMode = (int)getDword(k, L"FitMode", 0) % 5;
    thumbColor = getDword(k, L"ThumbColor", 0);
    thumbColorAll = getDword(k, L"ThumbColorAll", 0) != 0;
    DWORD sz = sizeof placement, type = 0;
    if (RegQueryValueExW(k, L"Placement", nullptr, &type, (BYTE*)&placement, &sz) == ERROR_SUCCESS && type == REG_BINARY &&
        sz == sizeof placement && placement.length == sizeof placement)
        hasPlacement = true;
    wchar_t buf[2048];
    sz = sizeof buf;
    if (RegQueryValueExW(k, L"LastDir", nullptr, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS && type == REG_SZ) lastDir.assign(buf, wcsnlen(buf, sz / 2));
    RegCloseKey(k);
}

void Settings::save() const {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    setDword(k, L"Theme", theme);
    setDword(k, L"Language", language);
    setDword(k, L"Accent", accent);
    setDword(k, L"Wheel", wheel);
    setDword(k, L"ImageBg", imageBg);
    setDword(k, L"Filmstrip", filmstrip);
    setDword(k, L"InfoPanel", infoPanel);
    setDword(k, L"Grid", grid);
    setDword(k, L"FitMode", fitMode);
    setDword(k, L"ThumbColor", thumbColor);
    setDword(k, L"ThumbColorAll", thumbColorAll);
    if (hasPlacement) RegSetValueExW(k, L"Placement", 0, REG_BINARY, (const BYTE*)&placement, sizeof placement);
    RegSetValueExW(k, L"LastDir", 0, REG_SZ, (const BYTE*)lastDir.c_str(), (DWORD)((lastDir.size() + 1) * 2));
    RegCloseKey(k);
}

bool systemDarkMode() {
    DWORD v = 1, sz = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                 RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v == 0;
}

uint32_t systemAccentRgb() {
    DWORD v = 0, sz = sizeof v;
    // AccentColor is stored as 0xAABBGGRR
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        return ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF);
    return 0;
}
}
