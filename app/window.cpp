// DeepImage Viewer — window, custom title bar, input routing
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "app.h"
#include "i18n.h"
#include "settings.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <oleidl.h>

namespace dv {

App A;

namespace {

const wchar_t* kClass = L"DMIND.DeepImageViewer";

class DropTarget : public IDropTarget {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** p) override {
        if (r == IID_IUnknown || r == IID_IDropTarget) { *p = this; AddRef(); return S_OK; }
        *p = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override { return --ref_; }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* d, DWORD, POINTL, DWORD* eff) override {
        FORMATETC f{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        ok_ = d && SUCCEEDED(d->QueryGetData(&f));
        *eff = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        A.dropHover = ok_;
        A.requestPaint();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* eff) override { *eff = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE; return S_OK; }
    HRESULT STDMETHODCALLTYPE DragLeave() override { A.dropHover = false; A.requestPaint(); return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* d, DWORD, POINTL, DWORD* eff) override {
        A.dropHover = false;
        *eff = DROPEFFECT_NONE;
        FORMATETC f{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM m{};
        if (d && SUCCEEDED(d->GetData(&f, &m))) {
            HDROP drop = (HDROP)GlobalLock(m.hGlobal);
            wchar_t path[MAX_PATH * 4];
            if (drop && DragQueryFileW(drop, 0, path, (UINT)std::size(path))) {
                std::wstring p = path;
                A.guardUnsaved([p] { A.openPath(p); });
                *eff = DROPEFFECT_COPY;
            }
            GlobalUnlock(m.hGlobal);
            ReleaseStgMedium(&m);
        }
        A.requestPaint();
        return S_OK;
    }
private:
    ULONG ref_ = 1;
    bool ok_ = false;
} g_drop;

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return A.proc(h, m, w, l); }

float dipX(LPARAM l) { return GET_X_LPARAM(l) / G.scale; }
float dipY(LPARAM l) { return GET_Y_LPARAM(l) / G.scale; }

} // namespace

bool App::create(HINSTANCE inst, const std::wstring& file) {
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(WS_EX_APPWINDOW, kClass, kAppName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1360, 860,
                           nullptr, nullptr, inst, nullptr);
    if (!hwnd) return false;
    // size for the monitor DPI
    UINT dpi = GetDpiForWindow(hwnd);
    if (S.hasPlacement) {
        SetWindowPlacement(hwnd, &S.placement);
    } else {
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int w = std::min<int>(MulDiv(1360, dpi, 96), wa.right - wa.left - 40), hgt = std::min<int>(MulDiv(860, dpi, 96), wa.bottom - wa.top - 40);
        SetWindowPos(hwnd, nullptr, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - hgt) / 2, w, hgt, SWP_NOZORDER);
    }
    if (!G.init(hwnd)) {
        MessageBoxW(hwnd, L"Direct3D 11 / Direct2D could not be initialised.", kAppName, MB_ICONERROR);
        return false;
    }
    applyTheme();
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    RegisterDragDrop(hwnd, &g_drop);
    thumbs.start(hwnd, WM_APP_THUMB);
    ShowWindow(hwnd, S.hasPlacement && S.placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    paint();
    if (!file.empty()) openPath(file);
    return true;
}

LRESULT App::hitTestNC(int sx, int sy) {
    POINT pt{sx, sy};
    ScreenToClient(hwnd, &pt);
    float x = pt.x / G.scale, y = pt.y / G.scale;
    if (fullscreen) return HTCLIENT;
    RECT wr;
    GetClientRect(hwnd, &wr);
    const bool maxed = IsZoomed(hwnd);
    const int frame = GetSystemMetricsForDpi(SM_CYFRAME, GetDpiForWindow(hwnd)) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, GetDpiForWindow(hwnd));
    if (!maxed && pt.y < frame / 2 + 1) {
        if (pt.x < frame * 2) return HTTOPLEFT;
        if (pt.x > wr.right - frame * 2) return HTTOPRIGHT;
        return HTTOP;
    }
    if (y < titleH()) {
        float W = G.W();
        if (x >= W - 46 * 3) {
            int i = (int)((x - (W - 46 * 3)) / 46);
            return i == 0 ? HTMINBUTTON : i == 1 ? HTMAXBUTTON : HTCLOSE;
        }
        if (U.overWidget(x, y) && modal.kind == 0) return HTCLIENT;
        if (modal.kind) return HTCLIENT;
        return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT App::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCCALCSIZE:
        if (w && !fullscreen) {
            auto* p = (NCCALCSIZE_PARAMS*)l;
            const LONG top = p->rgrc[0].top;
            DefWindowProcW(h, m, w, l);
            p->rgrc[0].top = top; // remove the caption, keep resize borders
            if (IsZoomed(h)) {
                UINT dpi = GetDpiForWindow(h);
                p->rgrc[0].top += GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        if (r != HTCLIENT && r != HTCAPTION && r != HTNOWHERE && r != HTSYSMENU && r != HTMINBUTTON && r != HTMAXBUTTON && r != HTCLOSE) return r;
        return hitTestNC(GET_X_LPARAM(l), GET_Y_LPARAM(l));
    }
    case WM_NCMOUSEMOVE: {
        int hv = w == HTMINBUTTON ? 1 : w == HTMAXBUTTON ? 2 : w == HTCLOSE ? 3 : 0;
        if (hv != capHover) { capHover = hv; requestPaint(); }
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE | TME_NONCLIENT, h, 0};
        TrackMouseEvent(&t);
        if (hv) return 0;
        break;
    }
    case WM_NCMOUSELEAVE:
        if (capHover || capPress) { capHover = 0; capPress = 0; requestPaint(); }
        break;
    case WM_NCLBUTTONDOWN:
        if (w == HTMINBUTTON || w == HTMAXBUTTON || w == HTCLOSE) {
            capPress = w == HTMINBUTTON ? 1 : w == HTMAXBUTTON ? 2 : 3;
            requestPaint();
            return 0;
        }
        break;
    case WM_NCLBUTTONUP:
        if (capPress) {
            int p = capPress;
            capPress = 0;
            int hv = w == HTMINBUTTON ? 1 : w == HTMAXBUTTON ? 2 : w == HTCLOSE ? 3 : 0;
            if (p == hv) {
                if (p == 1) ShowWindow(h, SW_MINIMIZE);
                else if (p == 2) ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
                else PostMessageW(h, WM_CLOSE, 0, 0);
            }
            requestPaint();
            return 0;
        }
        break;
    case WM_NCRBUTTONUP:
        if (w == HTCAPTION) {
            HMENU sys = GetSystemMenu(h, FALSE);
            UINT cmd = TrackPopupMenu(sys, TPM_RETURNCMD | TPM_RIGHTBUTTON, GET_X_LPARAM(l), GET_Y_LPARAM(l), 0, h, nullptr);
            if (cmd) PostMessageW(h, WM_SYSCOMMAND, cmd, 0);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        requestPaint();
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        paint();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE:
        if (G.swap) { G.resize(); paint(); }
        return 0;
    case WM_DPICHANGED: {
        auto* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        G.resize();
        requestPaint();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mi = (MINMAXINFO*)l;
        UINT dpi = GetDpiForWindow(h);
        mi->ptMinTrackSize.x = MulDiv(760, dpi, 96);
        mi->ptMinTrackSize.y = MulDiv(500, dpi, 96);
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (l && !wcscmp((const wchar_t*)l, L"ImmersiveColorSet")) { applyTheme(); requestPaint(); }
        break;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        applyTheme();
        requestPaint();
        break;
    case WM_TIMER:
        if (w == 1) {
            if (doc.tick()) requestPaint();
            if (compare && docR.tick()) requestPaint();
            if (needsAnimation()) requestPaint();
            else ensureTimer();
        } else if (w == 2) {
            KillTimer(h, 2);
            if (U.tooltipDue()) requestPaint();
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            float x = pt.x / G.scale, y = pt.y / G.scale;
            HCURSOR c = nullptr;
            if (HCURSOR wc = U.cursorAt(x, y)) c = wc;
            else if (U.overWidget(x, y) || modal.kind) c = LoadCursorW(nullptr, IDC_ARROW);
            else if (inside(rcViewport, x, y)) {
                if (mode == Mode::Image) {
                    if (panel == EditPanel::Crop) {
                        int hh = cropHitTest(x, y);
                        c = LoadCursorW(nullptr, hh == 8 ? IDC_SIZEALL : (hh == 0 || hh == 4) ? IDC_SIZENWSE : (hh == 2 || hh == 6) ? IDC_SIZENESW
                                                           : (hh == 1 || hh == 5) ? IDC_SIZENS : (hh == 3 || hh == 7) ? IDC_SIZEWE : IDC_CROSS);
                    } else if (annot && markup.tool == Tool::Hand && !spaceDown &&
                               (drag == DragKind::MoveShape || (drag == DragKind::None && markup.shapeAt(view.toImage(x, y), 1.f / view.s))))
                        c = LoadCursorW(nullptr, IDC_SIZEALL);
                    else if (annot && markup.tool != Tool::Hand && !spaceDown)
                        c = LoadCursorW(nullptr, markup.tool == Tool::Eraser ? IDC_ARROW : IDC_CROSS);
                    else c = LoadCursorW(nullptr, drag == DragKind::Pan ? IDC_SIZEALL : IDC_HAND);
                } else c = LoadCursorW(nullptr, IDC_ARROW);
            }
            SetCursor(c ? c : LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        SetCapture(h);
        onMouseDown(dipX(l), dipY(l), m == WM_LBUTTONDOWN ? 0 : m == WM_RBUTTONDOWN ? 1 : 2, w);
        return 0;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
        onMouseUp(dipX(l), dipY(l), m == WM_LBUTTONUP ? 0 : m == WM_RBUTTONUP ? 1 : 2);
        if (!(w & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK:
        onDoubleClick(dipX(l), dipY(l));
        return 0;
    case WM_XBUTTONUP:
        if (modal.kind == 0) guardUnsaved([this, d = GET_XBUTTON_WPARAM(w) == XBUTTON1 ? -1 : 1] { go(d); });
        return TRUE;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        onMouseMove(dipX(l), dipY(l), w);
        return 0;
    }
    case WM_MOUSELEAVE:
        mouseInViewport = false;
        U.mouseMove(-100, -100);
        if (capHover) capHover = 0;
        requestPaint();
        return 0;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(h, &pt);
        onWheel(pt.x / G.scale, pt.y / G.scale, (float)GET_WHEEL_DELTA_WPARAM(w), (GET_KEYSTATE_WPARAM(w) & MK_CONTROL) != 0, m == WM_MOUSEHWHEEL);
        return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (onKey((UINT)w, true)) return 0;
        break;
    case WM_KEYUP: case WM_SYSKEYUP:
        if (onKey((UINT)w, false)) return 0;
        break;
    case WM_CHAR:
        onChar((wchar_t)w);
        return 0;
    case WM_APP_IMAGE:
        onImageDecoded();
        return 0;
    case WM_APP_IMAGE_R: {
        std::unique_ptr<std::pair<std::wstring, std::shared_ptr<DecodedImage>>> holder((std::pair<std::wstring, std::shared_ptr<DecodedImage>>*)l);
        if (!holder || (uint64_t)w != (seqR & 0xFFFFFFFF) || !compare) return 0;
        if (holder->second) setRight(holder->second, holder->first);
        else { loadingR = false; errorR = "load.failed"; docR.clear(); requestPaint(); }
        return 0;
    }
    case WM_APP_MODEL: {
        std::unique_ptr<std::string> err((std::string*)w);
        onModelLoaded(err ? std::stoull(err->substr(0, err->find('|'))) : 0, (DvModel*)l, err ? err->substr(err->find('|') + 1) : "");
        return 0;
    }
    case WM_APP_THUMB: {
        ThumbResult r;
        bool any = false;
        while (thumbs.take(r)) {
            any = true;
            if (!r.ok || !r.w) { thumbFailed[r.path] = true; continue; }
            ComPtr<ID2D1Bitmap1> b;
            auto bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (SUCCEEDED(G.dc->CreateBitmap(D2D1::SizeU(r.w, r.h), r.bgra.data(), r.w * 4, &bp, &b))) thumbBmp[r.path] = b;
        }
        if (any) requestPaint();
        return 0;
    }
    case WM_APP_PRELOAD: {
        std::unique_ptr<std::pair<std::wstring, std::shared_ptr<DecodedImage>>> holder((std::pair<std::wstring, std::shared_ptr<DecodedImage>>*)l);
        preloadBusy = false;
        if (holder) {
            if (holder->second) {
                if (!cache.count(holder->first)) { cache[holder->first] = holder->second; cacheOrder.push_back(holder->first); }
            } else preloadFailed[holder->first] = true;
        }
        startPreload();
        return 0;
    }
    case WM_CLOSE:
        guardUnsaved([this] {
            if (!fullscreen) { S.placement.length = sizeof S.placement; GetWindowPlacement(hwnd, &S.placement); S.hasPlacement = true; }
            S.save();
            DestroyWindow(hwnd);
        });
        return 0;
    case WM_DESTROY:
        RevokeDragDrop(h);
        thumbs.stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool App::needsAnimation() {
    if (view.animating() || (compare && viewR.animating())) return true;
    if (compare && (loadingR || docR.frameCount() > 1)) return true;
    if (mode == Mode::Model && mv.animating()) return true;
    if (mode == Mode::Image && doc.frameCount() > 1 && !editMode) return true;
    if (loading) return true;
    if (!toasts.empty()) return true;
    if (std::fabs(stripScroll - stripTarget) > 0.002f) return true;
    return false;
}

void App::ensureTimer() {
    bool want = needsAnimation();
    if (want && !timerOn) { SetTimer(hwnd, 1, 15, nullptr); timerOn = true; }
    else if (!want && timerOn) { KillTimer(hwnd, 1); timerOn = false; }
}

// ======================================================================== mouse
int App::cropHitTest(float x, float y) {
    auto a = view.toScreen(crop.left, crop.top), b = view.toScreen(crop.right, crop.bottom);
    float l = std::min(a.x, b.x), r = std::max(a.x, b.x), t = std::min(a.y, b.y), btm = std::max(a.y, b.y);
    const float g = 10;
    bool nl = std::fabs(x - l) < g, nr = std::fabs(x - r) < g, nt = std::fabs(y - t) < g, nb = std::fabs(y - btm) < g;
    bool inX = x > l - g && x < r + g, inY = y > t - g && y < btm + g;
    if (nl && nt) return 0;
    if (nr && nt) return 2;
    if (nr && nb) return 4;
    if (nl && nb) return 6;
    if (nt && inX) return 1;
    if (nr && inY) return 3;
    if (nb && inX) return 5;
    if (nl && inY) return 7;
    if (x > l && x < r && y > t && y < btm) return 8;
    return -1;
}

void App::onMouseDown(float x, float y, int button, WPARAM keys) {
    downX = lastX = x;
    downY = lastY = y;
    if (button == 0 && U.mouseDown(x, y)) { requestPaint(); return; }
    if (modal.kind) return;
    // compare mode: right-click a thumbnail to put it in the right pane
    if (button == 1 && inside(rcStrip, x, y)) {
        for (auto& [k, r] : stripHits)
            if (inside(r, x, y)) {
                if (!compare && files[k].kind == Kind::Image && mode == Mode::Image) toggleCompare(1);
                if (compare) showRight(k);
                requestPaint();
                break;
            }
        return;
    }
    if (!inside(rcViewport, x, y)) return;
    if (mode == Mode::Image && doc.loaded()) {
        if (button == 0 && panel == EditPanel::Crop) {
            cropHandle = cropHitTest(x, y);
            if (cropHandle >= 0) drag = DragKind::Crop;
            return;
        }
        if (button == 0 && annot && markup.tool == Tool::Hand && !spaceDown) {
            if (markup.pick(view.toImage(x, y), 1.f / view.s)) {
                markup.beginMove(view.toImage(x, y));
                drag = DragKind::MoveShape;
            } else drag = DragKind::Pan;
            requestPaint();
            return;
        }
        if (button == 0 && annot && markup.tool != Tool::Hand && !spaceDown) {
            markup.begin(view.toImage(x, y), 1.f / view.s);
            drag = DragKind::Draw;
            requestPaint();
            return;
        }
        if (button == 0 || button == 2) drag = DragKind::Pan;
    } else if (mode == Mode::Model && mv.hasModel()) {
        drag = button == 0 && !(keys & MK_SHIFT) ? DragKind::Orbit : DragKind::PanModel;
        if (!hintGone) { hintGone = true; requestPaint(); }
    }
}

void App::onMouseMove(float x, float y, WPARAM keys) {
    bool redraw = U.mouseMove(x, y);
    if (redraw) { SetTimer(hwnd, 2, 460, nullptr); }
    bool inVp = inside(rcViewport, x, y);
    if (inVp != mouseInViewport) { mouseInViewport = inVp; redraw = true; }
    float dx = x - lastX, dy = y - lastY;
    lastX = x;
    lastY = y;
    switch (drag) {
    case DragKind::Pan: eachView([&](ImageView& v, const D2D1_RECT_F&) { v.panBy(dx, dy); }); redraw = true; break;
    case DragKind::MoveShape: markup.moveTo(view.toImage(x, y)); redraw = true; break;
    case DragKind::Draw: markup.move(view.toImage(x, y), 1.f / view.s, (keys & MK_SHIFT) != 0); redraw = true; break;
    case DragKind::Orbit: mv.orbit(dx, dy); redraw = true; break;
    case DragKind::PanModel: mv.pan(dx, dy, rh(rcViewport)); redraw = true; break;
    case DragKind::Crop: {
        auto p = view.toImage(x, y);
        auto q = view.toImage(x - dx, y - dy);
        float ddx = p.x - q.x, ddy = p.y - q.y;
        D2D1_RECT_F c = crop;
        float W = (float)doc.W(), H = (float)doc.H();
        if (cropHandle == 8) {
            float w = rw(c), h = rh(c);
            c.left = clampf(c.left + ddx, 0, W - w);
            c.top = clampf(c.top + ddy, 0, H - h);
            c.right = c.left + w;
            c.bottom = c.top + h;
        } else {
            if (cropHandle == 0 || cropHandle == 6 || cropHandle == 7) c.left = clampf(p.x, 0, c.right - 8);
            if (cropHandle == 2 || cropHandle == 3 || cropHandle == 4) c.right = clampf(p.x, c.left + 8, W);
            if (cropHandle == 0 || cropHandle == 1 || cropHandle == 2) c.top = clampf(p.y, 0, c.bottom - 8);
            if (cropHandle == 4 || cropHandle == 5 || cropHandle == 6) c.bottom = clampf(p.y, c.top + 8, H);
            float ratios[] = {0, (float)doc.W() / doc.H(), 1, 4.f / 3, 3.f / 2, 16.f / 9};
            float ar = cropAspect > 0 && cropAspect < 6 ? ratios[cropAspect] : 0;
            if (ar > 0) {
                bool vertical = cropHandle == 1 || cropHandle == 5;
                if (vertical) { float w = rh(c) * ar; c.right = std::min(W, c.left + w); }
                else {
                    float h = rw(c) / ar;
                    if (cropHandle == 0 || cropHandle == 1 || cropHandle == 2) c.top = std::max(0.f, c.bottom - h);
                    else c.bottom = std::min(H, c.top + h);
                }
            }
        }
        crop = c;
        redraw = true;
        break;
    }
    default: break;
    }
    if (redraw) requestPaint();
}

void App::onMouseUp(float x, float y, int button) {
    if (button == 0 && U.mouseUp(x, y)) { requestPaint(); ensureTimer(); return; }
    if (drag == DragKind::Draw) markup.end();
    if (drag == DragKind::MoveShape) markup.endMove();
    drag = DragKind::None;
    cropHandle = -1;
    requestPaint();
}

void App::onDoubleClick(float x, float y) {
    if (U.overWidget(x, y) || modal.kind) { onMouseDown(x, y, 0, 0); return; }
    if (!inside(rcViewport, x, y)) return;
    if (mode == Mode::Image && doc.loaded() && !(annot && markup.tool != Tool::Hand) && panel != EditPanel::Crop) {
        if (std::fabs(view.s - 1) < 1e-3f) eachView([](ImageView& v, const D2D1_RECT_F&) { v.mode == 4 ? v.fitWhole(true) : v.fit(true); });
        else zoomViews(1.f / view.s, x, y, true);
        ensureTimer();
        requestPaint();
    } else if (mode == Mode::Model) {
        mv.view("iso");
        ensureTimer();
        requestPaint();
    }
}

void App::onWheel(float x, float y, float delta, bool ctrl, bool horizontal) {
    if (U.wheel(x, y, horizontal ? -delta : delta)) { requestPaint(); ensureTimer(); return; }
    if (modal.kind || !inside(rcViewport, x, y)) return;
    if (mode == Mode::Image && doc.loaded()) {
        if (S.wheel == 1 && !ctrl && !annot && panel == EditPanel::None) {
            static DWORD last = 0;
            if (GetTickCount() - last < 160) return;
            last = GetTickCount();
            guardUnsaved([this, d = delta < 0 ? 1 : -1] { go(d); });
            return;
        }
        zoomViews(std::exp(delta / 120.f * 0.22f), x, y, false);
        requestPaint();
    } else if (mode == Mode::Model) {
        mv.zoom(delta);
        ensureTimer();
        requestPaint();
    }
}

void App::zoomViews(float factor, float x, float y, bool animate) {
    if (!compare) { view.zoomBy(factor, x, y, animate); ensureTimer(); return; }
    // keep both panes in step: zoom around the same relative point in each pane
    const D2D1_RECT_F& from = x >= rcPaneR.left ? rcPaneR : rcPaneL;
    float ox = x - from.left, oy = y - from.top;
    eachView([&](ImageView& v, const D2D1_RECT_F& p) { v.zoomBy(factor, p.left + ox, p.top + oy, animate); });
    ensureTimer();
}

void App::onChar(wchar_t c) {
    if (modal.kind != 3) return;
    std::wstring& f = focusField == 0 ? fieldW : fieldH;
    if (c >= L'0' && c <= L'9' && f.size() < 6) f.push_back(c);
    else if (c == 8 && !f.empty()) f.pop_back();
    else return;
    if (keepRatio && doc.loaded()) {
        float ar = (float)doc.W() / doc.H();
        long v = f.empty() ? 0 : std::wcstol(f.c_str(), nullptr, 10);
        if (focusField == 0) fieldH = v ? std::to_wstring(std::max(1L, std::lround(v / ar))) : L"";
        else fieldW = v ? std::to_wstring(std::max(1L, std::lround(v * ar))) : L"";
    }
    requestPaint();
}

bool App::onKey(UINT vk, bool down) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0;
    if (!down) {
        if (vk == VK_SPACE && spaceDown) { spaceDown = false; return true; }
        return false;
    }
    if (alt && vk == VK_F4) return false;
    if (modal.kind) {
        if (vk == VK_ESCAPE) { closeModal(); requestPaint(); return true; }
        if (modal.kind == 3) {
            if (vk == VK_TAB) { focusField ^= 1; requestPaint(); }
            else if (vk == VK_RETURN) applyResize();
        } else if (modal.kind == 4 && vk == VK_RETURN && !modal.buttons.empty()) {
            auto a = modal.buttons.back().action;
            closeModal();
            if (a) a();
        }
        requestPaint();
        return true;
    }
    const bool img = mode == Mode::Image && doc.loaded(), mdl = mode == Mode::Model && mv.hasModel();
    auto done = [this] { ensureTimer(); requestPaint(); return true; };
    const float cx = (rcViewport.left + rcViewport.right) / 2, cy = (rcViewport.top + rcViewport.bottom) / 2;
    auto rotateViews = [this](int deg) {
        if (editMode) { view.rotate(deg); bakeOrientationIntoImage(); }
        else eachView([deg](ImageView& v, const D2D1_RECT_F&) { v.rotate(deg); });
    };
    if (ctrl) {
        switch (vk) {
        case 'O': guardUnsaved([this] { openDialog(); }); return done();
        case 'S': if (img) saveAs(); else if (mdl) saveScreenshot(); return done();
        case 'C': if (img) copyImage(); return done();
        case 'Z': if (annot) { shift ? markup.redo() : markup.undo(); } else if (img && editMode) undoEdit(); return done();
        case 'Y': if (annot) markup.redo(); return done();
        case 'R': if (img && !annot && panel == EditPanel::None) rotateViews(shift ? -90 : 90); return done();
        case '0': case VK_NUMPAD0: if (img) eachView([](ImageView& v, const D2D1_RECT_F&) { v.fit(true); }); else if (mdl) mv.view("iso"); return done();
        case VK_OEM_PLUS: case VK_ADD: if (img) zoomViews(1.25f, cx, cy, true); return done();
        case VK_OEM_MINUS: case VK_SUBTRACT: if (img) zoomViews(0.8f, cx, cy, true); return done();
        }
        return false;
    }
    if (panel == EditPanel::Crop) {
        if (vk == VK_RETURN) { applyCrop(); return done(); }
        if (vk == VK_ESCAPE) { panel = EditPanel::None; return done(); }
    }
    if (panel == EditPanel::Adjust) {
        if (vk == VK_RETURN) { applyAdjust(); return done(); }
        if (vk == VK_ESCAPE) { adj = Adjust{}; fxActive = false; panel = EditPanel::None; return done(); }
    }
    if (annot) {
        Tool t = Tool::Hand;
        bool tool = true;
        switch (vk) {
        case 'H': t = Tool::Hand; break;
        case 'P': t = Tool::Pen; break;
        case 'F': t = Tool::Hl; break;
        case 'L': t = Tool::Line; break;
        case 'A': t = Tool::Arrow; break;
        case 'U': t = Tool::Rect; break;
        case 'O': t = Tool::Ellipse; break;
        case 'X': t = Tool::Eraser; break;
        case 'Z': t = Tool::Mosaic; break;
        case 'K': t = Tool::Mask; break;
        default: tool = false;
        }
        if (tool) { setTool(t); return done(); }
        const float k = 1.f / view.s;
        if (vk == VK_OEM_4) { markup.setWidth(std::max(1.f, markup.curWidth(k) - 2), k); markup.endStyleEdit(); return done(); }
        if (vk == VK_OEM_6) { markup.setWidth(std::min(120.f, markup.curWidth(k) + 2), k); markup.endStyleEdit(); return done(); }
        if ((vk == VK_DELETE || vk == VK_BACK) && markup.selection()) { markup.deleteSelection(); return done(); }
        if (vk == VK_ESCAPE && markup.selection()) { markup.deselect(); return done(); }
        if (vk == VK_ESCAPE || vk == VK_RETURN || vk == 'E') { toggleAnnot(0); return done(); }
        if (vk == VK_SPACE) { spaceDown = true; return true; }
    }
    if (editMode && vk == VK_ESCAPE) { toggleEdit(0); return done(); }
    switch (vk) {
    case VK_RIGHT: case VK_NEXT: guardUnsaved([this] { go(1); }); return done();
    case VK_LEFT: case VK_PRIOR: guardUnsaved([this] { go(-1); }); return done();
    case VK_HOME: if (!files.empty()) guardUnsaved([this] { show(0); }); return done();
    case VK_END: if (!files.empty()) guardUnsaved([this] { show((int)files.size() - 1); }); return done();
    case VK_OEM_PLUS: case VK_ADD: if (img) zoomViews(1.25f, cx, cy, true); return done();
    case VK_OEM_MINUS: case VK_SUBTRACT: if (img) zoomViews(0.8f, cx, cy, true); return done();
    case '0': case VK_NUMPAD0: if (img) setFitMode(0); else if (mdl) mv.view("iso"); return done();
    case '1': case VK_NUMPAD1: if (img) setFitMode(4); else if (mdl) mv.view("front"); return done();
    case '2': if (mdl) mv.view("top"); return done();
    case '3': if (mdl) mv.view("right"); return done();
    case VK_F11: toggleFullscreen(); return done();
    case VK_ESCAPE:
        // cancel what is in progress first; minimise only when there is nothing to cancel
        if (fullscreen) toggleFullscreen();
        else if (compare) toggleCompare(0);
        else ShowWindow(hwnd, SW_MINIMIZE);
        return done();
    case VK_TAB: toggleInfo(); return done();
    case 'R': if (img && !compare) { if (!editMode) toggleEdit(1); openResize(); } return done();
    case 'M':
        if (img && !compare) {
            if (editMode && panel == EditPanel::Adjust) { adj = Adjust{}; fxActive = false; panel = EditPanel::None; }
            else { if (!editMode) toggleEdit(1); beginAdjust(); }
        }
        return done();
    case 'F': if (img && !compare) { toggleAnnot(1); setTool(Tool::Hl); } return done();
    case 'E': if (img && !compare) toggleAnnot(); return done();
    case 'C': if (img && !compare) { if (!editMode) toggleEdit(1); beginCrop(); } return done();
    case 'V': if (img) toggleCompare(); return done();
    case 'T': S.filmstrip = !S.filmstrip; S.save(); return done();
    case 'W': if (mdl) { mv.wire = !mv.wire; mv.invalidate(); } return done();
    case 'G': if (mdl) { mv.grid = !mv.grid; mv.invalidate(); } return done();
    case 'B': if (mdl) { mv.bbox = !mv.bbox; mv.invalidate(); } return done();
    case VK_SPACE:
        if (mdl) { mv.autoRotate = !mv.autoRotate; mv.invalidate(); }
        else if (img && !editMode) guardUnsaved([this] { go(1); });
        return done();
    }
    return false;
}

} // namespace dv
