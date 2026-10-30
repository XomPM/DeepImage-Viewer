// DeepImage Viewer — rendering of the whole interface
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "app.h"
#include "i18n.h"
#include "settings.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

namespace dv {

namespace {
const uint32_t kPen[] = {0xFF3B30, 0xFF9500, 0xFFCC00, 0x34C759, 0x0A84FF, 0xAF52DE, 0xFFFFFF, 0x1C1C1E};
const uint32_t kHl[] = {0xFFE14D, 0x9DFF6B, 0xFF8AD8, 0x7FD8FF, 0xFFB35C};
float cxOf(const D2D1_RECT_F& r) { return (r.left + r.right) / 2; }
float cyOf(const D2D1_RECT_F& r) { return (r.top + r.bottom) / 2; }
bool sameColor(D2D1_COLOR_F a, uint32_t rgb) {
    auto b = rgba(rgb);
    return std::fabs(a.r - b.r) < 0.01f && std::fabs(a.g - b.g) < 0.01f && std::fabs(a.b - b.b) < 0.01f;
}
uint32_t toRgb(D2D1_COLOR_F c) { return ((uint32_t)(c.r * 255) << 16) | ((uint32_t)(c.g * 255) << 8) | (uint32_t)(c.b * 255); }
} // namespace

// ======================================================================== layout / frame
void App::layout() {
    float W = G.W(), H = G.H();
    float tH = titleH();
    float sH = (S.filmstrip && mode != Mode::Empty && !fullscreen && !files.empty()) ? 92.f : 0.f;
    float side = 0;
    if (mode != Mode::Empty && !fullscreen) {
        if (panel == EditPanel::Adjust) side = 300;
        else if (S.infoPanel) side = 296;
    }
    rcTitle = R(0, 0, W, tH);
    rcStage = R(0, tH, W, H - sH);
    rcViewport = R(0, tH, W - side, H - sH);
    rcSide = R(W - side, tH, W, H - sH);
    rcStrip = R(0, H - sH, W, H);
    view.padT = (fullscreen ? 12.f : 66.f);
    view.padB = mode == Mode::Image && panel == EditPanel::Crop ? 76.f : 52.f;
    view.padL = 72.f;
    view.padR = 72.f;
    if (compare && mode == Mode::Image) {
        float mid = std::round((rcViewport.left + rcViewport.right) / 2);
        rcPaneL = R(rcViewport.left, rcViewport.top, mid - 1, rcViewport.bottom);
        rcPaneR = R(mid + 1, rcViewport.top, rcViewport.right, rcViewport.bottom);
        view.padR = 20.f;
        view.padB = 58.f;
        viewR.padT = view.padT;
        viewR.padB = 58.f;
        viewR.padL = 20.f;
        viewR.padR = 72.f;
        view.setViewport(rcPaneL);
        viewR.setViewport(rcPaneR);
    } else {
        rcPaneL = rcViewport;
        rcPaneR = R(rcViewport.right, rcViewport.top, rcViewport.right, rcViewport.bottom);
        view.setViewport(rcViewport);
    }
}

void App::paint() {
    if (!G.dc) return;
    layout();
    if (mode == Mode::Model && mv.hasModel() && errorKey.empty())
        mv.render((UINT)std::max(1.f, rw(rcViewport) * G.scale), (UINT)std::max(1.f, rh(rcViewport) * G.scale));
    if (!G.beginFrame()) return;
    view.step();
    if (compare) viewR.step();
    if (fxActive) fxActive = fx.build(adj);
    U.begin();
    U.layer = 0;
    U.modalLayer = modal.kind ? 10 : 0;
    drawBackground();
    if (mode == Mode::Empty) drawEmpty();
    else if (mode == Mode::Image) drawImageStage();
    else drawModelStage();
    drawLoading();
    if (mode != Mode::Empty && errorKey.empty() && !loading) {
        drawNav();
        drawToolbars();
        drawStatus();
    } else if (mode != Mode::Empty) {
        drawNav();
    }
    if (rw(rcSide) > 0) {
        if (panel == EditPanel::Adjust) drawAdjustPanel();
        else drawInfoPanel();
    }
    drawFilmstrip();
    drawTitlebar();
    drawToasts();
    if (modal.kind) {
        U.layer = 10;
        drawModal();
        U.layer = 0;
    }
    drawDrop();
    U.drawTooltip();
    U.end();
    G.endFrame();
    ensureTimer();
}

void App::drawBackground() {
    G.dc->Clear(P.bg);
    if (mode == Mode::Empty) {
        G.fill(rcStage, P.canvas);
        // soft accent glow behind the mark
        D2D1_GRADIENT_STOP st[2] = {{0, withA(P.accent, P.dark ? .10f : .08f)}, {1, withA(P.accent, 0)}};
        ComPtr<ID2D1GradientStopCollection> sc;
        G.dc->CreateGradientStopCollection(st, 2, &sc);
        ComPtr<ID2D1RadialGradientBrush> rb;
        G.dc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cxOf(rcStage), rcStage.top + rh(rcStage) * 0.38f), {0, 0},
                                                                            rw(rcStage) * 0.45f, rh(rcStage) * 0.45f), sc.Get(), &rb);
        G.dc->FillRectangle(rcStage, rb.Get());
        return;
    }
    G.fill(rcViewport, P.canvas);
    if (mode == Mode::Image && S.imageBg == 1) {
        static ComPtr<ID2D1BitmapBrush> chk;
        static bool chkDark = false;
        if (!chk || chkDark != P.dark) {
            chk.Reset();
            ComPtr<ID2D1BitmapRenderTarget> rt;
            G.dc->CreateCompatibleRenderTarget(D2D1::SizeF(20, 20), &rt);
            rt->BeginDraw();
            rt->Clear(P.chkA);
            ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(P.chkB, &b);
            rt->FillRectangle(R(0, 0, 10, 10), b.Get());
            rt->FillRectangle(R(10, 10, 20, 20), b.Get());
            rt->EndDraw();
            ComPtr<ID2D1Bitmap> bmp;
            rt->GetBitmap(&bmp);
            G.dc->CreateBitmapBrush(bmp.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP), &chk);
            chkDark = P.dark;
        }
        G.dc->FillRectangle(rcViewport, chk.Get());
    }
    // vignette for depth
    D2D1_GRADIENT_STOP st[2] = {{0.55f, withA(P.canvas2, 0)}, {1.f, withA(P.canvas2, .7f)}};
    ComPtr<ID2D1GradientStopCollection> sc;
    G.dc->CreateGradientStopCollection(st, 2, &sc);
    ComPtr<ID2D1RadialGradientBrush> rb;
    G.dc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cxOf(rcViewport), rcViewport.top + rh(rcViewport) * .4f), {0, 0},
                                                                        rw(rcViewport) * .75f, rh(rcViewport) * .85f), sc.Get(), &rb);
    G.dc->FillRectangle(rcViewport, rb.Get());
}

// ======================================================================== title bar
void App::drawTitlebar() {
    if (fullscreen) return;
    const float W = G.W();
    G.fill(rcTitle, P.bg);
    G.line(0, rcTitle.bottom - .5f, W, rcTitle.bottom - .5f, P.line);
    const bool active = GetActiveWindow() == hwnd;
    // brand
    float x = 14;
    if (markBmp) G.dc->DrawBitmap(markBmp.Get(), RW(x, 11, 22, 22), 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    x += 30;
    G.text(L"Deep", R(x, 0, x + 60, 44), 13.5f, P.text2, 500);
    float dw = G.textWidth(L"Deep", 13.5f, 500);
    G.text(L"Image", R(x + dw, 0, x + 200, 44), 13.5f, P.text, 600);
    x += dw + G.textWidth(L"Image", 13.5f, 600) + 14;
    G.line(x, 13, x, 31, P.line2);
    x += 10;
    float ow = G.textWidth(T("open"), 13, 500) + 40;
    U.toolButton(hid("tb.open"), RW(x, 7, ow, 30), "folder-open", "open.file",
                 [this] { guardUnsaved([this] { openDialog(); }); }, false, true, T("open"), 17);
    // right side actions (before caption buttons)
    float rx = W - 46 * 3 - 8;
    const char* themeIcon = S.theme == 0 ? "monitor" : S.theme == 1 ? "sun" : "moon";
    rx -= 34;
    U.toolButton(hid("tb.about"), RW(rx, 7, 32, 30), "info", "about", [this] { openAbout(); });
    rx -= 34;
    U.toolButton(hid("tb.set"), RW(rx, 7, 32, 30), "settings", "settings", [this] { openSettings(); });
    rx -= 36;
    {
        D2D1_RECT_F r = RW(rx, 7, 34, 30);
        auto& g = U.add(hid("tb.lang"), r);
        g.tip = "language";
        g.click = [this] { S.language = lang() == Lang::Ko ? 2 : 1; S.save(); applyLanguage(); };
        if (U.isHot(g.id)) G.fill(r, P.hover, 9);
        D2D1_RECT_F pill = RW(rx + 6, 15, 22, 15);
        G.stroke(pill, U.isHot(g.id) ? P.text : P.text2, 4, 1.5f);
        G.text(lang() == Lang::Ko ? L"KO" : L"EN", pill, 9.5f, U.isHot(g.id) ? P.text : P.text2, 700, AC);
    }
    rx -= 34;
    U.toolButton(hid("tb.theme"), RW(rx, 7, 32, 30), themeIcon, "theme", [this] {
        S.theme = (S.theme + 1) % 3;
        S.save();
        applyTheme();
        toast(std::wstring(T("theme")) + L" · " + T(S.theme == 0 ? "theme.system" : S.theme == 1 ? "theme.light" : "theme.dark"));
    });
    // centre: file name
    if (!cur.path.empty() && mode != Mode::Empty) {
        std::wstring ext = cur.ext;
        for (auto& c : ext) c = (wchar_t)towupper(c);
        std::wstring count = Tf("count", {{L"i", std::to_wstring(index + 1)}, {L"n", std::to_wstring(files.size())}});
        float pw = G.textWidth(ext, 10, 700) + 12, nw = std::min(G.textWidth(cur.name, 13, 600), W * 0.32f), cw = G.textWidth(count, 12, 400);
        bool dirty = editDirty || markup.dirty(cur.path);
        float total = pw + 9 + nw + 9 + cw + (dirty ? 16 : 0);
        float cxp = W / 2 - total / 2;
        if (cxp < x + ow + 16) cxp = x + ow + 16;
        D2D1_RECT_F pill = RW(cxp, 13.5f, pw, 17);
        G.fill(pill, P.accentSoft, 4);
        G.text(ext, pill, 10, P.accent, 700, AC);
        G.textEllipsis(cur.name, RW(cxp + pw + 9, 0, nw + 2, 44), 13, active ? P.text : P.text2, 600);
        G.text(count, RW(cxp + pw + 18 + nw, 0, cw + 4, 44), 12, P.text3);
        if (dirty) G.circle(cxp + total - 4, 22, 3.5f, P.accent);
    }
    // caption buttons
    for (int i = 0; i < 3; ++i) {
        D2D1_RECT_F r = RW(W - 46 * (3 - i), 0, 46, 44);
        bool hot = capHover == i + 1, pressed = capPress == i + 1;
        if (i == 2 && (hot || pressed)) G.fill(r, pressed ? rgba(0xB0281A) : rgba(0xC42B1C));
        else if (hot || pressed) G.fill(r, pressed ? P.press : P.hover);
        D2D1_COLOR_F c = (i == 2 && (hot || pressed)) ? rgba(0xFFFFFF) : active ? P.text : P.text3;
        float cx = cxOf(r), cy = cyOf(r);
        if (i == 0) G.line(cx - 5, cy, cx + 5, cy, c, 1);
        else if (i == 1) {
            if (IsZoomed(hwnd)) {
                G.stroke(R(cx - 5, cy - 3, cx + 3, cy + 5), c, 1.5f, 1);
                G.line(cx - 3, cy - 5, cx + 4, cy - 5, c, 1);
                G.line(cx + 5, cy - 3, cx + 5, cy + 3, c, 1);
                G.line(cx + 3.5f, cy - 5, cx + 5, cy - 3.5f, c, 1);
            } else G.stroke(R(cx - 5, cy - 5, cx + 5, cy + 5), c, 1.5f, 1);
        } else {
            G.line(cx - 5, cy - 5, cx + 5, cy + 5, c, 1);
            G.line(cx + 5, cy - 5, cx - 5, cy + 5, c, 1);
        }
    }
}

// ======================================================================== empty state
void App::drawEmpty() {
    float cx = cxOf(rcStage);
    float top = rcStage.top + std::max(40.f, (rh(rcStage) - 520) / 2);
    if (markBmp) {
        // soft drop shadow
        for (int i = 1; i <= 5; ++i) G.fill(RW(cx - 52 + i, top + 18 + i * 2.5f, 104 - i * 2, 104), withA(P.accent, .045f), 26 + i * 2);
        G.dc->DrawBitmap(markBmp.Get(), RW(cx - 52, top, 104, 104), 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    }
    float y = top + 126;
    G.text(L"DeepImage Viewer", R(0, y, G.W(), y + 36), 26, P.text, 600, AC);
    y += 40;
    G.text(T("tagline"), R(0, y, G.W(), y + 22), 14, P.text2, 400, AC);
    y += 44;
    std::wstring lbl = T("open.file");
    float bw = G.textWidth(lbl, 14, 600) + 40 + 18 + 54;
    D2D1_RECT_F b = RW(cx - bw / 2, y, bw, 46);
    {
        auto& g = U.add(hid("empty.open"), b);
        g.click = [this] { openDialog(); };
        bool hot = U.isHot(g.id);
        G.fill(R(b.left + 4, b.top + 6, b.right - 4, b.bottom + 6), withA(P.accent, .28f), 14);
        G.fill(b, hot ? mix(P.accent, rgba(0xFFFFFF), .08f) : P.accent, 12);
        float x = b.left + 18;
        G.icon("folder-open", x + 9, cyOf(b), 18, P.accentFg);
        G.text(lbl, R(x + 26, b.top, b.right, b.bottom), 14, P.accentFg, 600);
        D2D1_RECT_F k = RW(b.right - 64, b.top + 13, 48, 20);
        G.fill(k, withA(P.accentFg, .2f), 5);
        G.text(L"Ctrl O", k, 10.5f, P.accentFg, 600, AC);
    }
    y += 64;
    G.text(T("open.hint"), R(0, y, G.W(), y + 20), 12.5f, P.text3, 400, AC);
    y += 44;
    auto chips = [&](const wchar_t* label, const std::vector<std::wstring>& list, float yy) {
        float total = 0;
        for (auto& s : list) total += G.textWidth(s, 10.5f, 600) + 14 + 4;
        float maxW = 480;
        float lw = 70;
        float x0 = cx - (std::min(total, maxW) + lw + 12) / 2;
        G.text(label, RW(x0, yy, lw, 22), 11.5f, P.text3, 500, AR);
        float x = x0 + lw + 12, row = yy;
        for (auto& s : list) {
            float w = G.textWidth(s, 10.5f, 600) + 14;
            if (x + w > x0 + lw + 12 + maxW) { x = x0 + lw + 12; row += 26; }
            D2D1_RECT_F r = RW(x, row, w, 22);
            G.fill(r, P.surface, 5);
            G.stroke(r, P.line, 5, 1);
            G.text(s, r, 10.5f, P.text2, 600, AC);
            x += w + 4;
        }
        return row + 26;
    };
    y = chips(T("open.formats.images"), {L"JPG", L"PNG", L"GIF", L"WEBP", L"HEIC", L"AVIF", L"TIFF", L"BMP", L"SVG", L"PSD", L"TGA", L"RAW"}, y) + 6;
    chips(T("open.formats.models"), {L"STL", L"3MF", L"OBJ", L"PLY", L"GLTF", L"GLB", L"FBX", L"STEP", L"IGES", L"BREP", L"DAE", L"3DS", L"AMF"}, y);
}

// ======================================================================== image
void App::drawComparePane(ImageDoc& d, ImageView& v, const D2D1_RECT_F& pane, uint32_t color, int idx, bool loadingPane, const std::string& err) {
    auto dc = G.dc.Get();
    dc->PushAxisAlignedClip(pane, D2D1_ANTIALIAS_MODE_ALIASED);
    if (d.loaded()) {
        dc->SetTransform(v.matrix());
        const float px = 1.f / v.s;
        G.stroke(R(-px, -px, v.imgW + px, v.imgH + px), P.line2, 0, px);
        d.draw(dc, v.s);
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
    } else if (loadingPane) {
        G.text(T("loading"), pane, 12.5f, P.text2, 500, AC);
    } else if (!err.empty()) {
        G.icon("image-off", cxOf(pane), cyOf(pane) - 18, 26, P.text3);
        G.text(T(err.c_str()), R(pane.left, cyOf(pane), pane.right, cyOf(pane) + 24), 13, P.text2, 600, AC);
    }
    // identity bar under the image, same colour as its thumbnail outline
    D2D1_COLOR_F c = rgba(color);
    float bw = std::min(rw(pane) - 48, 420.f);
    D2D1_RECT_F bar = RW(cxOf(pane) - bw / 2, pane.bottom - 14, bw, 4);
    G.fill(bar, c, 2);
    if (idx >= 0 && idx < (int)files.size()) {
        std::wstring name = files[idx].name;
        float tw = std::min(G.textWidth(name, 12, 600), bw - 24);
        D2D1_RECT_F lr = RW(cxOf(pane) - tw / 2 - 12, pane.bottom - 44, tw + 24, 24);
        G.fill(lr, P.glass, 7);
        G.stroke(lr, P.glassLine, 7, 1);
        G.circle(lr.left + 9, cyOf(lr), 3.5f, c);
        G.textEllipsis(name, R(lr.left + 17, lr.top, lr.right - 6, lr.bottom), 12, P.text, 600);
    }
    dc->PopAxisAlignedClip();
}

void App::drawImageStage() {
    if (compare) {
        drawComparePane(doc, view, rcPaneL, kCmpLeft, index, loading, errorKey);
        drawComparePane(docR, viewR, rcPaneR, kCmpRight, indexR, loadingR, errorR);
        G.fill(R(rcPaneL.right, rcViewport.top, rcPaneR.left, rcViewport.bottom), P.line2);
        return;
    }
    if (!doc.loaded()) return;
    auto dc = G.dc.Get();
    dc->PushAxisAlignedClip(rcViewport, D2D1_ANTIALIAS_MODE_ALIASED);
    dc->SetTransform(view.matrix());
    const float px = 1.f / view.s;
    // hairline frame so the image edge reads on similar backgrounds
    G.stroke(R(-px, -px, view.imgW + px, view.imgH + px), P.line2, 0, px);
    bool useFx = fxActive && fx.head();
    doc.draw(dc, view.s, useFx ? fx.head() : nullptr, useFx ? fx.tail() : nullptr);
    if (panel != EditPanel::Crop) markup.draw(dc, true, annot && markup.tool == Tool::Hand ? px : 0);
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    if (panel == EditPanel::Crop) drawCropOverlay();
    if (annot && markup.tool == Tool::Eraser && mouseInViewport && !U.overWidget(U.mx, U.my)) {
        float r = markup.ts().width / 2;
        G.circle(U.mx, U.my, r, rgba(0xFFFFFF, .95f), false, 1.5f);
        G.circle(U.mx, U.my, r, rgba(0, .5f), false, 1);
    }
    dc->PopAxisAlignedClip();
}

void App::drawCropOverlay() {
    auto a = view.toScreen(crop.left, crop.top), b = view.toScreen(crop.right, crop.bottom);
    D2D1_RECT_F c = R(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
    D2D1_COLOR_F dim = rgba(0x000000, .55f);
    G.fill(R(rcViewport.left, rcViewport.top, rcViewport.right, c.top), dim);
    G.fill(R(rcViewport.left, c.bottom, rcViewport.right, rcViewport.bottom), dim);
    G.fill(R(rcViewport.left, c.top, c.left, c.bottom), dim);
    G.fill(R(c.right, c.top, rcViewport.right, c.bottom), dim);
    for (int i = 1; i < 3; ++i) {
        float x = c.left + rw(c) * i / 3, y = c.top + rh(c) * i / 3;
        G.line(x, c.top, x, c.bottom, rgba(0xFFFFFF, .35f), 1);
        G.line(c.left, y, c.right, y, rgba(0xFFFFFF, .35f), 1);
    }
    G.stroke(c, rgba(0xFFFFFF, .95f), 0, 1.5f);
    // corner + edge handles
    const float L = 16, T2 = 3.5f;
    auto corner = [&](float x, float y, float sx, float sy) {
        G.fill(R(std::min(x, x + sx * L), std::min(y, y + sy * T2), std::max(x, x + sx * L), std::max(y, y + sy * T2)), rgba(0xFFFFFF));
        G.fill(R(std::min(x, x + sx * T2), std::min(y, y + sy * L), std::max(x, x + sx * T2), std::max(y, y + sy * L)), rgba(0xFFFFFF));
    };
    corner(c.left - 2, c.top - 2, 1, 1);
    corner(c.right + 2, c.top - 2, -1, 1);
    corner(c.right + 2, c.bottom + 2, -1, -1);
    corner(c.left - 2, c.bottom + 2, 1, -1);
    G.fill(RW(cxOf(c) - 10, c.top - 2.5f, 20, 4), rgba(0xFFFFFF), 2);
    G.fill(RW(cxOf(c) - 10, c.bottom - 1.5f, 20, 4), rgba(0xFFFFFF), 2);
    G.fill(RW(c.left - 2.5f, cyOf(c) - 10, 4, 20), rgba(0xFFFFFF), 2);
    G.fill(RW(c.right - 1.5f, cyOf(c) - 10, 4, 20), rgba(0xFFFFFF), 2);
    std::wstring sz = fmtf(L"%u × %u", (UINT)std::lround(rw(crop)), (UINT)std::lround(rh(crop)));
    float sw = G.textWidth(sz, 11.5f, 600) + 16;
    D2D1_RECT_F chip = RW(cxOf(c) - sw / 2, std::max(rcViewport.top + 6, c.top - 32), sw, 24);
    G.fill(chip, rgba(0x000000, .6f), 6);
    G.text(sz, chip, 11.5f, rgba(0xFFFFFF), 600, AC);
}

// ======================================================================== 3D
void App::drawModelStage() {
    if (!mv.hasModel() || !mv.bitmap() || !errorKey.empty()) return;
    G.dc->DrawBitmap(mv.bitmap(), rcViewport, 1, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    drawGizmo();
    if (!loading) drawPrintButton();
    if (!hintGone && !loading) {
        std::wstring h = T("v3d.hint");
        float w = G.textWidth(h, 11.5f, 500) + 42;
        D2D1_RECT_F r = RW(cxOf(rcViewport) - w / 2, rcViewport.bottom - 42, w, 28);
        G.fill(r, P.glass, 8);
        G.stroke(r, P.glassLine, 8, 1);
        G.icon("compass", r.left + 17, cyOf(r), 14, P.text2);
        G.text(h, R(r.left + 30, r.top, r.right, r.bottom), 11.5f, P.text2, 500);
    }
}

void App::drawPrintButton() {
    std::wstring label = T("print3d");
    float w = G.textWidth(label, 12, 600) + 52;
    const float right = rcViewport.right - 14 - 92 - 14; // left of the axis gizmo
    D2D1_RECT_F r = RW(right - w, rcViewport.bottom - 44, w, 32);
    uint64_t id = hid("print3d");
    auto& g = U.add(id, r);
    g.tip = "print3d.tip";
    g.click = [this] { open3dPrint(); };
    bool hot = U.isHot(id), down = U.isActive(id);
    G.fill(r, down ? P.press : hot ? P.surface : P.glass, 9);
    G.stroke(r, P.glassLine, 9, 1);
    D2D1_COLOR_F c = hot ? P.text : P.text2;
    G.icon("printer", r.left + 19, cyOf(r), 16, c);
    G.text(label, R(r.left + 42, r.top, r.right - 8, r.bottom), 12, c, 600);
}

void App::drawGizmo() {
    float d[3][3];
    mv.axisDirs(d);
    const float S0 = 92, m = 14;
    float cx = rcViewport.right - m - S0 / 2, cy = rcViewport.bottom - m - S0 / 2;
    const uint32_t col[3] = {0xF2555A, 0x5BC46A, 0x4C8DF6};
    const wchar_t* lbl[3] = {L"X", L"Y", L"Z"};
    struct E { int axis; bool neg; float depth; };
    std::vector<E> list;
    for (int a = 0; a < 3; ++a) { list.push_back({a, false, d[a][2]}); list.push_back({a, true, -d[a][2]}); }
    std::sort(list.begin(), list.end(), [](const E& a, const E& b) { return a.depth < b.depth; });
    const float L = 32;
    for (auto& e : list) {
        float sx = d[e.axis][0] * (e.neg ? -1 : 1), sy = d[e.axis][1] * (e.neg ? -1 : 1);
        float ex = cx + sx * L, ey = cy + sy * L;
        D2D1_COLOR_F c = rgba(col[e.axis], e.neg ? .55f : 1.f);
        uint64_t id = hid("gizmo", e.axis * 2 + e.neg);
        auto& g = U.add(id, R(ex - 11, ey - 11, ex + 11, ey + 11));
        int ax = e.axis;
        bool ng = e.neg;
        g.click = [this, ax, ng] { mv.viewModelAxis(ax, ng); hintGone = true; ensureTimer(); requestPaint(); };
        bool hot = U.isHot(id);
        if (!e.neg) {
            G.line(cx, cy, ex, ey, c, 2);
            G.circle(ex, ey, hot ? 10.5f : 9.5f, c);
            G.text(lbl[e.axis], R(ex - 10, ey - 10, ex + 10, ey + 10), 10.5f, rgba(0xFFFFFF), 700, AC);
        } else {
            G.circle(ex, ey, hot ? 7.f : 5.5f, c);
        }
    }
}

// ======================================================================== toolbars
float App::toolbarWidth(const std::vector<TbItem>& items) {
    float w = 10;
    for (auto& it : items) {
        switch (it.type) {
        case TbItem::Btn: w += it.label ? 34 + 8 + G.textWidth(it.label, 12.5f, 600) : 34; break;
        case TbItem::Sep: w += 11; break;
        case TbItem::Zoom: w += 62; break;
        case TbItem::Swatches: w += (markup.styleTool() == Tool::Hl ? 5 : 8) * 26 + 34; break;
        case TbItem::Width: w += 172; break;
        case TbItem::Primary: w += G.textWidth(it.label, 12.5f, 600) + 44; break;
        }
        w += 2;
    }
    return w;
}

void App::toolbar(uint64_t id, const std::vector<TbItem>& items, float cx, float top) {
    float w = toolbarWidth(items);
    D2D1_RECT_F r = RW(std::round(cx - w / 2), top, w, 44);
    U.glassPanel(r, 14);
    U.add(id, r); // background swallows clicks between buttons
    float x = r.left + 5;
    int n = 0;
    for (auto& it : items) {
        ++n;
        switch (it.type) {
        case TbItem::Btn: {
            float bw = it.label ? 34 + 8 + G.textWidth(it.label, 12.5f, 600) : 34;
            U.toolButton(id ^ (n * 7919), RW(x, r.top + 5, bw, 34), it.icon, it.tip, it.action, it.on, it.enabled, it.label);
            x += bw;
            break;
        }
        case TbItem::Sep:
            G.fill(RW(x + 5, r.top + 12, 1, 20), P.line2);
            x += 11;
            break;
        case TbItem::Zoom: {
            D2D1_RECT_F b = RW(x, r.top + 7, 62, 30);
            auto& g = U.add(id ^ (n * 7919), b);
            g.click = it.action;
            g.tip = "zoom.fit";
            if (U.isHot(g.id)) G.fill(b, P.hover, 7);
            float z = view.s * 100;
            G.text(z >= 1000 ? fmtf(L"%.0f%%", z) : fmtf(L"%.1f%%", z), b, 12, P.text, 600, AC);
            x += 62;
            break;
        }
        case TbItem::Swatches: {
            bool hl = markup.styleTool() == Tool::Hl;
            const D2D1_COLOR_F curC = markup.curColor();
            int cnt = hl ? 5 : 8;
            bool matched = false;
            for (int i = 0; i < cnt; ++i) {
                uint32_t c = hl ? kHl[i] : kPen[i];
                bool on = sameColor(curC, c);
                matched |= on;
                U.swatch(id ^ (0x5000 + i), x + 13 + i * 26, r.top + 22, rgba(c), on, [this, c] { markup.setColor(rgba(c)); requestPaint(); });
            }
            x += cnt * 26;
            // custom colour (Windows colour dialog)
            D2D1_RECT_F b = RW(x + 4, r.top + 9, 26, 26);
            auto& g = U.add(id ^ 0x5100, b);
            g.tip = "tool.color";
            g.click = [this] {
                static COLORREF custom[16] = {};
                CHOOSECOLORW cc{sizeof cc};
                cc.hwndOwner = hwnd;
                uint32_t cur = toRgb(markup.curColor());
                cc.rgbResult = RGB((cur >> 16) & 255, (cur >> 8) & 255, cur & 255);
                cc.lpCustColors = custom;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorW(&cc)) markup.setColor(rgba((GetRValue(cc.rgbResult) << 16) | (GetGValue(cc.rgbResult) << 8) | GetBValue(cc.rgbResult)));
                requestPaint();
            };
            if (!matched) {
                G.circle(cxOf(b), cyOf(b), 9, curC);
                G.circle(cxOf(b), cyOf(b), 12, P.accent, false, 2);
            } else {
                if (U.isHot(g.id)) G.fill(b, P.hover, 13);
                G.icon("palette", cxOf(b), cyOf(b), 16, P.text2);
            }
            x += 34;
            break;
        }
        case TbItem::Width: {
            const Tool stt = markup.styleTool();
            const float k = 1.f / view.s;
            const float wv = markup.curWidth(k);
            float mn = stt == Tool::Mosaic ? 4.f : 1.f;
            float mx = stt == Tool::Hl ? 80.f : stt == Tool::Eraser ? 120.f : stt == Tool::Mosaic ? 64.f : 40.f;
            float dot = clampf(wv, 2, 20);
            if (stt == Tool::Mosaic) G.icon("mosaic", x + 14, r.top + 22, 16, P.text2);
            else {
                G.circle(x + 14, r.top + 22, dot / 2 + .5f, P.line2);
                G.circle(x + 14, r.top + 22, dot / 2, stt == Tool::Eraser ? P.text2 : markup.curColor());
            }
            U.slider(id ^ 0x6000, RW(x + 32, r.top + 12, 104, 20), clampf(wv, mn, mx), mn, mx, [this, k](float v) { markup.setWidth(std::round(v), k); requestPaint(); });
            G.text(fmtf(L"%.0f", wv), RW(x + 142, r.top, 30, 44), 11.5f, P.text2, 600);
            x += 172;
            break;
        }
        case TbItem::Primary: {
            float bw = G.textWidth(it.label, 12.5f, 600) + 44;
            U.textButton(id ^ (n * 7919), RW(x + 2, r.top + 6, bw - 4, 32), it.label, it.action, true, it.icon);
            x += bw;
            break;
        }
        }
        x += 2;
    }
}

void App::drawToolbars() {
    const float cx = cxOf(rcViewport), top = rcViewport.top + 14;
    if (fullscreen && !mouseInViewport) return;
    if (mode == Mode::Image && doc.loaded()) {
        const float vcx = cxOf(rcViewport), vcy = cyOf(rcViewport);
        if (annot) {
            // the hand tool edits the selected shape, so it shows that shape's controls
            const Tool stt = markup.styleTool();
            const bool hasColor = stt != Tool::Eraser && stt != Tool::Hand && stt != Tool::Mosaic;
            const bool hasWidth = stt != Tool::Hand && stt != Tool::Mask;
            const bool hasFill = markup.tool == Tool::Rect || markup.tool == Tool::Ellipse;
            std::vector<TbItem> it;
            if (hasColor) { it.push_back({TbItem::Swatches}); if (hasWidth) it.push_back({TbItem::Sep}); }
            if (hasWidth) it.push_back({TbItem::Width});
            if (hasFill) it.push_back({TbItem::Btn, "square-dashed", "tool.fill", [this] { markup.ts().fill = !markup.ts().fill; requestPaint(); }, markup.ts().fill});
            if (markup.tool == Tool::Hand && markup.selection())
                it.push_back({TbItem::Btn, "trash", "tool.delete", [this] { markup.deleteSelection(); requestPaint(); }});
            if (!it.empty()) it.push_back({TbItem::Sep});
            it.push_back({TbItem::Btn, "trash", "clear", [this] {
                              confirm(T("clear"), T("confirm.clear"), {{T("cancel"), nullptr}, {T("clear"), [this] { markup.clear(); toast(T("cleared")); }, false, true}});
                          }, false, markup.count() > 0});
            it.push_back({TbItem::Btn, "copy", "copy", [this] { copyImage(); }});
            it.push_back({TbItem::Btn, "download", "save", [this] { saveAs(); }});
            it.push_back({TbItem::Primary, "check", nullptr, [this] { toggleAnnot(0); }, false, true, T("done")});
            toolbar(hid("tb.annot"), it, cx, top);
            drawRail();
        } else if (editMode) {
            if (panel == EditPanel::Crop) {
                // bottom crop bar
                std::vector<std::wstring> names = {T("edit.free"), T("edit.original"), L"1:1", L"4:3", L"3:2", L"16:9"};
                float sw = U.segmentedWidth(names);
                float bw1 = G.textWidth(T("cancel"), 13, 600) + 32, bw2 = G.textWidth(T("apply"), 13, 600) + 32;
                float w = sw + bw1 + bw2 + 40;
                D2D1_RECT_F r = RW(cx - w / 2, rcViewport.bottom - 62, w, 48);
                U.glassPanel(r, 14);
                U.add(hid("crop.bar"), r);
                U.segmented(hid("crop.aspect"), RW(r.left + 8, r.top + 8, sw, 32), names, cropAspect, [this](int i) {
                    cropAspect = i;
                    float ratios[] = {0, (float)doc.W() / doc.H(), 1, 4.f / 3, 3.f / 2, 16.f / 9};
                    if (i > 0) {
                        float ar = ratios[i], W = (float)doc.W(), H = (float)doc.H();
                        float w = W, h = W / ar;
                        if (h > H) { h = H; w = H * ar; }
                        crop = RW((W - w) / 2, (H - h) / 2, w, h);
                    }
                    requestPaint();
                });
                U.textButton(hid("crop.cancel"), RW(r.left + 16 + sw, r.top + 8, bw1, 32), T("cancel"), [this] { panel = EditPanel::None; requestPaint(); });
                U.textButton(hid("crop.apply"), RW(r.left + 24 + sw + bw1, r.top + 8, bw2, 32), T("apply"), [this] { applyCrop(); }, true);
            }
            std::vector<TbItem> it = {
                {TbItem::Btn, "crop", "edit.crop", [this] { beginCrop(); }, panel == EditPanel::Crop, true, TL("edit.crop")},
                {TbItem::Btn, "scaling", "edit.resize", [this] { openResize(); }, false, true, TL("edit.resize")},
                {TbItem::Btn, "sliders-horizontal", "edit.adjust", [this] { panel == EditPanel::Adjust ? (void)(panel = EditPanel::None, adj = Adjust{}, fxActive = false) : beginAdjust(); requestPaint(); },
                 panel == EditPanel::Adjust, true, TL("edit.adjust")},
                {TbItem::Sep},
                {TbItem::Btn, "rotate-ccw", "rotate.left", [this] { view.rotate(-90); bakeOrientationIntoImage(); }, false, panel == EditPanel::None},
                {TbItem::Btn, "rotate-cw", "rotate.right", [this] { view.rotate(90); bakeOrientationIntoImage(); }, false, panel == EditPanel::None},
                {TbItem::Btn, "flip-h", "flip.h", [this] { view.flip(true); bakeOrientationIntoImage(); }, false, panel == EditPanel::None},
                {TbItem::Btn, "flip-v", "flip.v", [this] { view.flip(false); bakeOrientationIntoImage(); }, false, panel == EditPanel::None},
                {TbItem::Sep},
                {TbItem::Btn, "undo-2", "edit.undo", [this] { undoEdit(); }, false, !editUndo.empty()},
                {TbItem::Btn, "download", "save", [this] { saveAs(); }},
                {TbItem::Primary, "check", nullptr, [this] { toggleEdit(0); }, false, true, T("done")},
            };
            // crop label is long in Korean: keep labels only on wide windows
            if (rw(rcViewport) < 980) for (auto& i : it) if (i.type == TbItem::Btn) i.label = nullptr;
            toolbar(hid("tb.edit"), it, cx, top);
        } else {
            auto fitOn = [this](int m) { return S.fitMode == m && view.fitted; };
            auto all = [this](auto f) { eachView([&](ImageView& v, const D2D1_RECT_F&) { f(v); }); };
            std::vector<TbItem> it = {
                {TbItem::Btn, "zoom-out", "zoom.out", [this, vcx, vcy] { zoomViews(0.8f, vcx, vcy, true); }},
                {TbItem::Zoom, nullptr, nullptr, [this] { setFitMode(S.fitMode); }},
                {TbItem::Btn, "zoom-in", "zoom.in", [this, vcx, vcy] { zoomViews(1.25f, vcx, vcy, true); }},
                {TbItem::Sep},
                {TbItem::Btn, "scan", "zoom.fit", [this] { setFitMode(0); }, fitOn(0)},
                {TbItem::Btn, "fit-fill", "zoom.fill", [this] { setFitMode(1); }, fitOn(1)},
                {TbItem::Btn, "fit-width", "zoom.width", [this] { setFitMode(2); }, fitOn(2)},
                {TbItem::Btn, "fit-height", "zoom.height", [this] { setFitMode(3); }, fitOn(3)},
                {TbItem::Btn, "one-to-one", "zoom.actual", [this] { setFitMode(4); }, fitOn(4)},
                {TbItem::Sep},
                {TbItem::Btn, "rotate-ccw", "rotate.left", [all] { all([](ImageView& v) { v.rotate(-90); }); }},
                {TbItem::Btn, "rotate-cw", "rotate.right", [all] { all([](ImageView& v) { v.rotate(90); }); }},
                {TbItem::Btn, "flip-h", "flip.h", [all] { all([](ImageView& v) { v.flip(true); }); }},
                {TbItem::Btn, "flip-v", "flip.v", [all] { all([](ImageView& v) { v.flip(false); }); }},
                {TbItem::Sep},
                {TbItem::Btn, "columns-2", "compare", [this] { toggleCompare(); }, compare, files.size() > 1},
                {TbItem::Btn, "pen-line", "annotate", [this] { toggleAnnot(1); }, false, !compare, TL("annot.title")},
                {TbItem::Btn, "sliders-horizontal", "edit", [this] { toggleEdit(1); }, false, !compare, TL("edit")},
                {TbItem::Sep},
                {TbItem::Btn, "download", "save", [this] { saveAs(); }},
                {TbItem::Btn, "panel-right-open", "info", [this] { toggleInfo(); }, S.infoPanel},
                {TbItem::Btn, "maximize-2", "fullscreen", [this] { toggleFullscreen(); }, fullscreen},
            };
            if (rw(rcViewport) < 1060) for (auto& i : it) if (i.type == TbItem::Btn) i.label = nullptr;
            toolbar(hid("tb.image"), it, cx, top);
        }
    } else if (mode == Mode::Model && mv.hasModel()) {
        auto inv = [this] { mv.invalidate(); ensureTimer(); requestPaint(); };
        std::vector<TbItem> it = {
            {TbItem::Btn, "refresh-ccw", "v3d.reset", [this, inv] { mv.view("iso"); inv(); }},
            {TbItem::Btn, "view-front", "v3d.front", [this, inv] { mv.view("front"); inv(); }},
            {TbItem::Btn, "view-top", "v3d.top", [this, inv] { mv.view("top"); inv(); }},
            {TbItem::Btn, "view-right", "v3d.right", [this, inv] { mv.view("right"); inv(); }},
            {TbItem::Btn, "view-iso", "v3d.iso", [this, inv] { mv.view("iso"); inv(); }},
            {TbItem::Sep},
            {TbItem::Btn, "wire", "v3d.wire", [this, inv] { mv.wire = !mv.wire; inv(); }, mv.wire},
            {TbItem::Btn, "spline", "v3d.edges", [this, inv] { mv.setEdges(!mv.edges); inv(); }, mv.edges},
            {TbItem::Btn, "sun-medium", "v3d.clay", [this, inv] { mv.clay = !mv.clay; inv(); }, mv.clay},
            {TbItem::Sep},
            {TbItem::Btn, "grid-3x3", "v3d.grid", [this, inv] { mv.grid = !mv.grid; inv(); }, mv.grid},
            {TbItem::Btn, "bbox", "v3d.bbox", [this, inv] { mv.bbox = !mv.bbox; inv(); }, mv.bbox},
            {TbItem::Btn, "orbit", "v3d.rotate", [this, inv] { mv.autoRotate = !mv.autoRotate; inv(); }, mv.autoRotate},
            {TbItem::Sep},
            {TbItem::Btn, "camera", "v3d.shot", [this] { saveScreenshot(); }},
            {TbItem::Btn, "panel-right-open", "info", [this] { toggleInfo(); }, S.infoPanel},
            {TbItem::Btn, "maximize-2", "fullscreen", [this] { toggleFullscreen(); }, fullscreen},
        };
        toolbar(hid("tb.model"), it, cx, top);
        // up-axis chip (text button) sits right after the toolbar
        float w = toolbarWidth(it);
        D2D1_RECT_F b = RW(cx + w / 2 + 8, top + 5, 46, 34);
        U.glassPanel(R(b.left - 4, top, b.right + 4, top + 44), 14);
        auto& g = U.add(hid("tb.up"), b);
        g.tip = "v3d.upaxis";
        g.click = [this, inv] { mv.toggleUp(); inv(); };
        if (U.isHot(g.id)) G.fill(b, P.hover, 9);
        G.text(mv.upAxis() == 2 ? L"Z↑" : L"Y↑", b, 12, P.text, 700, AC);
    }
}

void App::drawRail() {
    struct RI { int type; Tool tool; const char* icon; const char* tip; };
    const RI items[] = {{0, Tool::Hand, "hand", "tool.hand"}, {1}, {0, Tool::Pen, "pen-line", "tool.pen"}, {0, Tool::Hl, "highlighter", "tool.hl"}, {1},
                        {0, Tool::Line, "minus", "tool.line"}, {0, Tool::Arrow, "move-up-right", "tool.arrow"}, {0, Tool::Rect, "square", "tool.rect"},
                        {0, Tool::Ellipse, "circle", "tool.ellipse"}, {1}, {0, Tool::Mosaic, "mosaic", "tool.mosaic"}, {0, Tool::Mask, "mask", "tool.mask"},
                        {1}, {0, Tool::Eraser, "eraser", "tool.eraser"}, {1}, {2}, {3}};
    float h = 10;
    for (auto& i : items) h += i.type == 1 ? 9 : 40;
    D2D1_RECT_F r = RW(rcViewport.left + 14, cyOf(rcViewport) - h / 2, 48, h);
    U.glassPanel(r, 14);
    U.add(hid("rail"), r);
    float y = r.top + 5;
    int n = 0;
    for (auto& i : items) {
        ++n;
        if (i.type == 1) { G.fill(RW(r.left + 12, y + 4, 24, 1), P.line2); y += 9; continue; }
        D2D1_RECT_F b = RW(r.left + 5, y, 38, 38);
        if (i.type == 0) U.toolButton(hid("rail", n), b, i.icon, i.tip, [this, t = i.tool] { setTool(t); }, markup.tool == i.tool);
        else if (i.type == 2) U.toolButton(hid("rail", n), b, "undo-2", "undo", [this] { markup.undo(); requestPaint(); }, false, markup.canUndo());
        else U.toolButton(hid("rail", n), b, "redo-2", "redo", [this] { markup.redo(); requestPaint(); }, false, markup.canRedo());
        y += 40;
    }
}

// ======================================================================== side panels
static void kvRow(const wchar_t* k, const std::wstring& v, float x, float& y, float w) {
    auto l = G.layout(v, 12.5f, 500, w - 104, 200, AL, true);
    DWRITE_TEXT_METRICS m{};
    if (l) l->GetMetrics(&m);
    float h = std::max(20.f, m.height) + 12;
    G.text(k, RW(x, y, 100, 32), 12.5f, P.text3, 400, AL, false, false);
    if (l) G.dc->DrawTextLayout(D2D1::Point2F(x + 104, y + 6), l.Get(), G.brush(P.text));
    y += h;
    G.line(x, y - .5f, x + w, y - .5f, P.line);
}

void App::drawInfoPanel() {
    D2D1_RECT_F r = rcSide;
    G.fill(r, P.bg);
    G.line(r.left + .5f, r.top, r.left + .5f, r.bottom, P.line);
    G.text(T("info.title"), RW(r.left + 18, r.top + 8, 200, 36), 13.5f, P.text, 600);
    U.toolButton(hid("info.close"), RW(r.right - 40, r.top + 10, 28, 28), "x", nullptr, [this] { toggleInfo(); }, false, true, nullptr, 16);
    D2D1_RECT_F body = R(r.left, r.top + 50, r.right, r.bottom - 64);
    G.dc->PushAxisAlignedClip(body, D2D1_ANTIALIAS_MODE_ALIASED);
    float x = r.left + 18, w = rw(r) - 36;
    float y = body.top - infoScroll;
    auto section = [&](const char* key) {
        y += 8;
        G.text(std::wstring(T(key)), RW(x, y, w, 18), 10.5f, P.text3, 700);
        y += 26;
    };
    if (!cur.path.empty()) {
        section("info.file");
        std::wstring ext = cur.ext;
        for (auto& c : ext) c = (wchar_t)towupper(c);
        kvRow(T("info.name"), cur.name, x, y, w);
        kvRow(T("info.type"), mode == Mode::Image && !doc.img.format.empty() ? doc.img.format : ext, x, y, w);
        kvRow(T("info.size"), fmtBytes(cur.size), x, y, w);
        kvRow(T("info.modified"), formatFileTime(cur.mtime), x, y, w);
        kvRow(T("info.folder"), dirName(cur.path), x, y, w);
    }
    if (mode == Mode::Image && doc.loaded()) {
        y += 10;
        section("info.histogram");
        drawHistogram(hist, RW(x, y, w, 96));
        y += 108;
        section("info.image");
        kvRow(T("info.dimensions"), fmtf(L"%u × %u px", doc.W(), doc.H()), x, y, w);
        kvRow(T("info.megapixels"), fmtf(L"%.1f MP", doc.W() * (double)doc.H() / 1e6), x, y, w);
        if (doc.img.bitDepth) kvRow(T("info.bitdepth"), fmtf(L"%u bit", doc.img.bitDepth), x, y, w);
        if (doc.img.dpiX > 1) kvRow(T("info.dpi"), fmtf(L"%.0f", doc.img.dpiX), x, y, w);
        if (doc.frameCount() > 1) kvRow(T("info.frames"), std::to_wstring(doc.frameCount()), x, y, w);
        kvRow(T("info.zoom"), fmtf(L"%.1f%%", view.s * 100), x, y, w);
        y += 10;
        section("info.camera");
        if (exif.rows.empty()) {
            G.text(T("info.noexif"), RW(x, y, w, 22), 12.5f, P.text3);
            y += 30;
        } else
            for (auto& row : exif.rows) kvRow(T(row.key), row.value, x, y, w);
    } else if (mode == Mode::Model && mv.hasModel()) {
        y += 10;
        section("info.model");
        auto& s = mv.stats;
        std::wstring unit = s.units == 1 ? L" mm" : L"";
        kvRow(T("info.extent"), fmtLen(s.extent[0]) + L" × " + fmtLen(s.extent[1]) + L" × " + fmtLen(s.extent[2]) + unit, x, y, w);
        kvRow(T("info.meshes"), fmtNum(s.meshes), x, y, w);
        kvRow(T("info.vertices"), fmtNum(s.vertices), x, y, w);
        kvRow(T("info.triangles"), fmtNum(s.triangles), x, y, w);
        kvRow(T("info.materials"), fmtNum(s.materials), x, y, w);
    }
    float contentH = y + infoScroll - body.top;
    G.dc->PopAxisAlignedClip();
    auto& wheelRegion = U.add(hid("info.body"), body);
    wheelRegion.wheel = [this, contentH, body](float d) {
        infoScroll = clampf(infoScroll - d / 120 * 48, 0, std::max(0.f, contentH - rh(body) + 10));
        requestPaint();
    };
    // actions
    G.line(r.left, r.bottom - 64, r.right, r.bottom - 64, P.line);
    float bw = (rw(r) - 36 - 6) / 2;
    U.textButton(hid("info.show"), RW(x, r.bottom - 50, bw, 34), T("info.showFolder"), [this] {
        if (cur.path.empty()) return;
        PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(cur.path.c_str());
        if (pidl) { SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0); ILFree(pidl); }
    }, false, "folder");
    U.textButton(hid("info.copy"), RW(x + bw + 6, r.bottom - 50, bw, 34), T("info.copyPath"), [this] {
        if (cur.path.empty() || !OpenClipboard(hwnd)) return;
        EmptyClipboard();
        size_t bytes = (cur.path.size() + 1) * 2;
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) { memcpy(GlobalLock(h), cur.path.c_str(), bytes); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
        CloseClipboard();
        toast(T("path.copied"));
    }, false, "copy");
}

void App::drawAdjustPanel() {
    D2D1_RECT_F r = rcSide;
    G.fill(r, P.bg);
    G.line(r.left + .5f, r.top, r.left + .5f, r.bottom, P.line);
    G.text(TL("edit.adjust"), RW(r.left + 18, r.top + 8, 200, 36), 13.5f, P.text, 600);
    struct Row { const char* key; float* v; };
    Row rows[] = {{"adj.exposure", &adj.exposure}, {"adj.contrast", &adj.contrast}, {"adj.highlights", &adj.highlights}, {"adj.shadows", &adj.shadows},
                  {"adj.saturation", &adj.saturation}, {"adj.temperature", &adj.temperature}, {"adj.tint", &adj.tint}};
    float x = r.left + 18, w = rw(r) - 36, y = r.top + 58;
    int n = 0;
    for (auto& row : rows) {
        ++n;
        G.text(T(row.key), RW(x, y, w, 20), 12.5f, P.text2, 500);
        G.text(fmtf(L"%+.0f", *row.v * 100), RW(x, y, w, 20), 12, std::fabs(*row.v) > 0.001f ? P.accent : P.text3, 600, AR);
        float* v = row.v;
        U.slider(hid("adj", n), RW(x + 2, y + 24, w - 4, 20), *v, -1, 1, [this, v](float nv) {
            if (std::fabs(nv) < 0.03f) nv = 0; // snap to centre
            *v = nv;
            fxActive = true;
            requestPaint();
        }, true);
        y += 58;
    }
    float bw = (w - 12) / 3;
    float by = r.bottom - 52;
    G.line(r.left, by - 14, r.right, by - 14, P.line);
    U.textButton(hid("adj.reset"), RW(x, by, bw, 34), T("reset"), [this] { adj = Adjust{}; fxActive = false; requestPaint(); });
    U.textButton(hid("adj.cancel"), RW(x + bw + 6, by, bw, 34), T("cancel"), [this] { adj = Adjust{}; fxActive = false; panel = EditPanel::None; requestPaint(); });
    U.textButton(hid("adj.apply"), RW(x + (bw + 6) * 2, by, bw, 34), T("apply"), [this] { applyAdjust(); }, true);
}

// ======================================================================== filmstrip
void App::drawFilmstrip() {
    if (rh(rcStrip) <= 0 || files.empty()) return;
    G.fill(rcStrip, P.bg);
    G.line(0, rcStrip.top + .5f, G.W(), rcStrip.top + .5f, P.line);
    const float item = 68, gap = 6, step = item + gap;
    // keep the current image (or the compared pair) centred unless the user is scrolling the strip
    if (!stripUser) stripTarget = compare && indexR >= 0 ? (index + indexR) / 2.f : (float)index;
    {
        static DWORD last = GetTickCount();
        DWORD now = GetTickCount();
        float dt = std::min(0.1f, (now - last) / 1000.f);
        last = now;
        float d = stripTarget - stripScroll;
        if (std::fabs(d) > 40) stripScroll = stripTarget - (d > 0 ? 40.f : -40.f); // long jumps: skip most of the way
        stripScroll += (stripTarget - stripScroll) * (1 - std::exp(-dt / 0.075f));
        if (std::fabs(stripTarget - stripScroll) < 0.003f) stripScroll = stripTarget;
    }
    stripHits.clear();
    const float cx = G.W() / 2, cy = cyOf(rcStrip);
    auto& wr = U.add(hid("strip"), rcStrip);
    wr.wheel = [this](float d) {
        stripUser = true;
        stripTarget = clampf(std::round(stripTarget) - d / 120 * 3, 0, (float)files.size() - 1);
        ensureTimer();
        requestPaint();
    };
    int first = std::max(0, (int)std::floor(stripScroll - cx / step) - 1), last = std::min((int)files.size() - 1, (int)std::ceil(stripScroll + cx / step) + 1);
    auto dc = G.dc.Get();
    dc->PushAxisAlignedClip(rcStrip, D2D1_ANTIALIAS_MODE_ALIASED);
    for (int k = first; k <= last; ++k) {
        auto& f = files[k];
        float x = cx + (k - stripScroll) * step - item / 2;
        const bool isL = k == index, isR = compare && k == indexR;
        bool active = isL || isR;
        uint64_t id = hid("strip.item", k);
        bool hot = U.isHot(id);
        float lift = active ? -1.f : hot ? -2.f : 0.f;
        D2D1_RECT_F r = RW(x, cy - item / 2 + lift, item, item);
        auto& g = U.add(id, r);
        g.tipText = f.name;
        stripHits.push_back({k, r});
        g.click = [this, k] {
            if (compare && files[k].kind != Kind::Image) { toast(T("compare.imageonly"), true); return; }
            guardUnsaved([this, k] { show(k); });
        };
        if (!thumbBmp.count(f.path) && !thumbFailed.count(f.path)) thumbs.request(f.path, f.kind, 900 - std::abs(k - index));
        float op = active || hot ? 1.f : .62f;
        ComPtr<ID2D1RoundedRectangleGeometry> geo;
        G.d2f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, 10, 10), &geo);
        dc->PushLayer(D2D1::LayerParameters1(r, geo.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), op), nullptr);
        G.fill(r, f.kind == Kind::Model ? P.surface : P.surface2);
        auto it = thumbBmp.find(f.path);
        if (it != thumbBmp.end()) {
            auto sz = it->second->GetSize();
            float bw = sz.width, bh = sz.height;
            float k2 = f.kind == Kind::Model ? std::min((item - 6) / bw, (item - 6) / bh) : std::max(item / bw, item / bh);
            float dw = bw * k2, dh = bh * k2;
            dc->DrawBitmap(it->second.Get(), RW(cxOf(r) - dw / 2, cyOf(r) - dh / 2, dw, dh), 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        } else {
            G.icon(f.kind == Kind::Model ? "box" : "image", cxOf(r), cyOf(r), 18, P.text3);
        }
        dc->PopLayer();
        G.stroke(r, P.line, 10, 1);
        if (f.kind == Kind::Model) {
            std::wstring e = f.ext;
            for (auto& c : e) c = (wchar_t)towupper(c);
            float bw = G.textWidth(e, 8.5f, 700) + 8;
            D2D1_RECT_F b = RW(r.left + 4, r.bottom - 17, bw, 13);
            G.fill(b, mix(P.accent, rgba(0), .12f), 3);
            G.text(e, b, 8.5f, rgba(0xFFFFFF), 700, AC);
        }
        if (markup.hasMarks(f.path) || (active && editDirty)) {
            G.circle(r.right - 8, r.top + 8, 4.5f, P.surface);
            G.circle(r.right - 8, r.top + 8, 3.5f, P.accent);
        }
        if (active) {
            D2D1_COLOR_F oc = !compare ? P.accent : isL && isR ? rgba(kCmpLeft) : isL ? rgba(kCmpLeft) : rgba(kCmpRight);
            G.stroke(inset(r, -3), P.bg, 13, 2);
            G.stroke(inset(r, -4.5f), oc, 14.5f, 2.2f);
            if (compare) {
                D2D1_RECT_F tag = RW(r.left + 4, r.top + 4, 14, 14);
                G.fill(tag, oc, 4);
                G.text(isL ? L"L" : L"R", tag, 8.5f, rgba(0xFFFFFF), 700, AC);
            }
        }
    }
    dc->PopAxisAlignedClip();
    // edge fades
    D2D1_GRADIENT_STOP st[2] = {{0, P.bg}, {1, withA(P.bg, 0)}};
    ComPtr<ID2D1GradientStopCollection> sc;
    dc->CreateGradientStopCollection(st, 2, &sc);
    ComPtr<ID2D1LinearGradientBrush> lb;
    dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {48, 0}), sc.Get(), &lb);
    dc->FillRectangle(R(0, rcStrip.top + 1, 48, rcStrip.bottom), lb.Get());
    lb.Reset();
    dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({G.W(), 0}, {G.W() - 48, 0}), sc.Get(), &lb);
    dc->FillRectangle(R(G.W() - 48, rcStrip.top + 1, G.W(), rcStrip.bottom), lb.Get());
}

// ======================================================================== chips / overlays
std::wstring App::statusText() {
    if (mode == Mode::Image && doc.loaded())
        return fmtf(L"%u × %u", doc.W(), doc.H()) + L"  ·  " + fmtBytes(cur.size) + L"  ·  " + fmtf(L"%.1f%%", view.s * 100);
    if (mode == Mode::Model && mv.hasModel()) {
        auto& s = mv.stats;
        return fmtLen(s.extent[0]) + L" × " + fmtLen(s.extent[1]) + L" × " + fmtLen(s.extent[2]) + (s.units == 1 ? L" mm" : L"") + L"  ·  " +
               fmtNum(s.triangles ? s.triangles : s.vertices) + (s.triangles ? L" △" : L" pts") + L"  ·  " + fmtBytes(cur.size);
    }
    return L"";
}

void App::drawStatus() {
    if (mode != Mode::Model) return; // image view: zoom lives in the toolbar, size in the info panel
    std::wstring s = statusText();
    if (s.empty() || panel == EditPanel::Crop) return;
    float w = G.textWidth(s, 11.5f, 600) + 22;
    float x = rcViewport.left + (annot ? 76 : 14);
    D2D1_RECT_F r = RW(x, rcViewport.bottom - 42, w, 28);
    G.fill(r, P.glass, 8);
    G.stroke(r, P.glassLine, 8, 1);
    G.text(s, R(r.left + 11, r.top, r.right, r.bottom), 11.5f, P.text2, 600);
}

void App::drawNav() {
    if (!mouseInViewport || annot || panel == EditPanel::Crop || files.size() < 2) return;
    auto arrow = [&](bool next) {
        bool ok = next ? index < (int)files.size() - 1 : index > 0;
        if (!ok) return;
        D2D1_RECT_F r = next ? RW(rcViewport.right - 58, cyOf(rcViewport) - 32, 44, 64) : RW(rcViewport.left + 14, cyOf(rcViewport) - 32, 44, 64);
        uint64_t id = hid(next ? "nav.next" : "nav.prev");
        auto& g = U.add(id, r);
        g.tip = next ? "next" : "prev";
        g.click = [this, next] { guardUnsaved([this, next] { go(next ? 1 : -1); }); };
        bool hot = U.isHot(id);
        G.shadow(r, 12, .6f);
        G.fill(r, hot ? P.surface : P.glass, 12);
        G.stroke(r, P.glassLine, 12, 1);
        G.icon(next ? "chevron-right" : "chevron-left", cxOf(r), cyOf(r), 22, P.text);
    };
    arrow(false);
    arrow(true);
}

void App::drawLoading() {
    if (loading && !compare && GetTickCount() - loadStart > 150) {
        float cx = cxOf(rcViewport), cy = cyOf(rcViewport);
        float t = (GetTickCount() % 900) / 900.f * 6.2831853f;
        G.circle(cx, cy - 18, 13, P.line2, false, 2.5f);
        ComPtr<ID2D1PathGeometry> g;
        G.d2f->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> s;
        g->Open(&s);
        s->BeginFigure(D2D1::Point2F(cx + 13 * std::cos(t), cy - 18 + 13 * std::sin(t)), D2D1_FIGURE_BEGIN_HOLLOW);
        s->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + 13 * std::cos(t + 1.6f), cy - 18 + 13 * std::sin(t + 1.6f)), D2D1::SizeF(13, 13), 0,
                                   D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        s->EndFigure(D2D1_FIGURE_END_OPEN);
        s->Close();
        auto ss = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND);
        ComPtr<ID2D1StrokeStyle> st;
        G.d2f->CreateStrokeStyle(ss, nullptr, 0, &st);
        G.dc->DrawGeometry(g.Get(), G.brush(P.accent), 2.5f, st.Get());
        G.text(loadingLabel, R(rcViewport.left, cy + 6, rcViewport.right, cy + 30), 12.5f, P.text2, 500, AC);
        D2D1_RECT_F bar = RW(cx - 90, cy + 36, 180, 3);
        G.fill(bar, P.line2, 1.5f);
        float p = (GetTickCount() % 1100) / 1100.f;
        float bx = bar.left - 60 + (rw(bar) + 60) * p;
        G.dc->PushAxisAlignedClip(bar, D2D1_ANTIALIAS_MODE_ALIASED);
        G.fill(RW(bx, bar.top, 60, 3), P.accent, 1.5f);
        G.dc->PopAxisAlignedClip();
    }
    if (!errorKey.empty() && mode != Mode::Empty && !compare) {
        float cx = cxOf(rcViewport), cy = cyOf(rcViewport);
        G.icon("image-off", cx, cy - 34, 30, P.text3);
        G.text(T(errorKey.c_str()), R(rcViewport.left, cy - 8, rcViewport.right, cy + 16), 14, P.text, 600, AC);
        G.text(cur.name, R(rcViewport.left + 40, cy + 18, rcViewport.right - 40, cy + 38), 12, P.text3, 400, AC);
    }
}

void App::drawToasts() {
    DWORD now = GetTickCount();
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const Toast& t) { return now - t.t0 > 2600; }), toasts.end());
    float y = (rh(rcStrip) > 0 ? rcStrip.top : G.H()) - 22;
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
        float age = (now - it->t0) / 1000.f;
        float a = std::min(1.f, age / 0.18f) * (age > 2.3f ? std::max(0.f, 1 - (age - 2.3f) / 0.3f) : 1.f);
        float w = G.textWidth(it->text, 12.5f, 500) + 48;
        float h = 38;
        D2D1_RECT_F r = RW(G.W() / 2 - w / 2, y - h + (1 - a) * 8, w, h);
        G.dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), a), nullptr);
        G.shadow(r, 10, 1.2f);
        G.fill(r, P.tooltip, 10);
        G.icon(it->error ? "x" : "check", r.left + 20, cyOf(r), 15, it->error ? rgba(0xFF7B7F) : rgba(0x7CE38B));
        G.text(it->text, R(r.left + 34, r.top, r.right, r.bottom), 12.5f, P.tooltipText, 500);
        G.dc->PopLayer();
        y -= h + 8;
    }
}

void App::drawDrop() {
    if (!dropHover) return;
    G.fill(R(0, 0, G.W(), G.H()), withA(P.accent, .10f));
    D2D1_RECT_F c = RW(G.W() / 2 - 190, G.H() / 2 - 70, 380, 140);
    G.shadow(c, 20, 1.5f);
    G.fill(c, P.glass, 20);
    auto ss = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10, D2D1_DASH_STYLE_DASH);
    ComPtr<ID2D1StrokeStyle> st;
    G.d2f->CreateStrokeStyle(ss, nullptr, 0, &st);
    G.dc->DrawRoundedRectangle(D2D1::RoundedRect(inset(c, 1), 20, 20), G.brush(P.accent), 2, st.Get());
    G.icon("image", cxOf(c), c.top + 48, 28, P.accent);
    G.text(T("open.hint"), R(c.left, c.top + 72, c.right, c.top + 104), 14, P.text, 600, AC);
}

// ======================================================================== modals
void App::drawModal() {
    G.fill(R(0, 0, G.W(), G.H()), rgba(0x08090C, P.dark ? .55f : .38f));
    auto& bg = U.add(hid("modal.bg"), R(0, 0, G.W(), G.H()));
    if (modal.kind == 1 || modal.kind == 2) bg.click = [this] { closeModal(); requestPaint(); };
    float W = modal.kind == 4 ? 420.f : 470.f;
    float H = modal.kind == 1 ? 470.f : modal.kind == 2 ? 470.f : modal.kind == 3 ? 300.f : 200.f;
    if (modal.kind == 4) {
        auto l = G.layout(modal.message, 13, 400, W - 40, 400, AL, true);
        DWRITE_TEXT_METRICS m{};
        if (l) l->GetMetrics(&m);
        H = 128 + m.height;
    }
    D2D1_RECT_F c = RW(std::round(G.W() / 2 - W / 2), std::round(G.H() / 2 - H / 2), W, H);
    G.shadow(c, 16, 2);
    G.fill(c, P.surface, 16);
    G.stroke(c, P.line, 16, 1);
    U.add(hid("modal.card"), c);
    const wchar_t* title = modal.kind == 1 ? T("settings") : modal.kind == 2 ? T("about") : modal.kind == 3 ? TL("edit.resize") : modal.title.c_str();
    G.text(title, RW(c.left + 22, c.top + 10, W - 80, 40), 15, P.text, 600);
    if (modal.kind != 4)
        U.toolButton(hid("modal.x"), RW(c.right - 44, c.top + 12, 30, 30), "x", nullptr, [this] { closeModal(); requestPaint(); }, false, true, nullptr, 16);
    float x = c.left + 22, w = W - 44, y = c.top + 56;

    if (modal.kind == 1) {
        auto group = [&](const char* k) { G.text(T(k), RW(x, y, w, 18), 10.5f, P.text3, 700); y += 24; };
        auto rowSeg = [&](const char* k, std::vector<std::wstring> items, std::vector<const char*> icons, int sel, std::function<void(int)> on, int n) {
            G.text(T(k), RW(x, y, w, 40), 13, P.text, 500);
            float sw = U.segmentedWidth(items, icons);
            U.segmented(hid("set.seg", n), RW(x + w - sw, y + 4, sw, 32), items, sel, on, icons);
            y += 42;
            G.line(x, y - .5f, x + w, y - .5f, P.line);
        };
        auto rowSwitch = [&](const char* k, bool on, Action a, int n) {
            G.text(T(k), RW(x, y, w, 40), 13, P.text, 500);
            U.toggleSwitch(hid("set.sw", n), x + w - 36, y + 9.5f, on, a);
            y += 42;
            G.line(x, y - .5f, x + w, y - .5f, P.line);
        };
        group("set.appearance");
        rowSeg("theme", {T("theme.system"), T("theme.light"), T("theme.dark")}, {"monitor", "sun", "moon"}, S.theme,
               [this](int i) { S.theme = i; S.save(); applyTheme(); }, 1);
        rowSeg("set.accent", {T("set.accent.system"), T("set.accent.brand")}, {}, S.accent, [this](int i) { S.accent = i; S.save(); applyTheme(); }, 2);
        rowSeg("language", {T("lang.system"), L"한국어", L"English"}, {}, S.language, [this](int i) { S.language = i; S.save(); applyLanguage(); }, 3);
        y += 12;
        group("set.viewer");
        rowSeg("set.wheel", {T("set.wheel.zoom"), T("set.wheel.navigate")}, {}, S.wheel, [this](int i) { S.wheel = i; S.save(); requestPaint(); }, 4);
        rowSeg("set.imagebg", {T("set.imagebg.plain"), T("set.imagebg.checker")}, {}, S.imageBg, [this](int i) { S.imageBg = i; S.save(); requestPaint(); }, 5);
        rowSwitch("set.filmstrip", S.filmstrip, [this] { S.filmstrip = !S.filmstrip; S.save(); requestPaint(); }, 6);
        rowSwitch("set.grid", S.grid, [this] { S.grid = !S.grid; mv.grid = S.grid; mv.invalidate(); S.save(); requestPaint(); }, 7);
        float bw = G.textWidth(T("set.defaults"), 13, 600) + 52;
        U.textButton(hid("set.defaults"), RW(x, c.bottom - 54, bw, 34), T("set.defaults"), [] {
            ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=DeepImage%20Viewer", nullptr, nullptr, SW_SHOWNORMAL);
        }, false, "external-link");
    } else if (modal.kind == 2) {
        float cx = cxOf(c);
        if (markBmp) G.dc->DrawBitmap(markBmp.Get(), RW(cx - 44, y, 88, 88), 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        y += 100;
        G.text(L"DeepImage Viewer", RW(c.left, y, W, 28), 20, P.text, 600, AC);
        y += 30;
        G.text(T("tagline"), RW(c.left, y, W, 20), 13, P.text2, 400, AC);
        y += 34;
        D2D1_RECT_F card = RW(x, y, w, 132);
        G.fill(card, P.surface2, 12);
        G.stroke(card, P.line, 12, 1);
        auto row = [&](const char* k, const std::wstring& v, float yy, bool link) {
            G.text(T(k), RW(x + 16, yy, 80, 44), 12.5f, P.text3);
            if (link) {
                float lw = G.textWidth(v, 12.5f, 600);
                D2D1_RECT_F lr = RW(x + 100, yy + 10, lw, 24);
                auto& g = U.add(hid("about.mail"), lr);
                g.click = [] { ShellExecuteW(nullptr, L"open", L"mailto:dmind114@gmail.com", nullptr, nullptr, SW_SHOWNORMAL); };
                G.text(v, RW(x + 100, yy, lw + 4, 44), 12.5f, P.accent, 600);
                if (U.isHot(g.id)) G.line(x + 100, yy + 30, x + 100 + lw, yy + 30, P.accent, 1);
            } else G.text(v, RW(x + 100, yy, w - 110, 44), 12.5f, P.text, 600);
        };
        row("about.version", kVersion, y, false);
        G.line(x, y + 44, x + w, y + 44, P.line);
        row("about.madeby", L"DMIND", y + 44, false);
        G.line(x, y + 88, x + w, y + 88, P.line);
        row("about.contact", L"dmind114@gmail.com", y + 88, true);
        y += 146;
        G.text(std::wstring(T("about.credits")) + L" — " + T("about.credits.text"), RW(x, y, w, 70), 11.5f, P.text3, 400, AL, true, false);
    } else if (modal.kind == 3) {
        auto field = [&](int idx, const char* label, std::wstring& val, float fx) {
            G.text(T(label), RW(fx, y, 160, 18), 12, P.text2, 500);
            D2D1_RECT_F f = RW(fx, y + 22, 160, 40);
            auto& g = U.add(hid("field", idx), f);
            g.click = [this, idx] { focusField = idx; requestPaint(); };
            g.cursor = LoadCursorW(nullptr, IDC_IBEAM);
            bool focused = focusField == idx;
            G.fill(f, P.surface2, 9);
            G.stroke(f, focused ? P.accent : P.line2, 9, focused ? 2.f : 1.f);
            G.text(val, R(f.left + 12, f.top, f.right - 34, f.bottom), 15, P.text, 600);
            if (focused) {
                float tw = G.textWidth(val, 15, 600);
                G.line(f.left + 13 + tw, f.top + 11, f.left + 13 + tw, f.bottom - 11, P.accent, 1.5f);
            }
            G.text(L"px", R(f.right - 30, f.top, f.right - 8, f.bottom), 12, P.text3, 500, AR);
        };
        field(0, "edit.width", fieldW, x);
        field(1, "edit.height", fieldH, x + w - 160);
        // ratio lock between the fields
        D2D1_RECT_F lk = RW(x + w / 2 - 17, y + 25, 34, 34);
        U.toolButton(hid("resize.lock"), lk, "spline", "edit.keep", [this] { keepRatio = !keepRatio; requestPaint(); }, keepRatio, true, nullptr, 17);
        y += 78;
        G.text(T("edit.percent"), RW(x, y, 100, 30), 12, P.text2, 500);
        const int pcts[] = {25, 50, 75, 150, 200};
        float bx = x + 70;
        for (int i = 0; i < 5; ++i) {
            std::wstring l = std::to_wstring(pcts[i]) + L"%";
            float bw = G.textWidth(l, 12, 600) + 22;
            int pct = pcts[i];
            U.textButton(hid("resize.pct", i), RW(bx, y, bw, 30), l, [this, pct] {
                fieldW = std::to_wstring(std::max(1L, std::lround(doc.W() * pct / 100.0)));
                fieldH = std::to_wstring(std::max(1L, std::lround(doc.H() * pct / 100.0)));
                requestPaint();
            });
            bx += bw + 6;
        }
        y += 44;
        G.text(fmtf(L"%u × %u px  →", doc.W(), doc.H()), RW(x, y, w, 20), 11.5f, P.text3);
        float b1 = G.textWidth(T("cancel"), 13, 600) + 34, b2 = G.textWidth(T("apply"), 13, 600) + 34;
        U.textButton(hid("resize.cancel"), RW(c.right - 22 - b2 - 8 - b1, c.bottom - 56, b1, 36), T("cancel"), [this] { closeModal(); requestPaint(); });
        U.textButton(hid("resize.apply"), RW(c.right - 22 - b2, c.bottom - 56, b2, 36), T("apply"), [this] { applyResize(); }, true);
    } else if (modal.kind == 4) {
        G.text(modal.message, RW(x, y, w, H - 120), 13, P.text2, 400, AL, true, false);
        float bx = c.right - 22;
        for (int i = (int)modal.buttons.size() - 1; i >= 0; --i) {
            auto& b = modal.buttons[i];
            float bw = G.textWidth(b.label, 13, 600) + 34;
            bx -= bw;
            Action a = b.action;
            U.textButton(hid("confirm.btn", i), RW(bx, c.bottom - 56, bw, 36), b.label, [this, a] {
                closeModal();
                if (a) a();
                requestPaint();
            }, b.primary, nullptr, b.danger);
            bx -= 8;
        }
    }
}

} // namespace dv
