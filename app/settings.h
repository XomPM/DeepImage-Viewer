#pragma once
#include <string>
#include <windows.h>

namespace dv {
struct Settings {
    int theme = 0;        // 0 system, 1 light, 2 dark
    int language = 0;     // 0 system, 1 ko, 2 en
    int accent = 0;       // 0 system, 1 brand
    int wheel = 0;        // 0 zoom, 1 navigate
    int imageBg = 0;      // 0 plain, 1 checker
    bool filmstrip = true;
    bool infoPanel = false;
    bool grid = true;
    uint32_t thumbColor = 0;   // 0xRRGGBB | 0x1000000 when set; 0 = built-in grey
    bool thumbColorAll = false;
    int fitMode = 0;      // 0 whole, 1 fill, 2 width, 3 height, 4 actual size
    WINDOWPLACEMENT placement{};
    bool hasPlacement = false;
    std::wstring lastDir;
    void load();
    void save() const;
};
extern Settings S;

bool systemDarkMode();
uint32_t systemAccentRgb();
}
