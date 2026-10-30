// DeepImage Viewer — lightweight UI toolkit
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
// Widgets are drawn every frame; their hit regions are retained from the last frame so that
// mouse handling (and the actions it triggers) never runs inside a paint.
#pragma once
#include "common.h"
#include "gfx.h"

namespace dv {

using Action = std::function<void()>;
using DragFn = std::function<void(float x, float y, bool first)>;
using WheelFn = std::function<void(float delta)>;

struct Region {
    uint64_t id = 0;
    D2D1_RECT_F rect{};
    int layer = 0;
    Action click;
    DragFn drag;
    WheelFn wheel;
    std::string tip;          // i18n key
    std::wstring tipText;     // literal tooltip
    bool enabled = true;
    HCURSOR cursor = nullptr;
};

uint64_t hid(const char* s, int n = 0);

class UI {
public:
    // frame lifecycle
    void begin();
    void end();
    void drawTooltip();

    // input (called by window procedure)
    bool mouseMove(float x, float y);         // returns true if redraw needed
    bool mouseDown(float x, float y);         // true if a widget took the press
    bool mouseUp(float x, float y);           // true if a widget handled it
    bool wheel(float x, float y, float delta);
    bool overWidget(float x, float y) const;
    HCURSOR cursorAt(float x, float y) const;
    bool tooltipDue();                        // hover timer elapsed and tooltip not yet shown
    void clearHover() { hot_ = 0; tipShown_ = false; }

    // layers (modal dialogs set this to block lower layers)
    int layer = 0;
    int modalLayer = 0;

    // region registration
    Region& add(uint64_t id, const D2D1_RECT_F& r);
    bool isHot(uint64_t id) const { return hot_ == id; }
    bool isActive(uint64_t id) const { return active_ == id; }

    // widgets
    void toolButton(uint64_t id, const D2D1_RECT_F& r, const char* icon, const char* tip, Action a, bool on = false,
                    bool enabled = true, const wchar_t* label = nullptr, float iconSize = 18);
    void textButton(uint64_t id, const D2D1_RECT_F& r, const std::wstring& label, Action a, bool primary = false,
                    const char* icon = nullptr, bool danger = false);
    void segmented(uint64_t id, const D2D1_RECT_F& r, const std::vector<std::wstring>& items, int sel, std::function<void(int)> on,
                   const std::vector<const char*>& icons = {});
    float segmentedWidth(const std::vector<std::wstring>& items, const std::vector<const char*>& icons = {});
    void toggleSwitch(uint64_t id, float x, float y, bool on, Action a);
    void slider(uint64_t id, const D2D1_RECT_F& r, float value, float mn, float mx, std::function<void(float)> on, bool centered = false);
    void swatch(uint64_t id, float cx, float cy, const D2D1_COLOR_F& c, bool on, Action a);
    void glassPanel(const D2D1_RECT_F& r, float radius = 14);

    float mx = -1, my = -1;
    bool leftDown = false;

private:
    const Region* hitTest(float x, float y) const;
    std::vector<Region> regions_, building_;
    uint64_t hot_ = 0, active_ = 0;
    DWORD hoverSince_ = 0;
    bool tipShown_ = false;
};

extern UI U;

} // namespace dv
