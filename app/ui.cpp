// DeepImage Viewer — UI toolkit implementation
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "ui.h"
#include "i18n.h"

namespace dv {

UI U;

uint64_t hid(const char* s, int n) {
    uint64_t h = 1469598103934665603ull;
    for (; *s; ++s) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    h = (h ^ (uint64_t)(n + 0x9E37)) * 1099511628211ull;
    return h ? h : 1;
}

void UI::begin() { building_.clear(); }
void UI::end() {
    regions_.swap(building_);
    if (hot_ && !hitTest(mx, my)) hot_ = 0;
}

Region& UI::add(uint64_t id, const D2D1_RECT_F& r) {
    building_.emplace_back();
    Region& g = building_.back();
    g.id = id;
    g.rect = r;
    g.layer = layer;
    return g;
}

const Region* UI::hitTest(float x, float y) const {
    for (auto it = regions_.rbegin(); it != regions_.rend(); ++it) {
        if (it->layer < modalLayer) continue;
        if (inside(it->rect, x, y)) return &*it;
    }
    return nullptr;
}

bool UI::overWidget(float x, float y) const {
    if (modalLayer > 0) return true; // modal swallows everything
    return hitTest(x, y) != nullptr;
}

HCURSOR UI::cursorAt(float x, float y) const {
    auto r = hitTest(x, y);
    return r && r->cursor ? r->cursor : nullptr;
}

bool UI::mouseMove(float x, float y) {
    mx = x; my = y;
    if (active_) {
        for (auto& r : regions_)
            if (r.id == active_ && r.drag) { r.drag(x, y, false); return true; }
    }
    auto r = hitTest(x, y);
    uint64_t h = r && r->enabled ? r->id : 0;
    if (h != hot_) {
        hot_ = h;
        hoverSince_ = GetTickCount();
        tipShown_ = false;
        return true;
    }
    return false;
}

bool UI::mouseDown(float x, float y) {
    mx = x; my = y;
    leftDown = true;
    auto r = hitTest(x, y);
    if (!r) return modalLayer > 0;
    tipShown_ = true; // hide tooltip while pressing
    if (!r->enabled) return true;
    active_ = r->id;
    if (r->drag) r->drag(x, y, true);
    return true;
}

bool UI::mouseUp(float x, float y) {
    mx = x; my = y;
    leftDown = false;
    uint64_t was = active_;
    active_ = 0;
    if (!was) return false;
    auto r = hitTest(x, y);
    if (r && r->id == was && r->click && r->enabled) {
        Action a = r->click; // copy: the callback may rebuild regions
        a();
    }
    return true;
}

bool UI::wheel(float x, float y, float delta) {
    // topmost region under the cursor that handles the wheel (e.g. the filmstrip behind its thumbnails)
    for (auto it = regions_.rbegin(); it != regions_.rend(); ++it) {
        if (it->layer < modalLayer || !inside(it->rect, x, y)) continue;
        if (it->wheel) { WheelFn f = it->wheel; f(delta); return true; }
    }
    return false;
}

bool UI::tooltipDue() {
    if (!hot_ || tipShown_ || leftDown) return false;
    if (GetTickCount() - hoverSince_ < 450) return false;
    tipShown_ = true;
    return true;
}

void UI::drawTooltip() {
    if (!hot_ || !tipShown_ || leftDown) return;
    const Region* r = nullptr;
    for (auto& g : regions_) if (g.id == hot_) r = &g;
    if (!r || (r->tip.empty() && r->tipText.empty())) return;
    std::wstring s = r->tip.empty() ? r->tipText : T(r->tip.c_str()), label, kbd;
    splitTip(s, label, kbd);
    const float fs = 12, pad = 9;
    float w = G.textWidth(label, fs, 500) + pad * 2;
    float kw = kbd.empty() ? 0 : G.textWidth(kbd, 10.5f, 600) + 10;
    if (kw) w += kw + 8;
    float h = 26;
    float x = (r->rect.left + r->rect.right) / 2 - w / 2;
    float y = r->rect.bottom + 8;
    if (rw(r->rect) < 60 && rh(r->rect) > 30 && r->rect.left < 80) { // vertical rail on the left
        x = r->rect.right + 10;
        y = (r->rect.top + r->rect.bottom) / 2 - h / 2;
    }
    if (y + h > G.H() - 6) y = r->rect.top - h - 8;
    x = clampf(x, 6, G.W() - w - 6);
    D2D1_RECT_F box = RW(x, y, w, h);
    G.shadow(box, 7, 0.8f);
    G.fill(box, P.tooltip, 7);
    G.text(label, R(x + pad, y, x + w, y + h), fs, P.tooltipText, 500);
    if (kw) {
        D2D1_RECT_F k = RW(x + w - pad - kw, y + 5, kw, h - 10);
        G.fill(k, rgba(0xFFFFFF, .14f), 4);
        G.text(kbd, k, 10.5f, P.tooltipText, 600, AC);
    }
}

void UI::glassPanel(const D2D1_RECT_F& r, float radius) {
    G.shadow(r, radius, 1);
    G.fill(r, P.glass, radius);
    G.stroke(r, P.glassLine, radius, 1);
}

void UI::toolButton(uint64_t id, const D2D1_RECT_F& r, const char* icon, const char* tip, Action a, bool on, bool enabled,
                    const wchar_t* label, float iconSize) {
    auto& g = add(id, r);
    g.click = std::move(a);
    if (tip) g.tip = tip;
    g.enabled = enabled;
    bool hot = isHot(id) && enabled, act = isActive(id);
    D2D1_COLOR_F fg = !enabled ? withA(P.text2, .35f) : on ? P.accent : hot ? P.text : P.text2;
    if (on) G.fill(r, P.accentSoft, 9);
    else if (act) G.fill(r, P.press, 9);
    else if (hot) G.fill(r, P.hover, 9);
    if (label && *label) {
        float lw = G.textWidth(label, 12.5f, 600);
        float total = iconSize + 6 + lw;
        float x0 = (r.left + r.right) / 2 - total / 2;
        G.icon(icon, x0 + iconSize / 2, (r.top + r.bottom) / 2, iconSize, on ? P.accent : enabled ? P.text : fg);
        G.text(label, R(x0 + iconSize + 6, r.top, r.right, r.bottom), 12.5f, on ? P.accent : P.text, 600);
    } else if (icon) {
        G.icon(icon, (r.left + r.right) / 2, (r.top + r.bottom) / 2, iconSize, fg);
    }
}

void UI::textButton(uint64_t id, const D2D1_RECT_F& r, const std::wstring& label, Action a, bool primary, const char* icon, bool danger) {
    auto& g = add(id, r);
    g.click = std::move(a);
    bool hot = isHot(id), act = isActive(id);
    float rad = std::min(10.f, rh(r) / 2 - 2);
    if (primary || danger) {
        D2D1_COLOR_F c = danger ? P.danger : P.accent;
        if (hot) c = mix(c, rgba(0xFFFFFF), .08f);
        if (act) c = mix(c, rgba(0x000000), .08f);
        if (!danger) G.fill(R(r.left, r.top + 3, r.right, r.bottom + 3), withA(P.accent, .22f), rad + 2);
        G.fill(r, c, rad);
    } else {
        G.fill(r, act ? P.press : hot ? mix(P.surface2, P.text, .06f) : P.surface2, rad);
        G.stroke(r, P.line, rad, 1);
    }
    D2D1_COLOR_F fg = primary || danger ? P.accentFg : P.text;
    float lw = G.textWidth(label, 13, 600);
    float iw = icon ? 18 + 7 : 0;
    float x0 = (r.left + r.right) / 2 - (lw + iw) / 2;
    if (icon) G.icon(icon, x0 + 9, (r.top + r.bottom) / 2, 16, fg);
    G.text(label, R(x0 + iw, r.top, x0 + iw + lw + 4, r.bottom), 13, fg, 600);
}

float UI::segmentedWidth(const std::vector<std::wstring>& items, const std::vector<const char*>& icons) {
    float w = 4;
    for (size_t i = 0; i < items.size(); ++i) w += G.textWidth(items[i], 12, 600) + 22 + (i < icons.size() && icons[i] ? 20 : 0);
    return w;
}

void UI::segmented(uint64_t id, const D2D1_RECT_F& r, const std::vector<std::wstring>& items, int sel, std::function<void(int)> on,
                   const std::vector<const char*>& icons) {
    G.fill(r, P.surface2, 9);
    G.stroke(r, P.line, 9, 1);
    float x = r.left + 2;
    for (size_t i = 0; i < items.size(); ++i) {
        bool hasIcon = i < icons.size() && icons[i];
        float w = G.textWidth(items[i], 12, 600) + 22 + (hasIcon ? 20 : 0);
        D2D1_RECT_F b = R(x, r.top + 2, x + w, r.bottom - 2);
        uint64_t bid = id ^ (0x100 + i);
        auto& g = add(bid, b);
        int idx = (int)i;
        g.click = [on, idx] { on(idx); };
        bool cur = (int)i == sel;
        if (cur) {
            G.fill(R(b.left, b.top + 1, b.right, b.bottom + 1), P.shadow, 7);
            G.fill(b, P.dark ? P.surface : P.surface, 7);
        } else if (isHot(bid)) G.fill(b, P.hover, 7);
        D2D1_COLOR_F fg = cur ? P.text : P.text2;
        float tx = b.left + 11;
        if (hasIcon) { G.icon(icons[i], tx + 7, (b.top + b.bottom) / 2, 14, fg); tx += 20; }
        G.text(items[i], R(tx, b.top, b.right, b.bottom), 12, fg, 600);
        x += w;
    }
}

void UI::toggleSwitch(uint64_t id, float x, float y, bool on, Action a) {
    D2D1_RECT_F r = RW(x, y, 36, 21);
    auto& g = add(id, r);
    g.click = std::move(a);
    G.fill(r, on ? P.accent : P.line2, 10.5f);
    float kx = on ? x + 36 - 10.5f : x + 10.5f;
    G.circle(kx, y + 10.5f, 9.f, rgba(0, .18f));
    G.circle(kx, y + 10.5f, 8.5f, rgba(0xFFFFFF));
}

void UI::slider(uint64_t id, const D2D1_RECT_F& r, float value, float mn, float mx_, std::function<void(float)> on, bool centered) {
    auto& g = add(id, R(r.left - 6, r.top, r.right + 6, r.bottom));
    float L = r.left, Rr = r.right;
    g.drag = [=](float x, float, bool) { on(mn + clampf((x - L) / (Rr - L), 0, 1) * (mx_ - mn)); };
    float cy = (r.top + r.bottom) / 2;
    float t = clampf((value - mn) / (mx_ - mn), 0, 1);
    float px = L + t * (Rr - L);
    G.fill(R(L, cy - 2, Rr, cy + 2), P.line2, 2);
    if (centered) {
        float c0 = L + (0 - mn) / (mx_ - mn) * (Rr - L);
        G.fill(R(std::min(c0, px), cy - 2, std::max(c0, px), cy + 2), P.accent, 2);
        G.fill(R(c0 - .75f, cy - 5, c0 + .75f, cy + 5), P.text3, 0);
    } else {
        G.fill(R(L, cy - 2, px, cy + 2), P.accent, 2);
    }
    bool hot = isHot(g.id) || isActive(g.id);
    G.circle(px, cy + 1, hot ? 9.f : 8.f, rgba(0, .22f));
    G.circle(px, cy, hot ? 8.f : 7.f, rgba(0xFFFFFF));
}

void UI::swatch(uint64_t id, float cx, float cy, const D2D1_COLOR_F& c, bool on, Action a) {
    auto& g = add(id, R(cx - 13, cy - 13, cx + 13, cy + 13));
    g.click = std::move(a);
    float r = isHot(id) ? 10.f : 9.f;
    G.circle(cx, cy, r + .5f, P.line2);
    G.circle(cx, cy, r, c);
    if (on) G.circle(cx, cy, 12.f, P.accent, false, 2);
}

} // namespace dv
