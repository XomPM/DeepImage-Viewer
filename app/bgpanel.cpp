// DeepImage Viewer — AI background removal: worker jobs, the refinement panel and its tools
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "app.h"
#include "bgremove.h"
#include "i18n.h"
#include <thread>

namespace dv {

namespace {
float cxOf(const D2D1_RECT_F& r) { return (r.left + r.right) / 2; }
float cyOf(const D2D1_RECT_F& r) { return (r.top + r.bottom) / 2; }
const uint32_t kBgColors[] = {0xFFFFFF, 0x000000, 0xF2F2F7, 0x8E8E93, 0xFF3B30, 0x34C759, 0x0A84FF, 0xFFCC00};
} // namespace

// kind 0: whole picture (opens the panel); kind 1: AI again inside `roi` (outside becomes background)
struct App::BgJob {
    uint64_t seq = 0;
    int kind = 0;
    std::string err;
    std::wstring device;
    DWORD ms = 0;
    std::shared_ptr<BgSource> src;
    std::shared_ptr<std::vector<uint8_t>> mask;
    std::unique_ptr<BgStage> stage;
};

void App::removeBackground() {
    if (bgBusy || mode != Mode::Image || !doc.loaded() || doc.img.frames.empty()) return;
    if (panel == EditPanel::Bg) return;
    if (!bg::runtimeAvailable()) { toast(T("bg.noruntime"), true); return; }
    if (bg::modelReady()) { startBgJob(0, false); return; }
    // first use: the model is a one-off ~224 MB download
    confirm(T("bg.dl.title"), T("bg.dl.msg"),
            {{T("cancel"), [this] { closeModal(); requestPaint(); }},
             {T("bg.dl.go"), [this] { closeModal(); startBgJob(0, true); }, true}});
}

void App::startBgJob(int kind, bool download, WICRect roi) {
    if (bgBusy || !doc.loaded() || doc.img.frames.empty()) return;
    bgBusy = true;
    bgDownloading = download;
    bgProgress = 0;
    const uint64_t s = ++bgSeq;
    bgCancel = std::make_shared<std::atomic<bool>>(false);
    auto cancel = bgCancel;
    ComPtr<IWICBitmap> src = doc.img.frames[0];
    HWND hw = hwnd;
    std::thread([s, kind, src, download, cancel, hw, roi] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto* job = new BgJob;
        job->seq = s;
        job->kind = kind;
        DWORD tRun = GetTickCount();
        bool ok = true;
        if (download) {
            DWORD last = 0;
            ok = bg::downloadModel([&](float p) {
                    DWORD now = GetTickCount();
                    if (now - last > 100) { last = now; PostMessageW(hw, WM_APP_BG_PROGRESS, (WPARAM)s, (LPARAM)(p * 1000)); }
                }, [cancel] { return cancel->load(); }, job->err);
            PostMessageW(hw, WM_APP_BG_PROGRESS, (WPARAM)s, (LPARAM)-1); // download over: now the cut-out
            tRun = GetTickCount();
        }
        UINT W = 0, H = 0;
        src->GetSize(&W, &H);
        std::vector<uint8_t> m;
        if (ok && !cancel->load()) ok = bg::predictMask(src.Get(), kind == 1 ? &roi : nullptr, m, job->err, job->device);
        else ok = false;
        if (ok && kind == 0) {
            job->src = std::make_shared<BgSource>();
            job->src->W = (int)W;
            job->src->H = (int)H;
            job->src->px.resize((size_t)W * H * 4);
            if (FAILED(src->CopyPixels(nullptr, W * 4, (UINT)job->src->px.size(), job->src->px.data()))) { ok = false; job->err = "bg.failed"; }
            if (ok) {
                job->mask = std::make_shared<std::vector<uint8_t>>(std::move(m));
                job->stage = bgComputeStage(*job->src, *job->mask, BgParams{});
            }
        } else if (ok && kind == 1) {
            // the new AI mask: the area's result, background everywhere else
            job->mask = std::make_shared<std::vector<uint8_t>>((size_t)W * H, 0);
            for (int y = 0; y < roi.Height; ++y)
                std::memcpy(&(*job->mask)[(size_t)(roi.Y + y) * W + roi.X], &m[(size_t)y * roi.Width], roi.Width);
        }
        if (!ok) { job->src.reset(); job->mask.reset(); job->stage.reset(); }
        job->ms = GetTickCount() - tRun;
        if (!PostMessageW(hw, WM_APP_BG, 0, (LPARAM)job)) delete job;
        CoUninitialize();
    }).detach();
    ensureTimer();
    requestPaint();
}

void App::onBgJob(BgJob* raw) {
    std::unique_ptr<BgJob> job(raw);
    if (job->seq != bgSeq) return;
    bgBusy = bgDownloading = false;
    if (!job->mask) {
        if (job->err != "bg.cancelled") toast(T(job->err.empty() ? "bg.failed" : job->err.c_str()), true);
        requestPaint();
        return;
    }
    if (!doc.loaded()) { requestPaint(); return; }
    if (job->kind == 0) {
        if (!editMode) toggleEdit(1);
        if (panel == EditPanel::Adjust) { adj = Adjust{}; fxActive = false; }
        bge = BgEdit{};
        ++bgSession;
        bge.src = job->src;
        bge.ai = job->mask;
        bge.stage = std::move(job->stage);
        bgStageReq = bge.p;
        bgStageDirty = false;
        bgTool = 0;
        panel = EditPanel::Bg;
        bge.composeAll();
        bgShowAll();
        toast(fmtf(T("bg.ready"), job->device.c_str(), job->ms / 1000.0));
    } else if (panel == EditPanel::Bg) {
        BgEdit::Undo u;
        u.kind = 1;
        u.ai = bge.ai;
        u.objects = bge.p.objects;
        bge.pushUndo(std::move(u));
        bge.ai = job->mask;
        bge.p.objects.clear(); // old object numbers mean nothing for the new mask
        startBgStage();
        bgTool = 0;
    }
    requestPaint();
}

// ------------------------------------------------------------------ stage (worker) and preview
void App::startBgStage() {
    if (!bge.src || !bge.ai) return;
    if (bgStageBusy) { bgStageDirty = true; return; }
    bgStageBusy = true;
    bgStageDirty = false;
    bgStageReq = bge.p;
    auto src = bge.src;
    auto ai = bge.ai;
    BgParams p = bge.p;
    const uint64_t gen = bgSession;
    HWND hw = hwnd;
    std::thread([src, ai, p, gen, hw] {
        BgStage* st = bgComputeStage(*src, *ai, p).release();
        if (!PostMessageW(hw, WM_APP_BG_STAGE, (WPARAM)gen, (LPARAM)st)) delete st;
    }).detach();
    ensureTimer();
}

void App::onBgStage(uint64_t gen, BgStage* raw) {
    std::unique_ptr<BgStage> st(raw);
    bgStageBusy = false;
    if (gen == bgSession && panel == EditPanel::Bg && bge.src) {
        bge.stage = std::move(st);
        bge.composeAll();
        bgShowAll();
    }
    if (bgStageDirty && panel == EditPanel::Bg) startBgStage();
    requestPaint();
}

void App::bgParamsChanged() {
    if (!bge.src) return;
    if (!bge.p.stageEquals(bgStageReq)) startBgStage(); // sets the dirty flag when one is already running
    else {
        bge.composeAll();
        bgShowAll();
    }
    requestPaint();
}

void App::bgShow(const BgEdit::Rect& r) {
    if (r.empty() || bge.out.empty()) return;
    doc.updatePixels(bge.out.data(), r.x0, r.y0, r.x1, r.y1);
    requestPaint();
}

void App::closeBg(bool restore) {
    if (restore && doc.loaded()) doc.upload(G.dc.Get());
    bge = BgEdit{};
    ++bgSession;
    bgStageDirty = false;
    if (panel == EditPanel::Bg) panel = EditPanel::None;
    if (drag == DragKind::BgBrush || drag == DragKind::BgArea) drag = DragKind::None;
    requestPaint();
}

void App::applyBg() {
    if (!bge.src || !bge.stage) { closeBg(true); return; }
    bge.showMask = false;
    bge.composeAll();
    ComPtr<IWICBitmap> out;
    if (FAILED(G.wic->CreateBitmapFromMemory(bge.W(), bge.H(), GUID_WICPixelFormat32bppPBGRA, bge.W() * 4, (UINT)bge.out.size(), bge.out.data(), &out))) {
        toast(T("bg.failed"), true);
        return;
    }
    const bool transparent = bge.p.bgMode == 0;
    closeBg(false);
    pushEdit();
    doc.replace(out);
    if (transparent) doc.img.container = GUID_ContainerFormatPng; // keep the transparency when saving
    bgCutout = transparent;
    afterPixelEdit();
    editDirty = true;
    toast(T(transparent ? "bg.applied.png" : "bg.applied"));
}

void App::bgUndo() {
    if (bge.undo.empty()) return;
    BgEdit::Undo u = std::move(bge.undo.back());
    bge.undo.pop_back();
    if (u.kind == 0) {
        const int W = bge.W(), w = u.r.x1 - u.r.x0;
        if (bge.keep.empty() || bge.erase.empty()) return;
        for (int y = u.r.y0; y < u.r.y1; ++y) {
            std::memcpy(&bge.keep[(size_t)y * W + u.r.x0], &u.keep[(size_t)(y - u.r.y0) * w], w);
            std::memcpy(&bge.erase[(size_t)y * W + u.r.x0], &u.erase[(size_t)(y - u.r.y0) * w], w);
        }
        bge.compose(u.r.x0, u.r.y0, u.r.x1, u.r.y1);
        bgShow(u.r);
    } else {
        if (u.kind == 1) bge.ai = u.ai;
        bge.p.objects = u.objects;
        startBgStage();
    }
    requestPaint();
}

void App::bgPickObject(float x, float y) {
    auto p = view.toImage(x, y);
    const int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y);
    if (ix < 0 || iy < 0 || ix >= bge.W() || iy >= bge.H()) return;
    const int reach = (int)(10 / view.s) + 2;
    const uint32_t l = bge.labelAt(ix, iy, reach);
    if (!l) { toast(T("bg.obj.none"), true); return; }
    BgEdit::Undo u;
    u.kind = 2;
    u.objects = bge.p.objects;
    auto& o = bge.p.objects;
    const size_t before = o.size();
    o.erase(std::remove_if(o.begin(), o.end(), [&](const std::pair<int, int>& q) { return bge.labelAt(q.first, q.second, 12) == l; }), o.end());
    if (o.size() == before) o.push_back({ix, iy});
    bge.pushUndo(std::move(u));
    startBgStage();
    requestPaint();
}

// ------------------------------------------------------------------ drawing
void App::drawBgOverlay() {
    if (panel != EditPanel::Bg) return;
    // chosen objects: numbered dots
    if (bgTool == 3 || !bge.p.objects.empty()) {
        int n = 0;
        for (auto [x, y] : bge.p.objects) {
            ++n;
            auto s = view.toScreen(x + .5f, y + .5f);
            G.circle(s.x, s.y + 1, 11, rgba(0, .3f));
            G.circle(s.x, s.y, 10, P.accent);
            G.circle(s.x, s.y, 10, rgba(0xFFFFFF), false, 1.5f);
            G.text(std::to_wstring(n), RW(s.x - 10, s.y - 10, 20, 20), 11, rgba(0xFFFFFF), 700, AC);
        }
    }
    if (drag == DragKind::BgArea) {
        auto a = view.toScreen(bgAreaA.x, bgAreaA.y), b = view.toScreen(bgAreaB.x, bgAreaB.y);
        D2D1_RECT_F r = R(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
        G.fill(r, withA(P.accent, .12f));
        G.stroke(r, rgba(0, .5f), 0, 3);
        G.stroke(r, rgba(0xFFFFFF), 0, 1.5f);
    }
    if ((bgTool == 1 || bgTool == 2) && mouseInViewport && !U.overWidget(U.mx, U.my)) {
        const float r = bgBrush / 2;
        G.circle(U.mx, U.my, r + 1, rgba(0, .55f), false, 1.5f);
        G.circle(U.mx, U.my, r, bgTool == 1 ? rgba(0x34C759) : rgba(0xFF453A), false, 1.75f);
        G.line(U.mx - 4, U.my, U.mx + 4, U.my, rgba(0xFFFFFF, .9f), 1.25f);
        if (bgTool == 1) G.line(U.mx, U.my - 4, U.mx, U.my + 4, rgba(0xFFFFFF, .9f), 1.25f);
    }
}

void App::drawBgPanel() {
    D2D1_RECT_F r = rcSide;
    G.fill(r, P.bg);
    G.line(r.left + .5f, r.top, r.left + .5f, r.bottom, P.line);
    U.add(hid("bg.panel"), r);
    const float x = r.left + 18, w = rw(r) - 36;
    G.text(T("bg.title"), RW(x, r.top + 8, 200, 36), 13.5f, P.text, 600);
    U.toolButton(hid("bg.undo"), RW(r.right - 50, r.top + 8, 34, 34), "undo-2", "bg.undo", [this] { bgUndo(); }, false, !bge.undo.empty());
    if (bgStageBusy) { // small activity dot next to the title while the mask is being recomputed
        const float t = (GetTickCount() % 900) / 900.f;
        G.circle(x + G.textWidth(T("bg.title"), 13.5f, 600) + 12, r.top + 26, 3.5f, withA(P.accent, .35f + .65f * std::fabs(std::sin(t * 3.14159f))));
    }
    float y = r.top + 52;
    // tools
    struct ToolDef { const char* icon; const char* key; };
    const ToolDef tools[] = {{"hand", "bg.tool.pan"}, {"pen-line", "bg.tool.keep"}, {"eraser", "bg.tool.erase"}, {"mouse-pointer", "bg.tool.obj"}, {"square-dashed", "bg.tool.area"}};
    const float cw = w / 5;
    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F c = RW(x + i * cw + 1, y, cw - 2, 54);
        uint64_t id = hid("bg.tool", i);
        auto& g = U.add(id, c);
        g.click = [this, i] { bgTool = i; requestPaint(); };
        g.tipText = std::wstring(T(tools[i].key)) + L" (" + std::to_wstring(i + 1) + L")";
        const bool on = bgTool == i;
        if (on) G.fill(c, withA(P.accent, P.dark ? .22f : .14f), 9);
        else if (U.isHot(id)) G.fill(c, P.hover, 9);
        D2D1_COLOR_F fg = on ? P.accent : P.text2;
        G.icon(tools[i].icon, cxOf(c), c.top + 19, 18, fg);
        G.text(T(tools[i].key), R(c.left, c.top + 32, c.right, c.bottom - 2), 11, on ? P.accent : P.text2, 600, AC);
    }
    y += 60;
    static const char* hints[] = {"bg.hint.pan", "bg.hint.keep", "bg.hint.erase", "bg.hint.obj", "bg.hint.area"};
    G.text(T(hints[bgTool]), RW(x, y, w, 34), 11.5f, P.text3, 500, AL, true, false);
    y += 38;
    if (bgTool == 1 || bgTool == 2) {
        G.text(T("bg.brush"), RW(x, y, w, 20), 12.5f, P.text2, 500);
        G.text(fmtf(L"%.0f", bgBrush), RW(x, y, w, 20), 12, P.text3, 600, AR);
        U.slider(hid("bg.brushsz"), RW(x + 2, y + 24, w - 4, 20), bgBrush, 4, 300, [this](float v) { bgBrush = std::round(v); requestPaint(); });
        y += 52;
    } else if (bgTool == 3 && !bge.p.objects.empty()) {
        U.textButton(hid("bg.objclear"), RW(x, y, w, 32), T("bg.obj.clear"), [this] {
            BgEdit::Undo u;
            u.kind = 2;
            u.objects = bge.p.objects;
            bge.pushUndo(std::move(u));
            bge.p.objects.clear();
            startBgStage();
            requestPaint();
        });
        y += 44;
    }
    G.line(r.left, y + 2, r.right, y + 2, P.line);
    y += 12;
    auto toggleRow = [&](const char* key, bool on, Action a, int n) {
        G.text(T(key), RW(x, y, w - 44, 36), 12.5f, P.text2, 500);
        U.toggleSwitch(hid("bg.sw", n), x + w - 36, y + 7.5f, on, std::move(a));
        y += 38;
    };
    toggleRow("bg.showmask", bge.showMask, [this] { bge.showMask = !bge.showMask; bge.composeAll(); bgShowAll(); }, 0);
    struct SRow { const char* key; int* v; int mn, mx; bool centered; const wchar_t* fmt; };
    SRow rows[] = {{"bg.strength", &bge.p.strength, -100, 100, true, L"%+d"},
                   {"bg.edge", &bge.p.edge, -30, 30, true, L"%+d px"},
                   {"bg.feather", &bge.p.feather, 0, 30, false, L"%d px"}};
    int n = 0;
    for (auto& row : rows) {
        ++n;
        G.text(T(row.key), RW(x, y, w, 20), 12.5f, P.text2, 500);
        G.text(fmtf(row.fmt, *row.v), RW(x, y, w, 20), 12, *row.v ? P.accent : P.text3, 600, AR);
        int* v = row.v;
        const int mn = row.mn, mx = row.mx;
        U.slider(hid("bg.sl", n), RW(x + 2, y + 24, w - 4, 20), (float)*v, (float)mn, (float)mx, [this, v, mn, mx](float nv) {
            int iv = (int)std::lround(nv);
            if (mn < 0 && std::abs(iv) <= (mx - mn) / 60) iv = 0; // snap to centre
            if (iv == *v) return;
            *v = iv;
            bgParamsChanged();
        }, row.centered);
        y += 52;
    }
    toggleRow("bg.decontam", bge.p.decontam, [this] { bge.p.decontam = !bge.p.decontam; bgParamsChanged(); }, 1);
    G.line(r.left, y + 2, r.right, y + 2, P.line);
    y += 12;
    G.text(T("bg.bg"), RW(x, y, 80, 32), 12.5f, P.text2, 500);
    std::vector<std::wstring> modes = {T("bg.bg.none"), T("bg.bg.color"), T("bg.bg.blur")};
    float sw = U.segmentedWidth(modes);
    U.segmented(hid("bg.mode"), RW(x + w - sw, y, sw, 32), modes, bge.p.bgMode, [this](int i) {
        if (bge.p.bgMode == i) return;
        bge.p.bgMode = i;
        bgParamsChanged();
    });
    y += 42;
    if (bge.p.bgMode == 1) {
        int i = 0;
        for (uint32_t c : kBgColors) {
            U.swatch(hid("bg.col", i), x + 13 + i * 30, y + 14, rgba(c), bge.p.bgColor == c, [this, c] { bge.p.bgColor = c; bgParamsChanged(); });
            ++i;
        }
        y += 36;
    } else if (bge.p.bgMode == 2) {
        G.text(T("bg.blur"), RW(x, y, w, 20), 12.5f, P.text2, 500);
        G.text(fmtf(L"%d", bge.p.blur), RW(x, y, w, 20), 12, P.text3, 600, AR);
        U.slider(hid("bg.blur"), RW(x + 2, y + 24, w - 4, 20), (float)bge.p.blur, 0, 100, [this](float nv) {
            int iv = (int)std::lround(nv);
            if (iv == bge.p.blur) return;
            bge.p.blur = iv;
            bgParamsChanged();
        });
        y += 52;
    }
    // bottom buttons
    float bw = (w - 12) / 3;
    float by = r.bottom - 52;
    G.line(r.left, by - 14, r.right, by - 14, P.line);
    U.textButton(hid("bg.reset"), RW(x, by, bw, 34), T("reset"), [this] {
        const bool mask = bge.showMask;
        bge.p = BgParams{};
        bge.showMask = mask;
        bge.keep.clear();
        bge.erase.clear();
        bge.undo.clear();
        startBgStage();
        requestPaint();
    });
    U.textButton(hid("bg.cancel2"), RW(x + bw + 6, by, bw, 34), T("cancel"), [this] { closeBg(true); });
    U.textButton(hid("bg.apply"), RW(x + (bw + 6) * 2, by, bw, 34), T("apply"), [this] { applyBg(); }, true);
}

} // namespace dv
