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
    int slideDelay = 3;   // slideshow seconds per image
    bool pinChrome = false; // toolbar, filmstrip and side arrows never auto-hide
    // 3D capture
    int capScale = 1;        // 0 screen size, 1 twice, 2 4K long side
    int capBg = 0;           // 0 viewer background, 1 transparent, 2 white
    bool capOverlay = true;  // measurements and notes drawn into the picture
    bool capOpen = true;     // open the capture in a new window, ready to mark up
    bool notesShow = true;   // 3D notes visible
    bool updateCheck = true; // look for a new version once a day (GitHub installs)
    uint32_t updateLast = 0; // day (since 1970) of the last check
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
