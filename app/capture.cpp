// DeepImage Viewer — 3D capture: the view as a picture (with measurements and notes), saved next to the model and
// opened in a new window in markup mode
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "app.h"
#include "archive.h"
#include "edit.h"
#include "i18n.h"
#include "settings.h"

#include <shellapi.h>
#include <shlobj.h>

namespace dv {

namespace {
float cxOf(const D2D1_RECT_F& r) { return (r.left + r.right) / 2; }
bool fileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
std::wstring stemOf(const std::wstring& name) {
    size_t d = name.find_last_of(L'.');
    return d == std::wstring::npos ? name : name.substr(0, d);
}
} // namespace

ComPtr<IWICBitmap> App::renderCapture() {
    if (mode != Mode::Model || !mv.hasModel()) return nullptr;
    const bool two = compare && compareModels && mvR.hasModel();
    auto one = [&](ModelView& m, D2D1_RECT_F pane, bool right) -> ComPtr<IWICBitmap> {
        const float vw = std::max(1.f, rw(pane)), vh = std::max(1.f, rh(pane));
        float k = G.scale * (S.capScale == 1 ? 2.f : 1.f);
        if (S.capScale == 2) k = 3840.f / std::max(vw, vh);
        k = std::min(k, 8192.f / std::max(vw, vh));
        const UINT W = (UINT)std::max(1L, std::lround(vw * k)), H = (UINT)std::max(1L, std::lround(vh * k));
        auto shot = m.screenshot(W, H);
        m.invalidate(); // the next frame renders at the window size again
        if (!shot) return nullptr;
        ComPtr<ID2D1Bitmap1> src;
        auto bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (FAILED(G.dc->CreateBitmapFromWicBitmap(shot.Get(), &bp, &src))) return nullptr;
        return renderToWic(W, H, [&](ID2D1DeviceContext* dc) {
            D2D1_MATRIX_3X2_F old;
            dc->GetTransform(&old);
            if (S.capBg == 0) G.fill(D2D1::RectF(0, 0, (float)W, (float)H), P.canvas);
            dc->DrawBitmap(src.Get(), D2D1::RectF(0, 0, (float)W, (float)H));
            if (S.capOverlay) {
                // overlays are laid out in window DIPs: map the pane onto the picture
                D2D1_MATRIX_3X2_F o = old;
                dc->SetTransform(D2D1::Matrix3x2F::Translation(-pane.left, -pane.top) * D2D1::Matrix3x2F::Scale(k, k) * o);
                if (!right) drawNotes(true, pane);
                measCapture = true;
                measCaptureSide = right ? 1 : 0;
                drawMeasure();
                measCapture = false;
                dc->SetTransform(old);
            }
        }, S.capBg == 2);
    };
    if (!two) return one(mv, rcViewport, false);
    auto l = one(mv, rcPaneL, false), r = one(mvR, rcPaneR, true);
    return l && r ? joinSideBySide(l.Get(), r.Get()) : nullptr;
}

void App::capture3d(int action) {
    capMenu = false;
    if (mode != Mode::Model || !mv.hasModel()) return;
    auto px = renderCapture();
    requestPaint();
    if (!px) { toast(T("save.failed"), true); return; }
    if (action == 1) {
        if (copyToClipboard(hwnd, px.Get())) toast(T("cap.copied"));
        else toast(T("save.failed"), true);
        return;
    }
    if (action == 2) {
        ComPtr<IFileSaveDialog> d;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return;
        COMDLG_FILTERSPEC spec[] = {{L"PNG", L"*.png"}};
        d->SetFileTypes(1, spec);
        d->SetDefaultExtension(L"png");
        std::wstring stem = stemOf(cur.name) + L"_view";
        d->SetFileName(stem.c_str());
        std::wstring base = arc::archiveOf(cur.path);
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(dirName(base.empty() ? cur.path : base).c_str(), nullptr, IID_PPV_ARGS(&folder)))) d->SetFolder(folder.Get());
        if (FAILED(d->Show(hwnd))) return;
        ComPtr<IShellItem> it;
        PWSTR p = nullptr;
        if (FAILED(d->GetResult(&it)) || FAILED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) return;
        std::wstring out = p;
        CoTaskMemFree(p);
        if (saveBitmap(out, GUID_ContainerFormatPng, px.Get(), 1, L"", false)) {
            toast(std::wstring(T("saved")) + L" · " + fileName(out));
            if (!_wcsicmp(dirName(out).c_str(), dirName(cur.path).c_str())) refreshFolder();
        } else toast(T("save.failed"), true);
        return;
    }
    // next to the model (an archive entry: next to the archive), never over an existing file
    std::wstring base = arc::archiveOf(cur.path);
    std::wstring dir = dirName(base.empty() ? cur.path : base);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::wstring stem = stemOf(cur.name) + fmtf(L"_capture_%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    auto pick = [&](const std::wstring& d) {
        std::wstring p = d + L"\\" + stem + L".png";
        for (int n = 2; fileExists(p) && n < 100; ++n) p = d + L"\\" + stem + fmtf(L"-%d", n) + L".png";
        return p;
    };
    std::wstring out = pick(dir);
    bool ok = saveBitmap(out, GUID_ContainerFormatPng, px.Get(), 1, L"", false);
    bool fallback = false;
    if (!ok) {
        // read-only folder (a CD, a protected location): Pictures\DeepImage Viewer
        PWSTR pics = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pics))) {
            std::wstring d = std::wstring(pics) + L"\\DeepImage Viewer";
            CoTaskMemFree(pics);
            CreateDirectoryW(d.c_str(), nullptr);
            out = pick(d);
            ok = saveBitmap(out, GUID_ContainerFormatPng, px.Get(), 1, L"", false);
            fallback = ok;
        }
    }
    if (!ok) { toast(T("save.failed"), true); return; }
    if (!fallback && base.empty()) refreshFolder(); // the capture shows up in the filmstrip
    if (S.capOpen) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring args = L"--markup \"" + out + L"\"";
        ShellExecuteW(nullptr, L"open", exe, args.c_str(), dirName(out).c_str(), SW_SHOWNORMAL);
    }
    toast(fmtf(T(fallback ? "cap.saved.pics" : (S.capOpen ? "cap.saved.open" : "cap.saved")), fileName(out).c_str()));
}

void App::drawCaptureMenu() {
    if (!capMenu) return;
    if (mode != Mode::Model || !mv.hasModel() || modal.kind) { capMenu = false; return; }
    // anything outside the card closes it
    auto& catcher = U.add(hid("cap.catch"), R(0, 0, G.W(), G.H()));
    catcher.click = [this] { capMenu = false; requestPaint(); };
    const float w = 316, h = 290;
    float left = std::clamp(cxOf(capBtnRect) - w / 2, 12.f, G.W() - w - 12);
    D2D1_RECT_F r = RW(left, capBtnRect.bottom + 12, w, h);
    G.shadow(r, 14, 1.4f);
    G.fill(r, P.surface, 14);
    G.stroke(r, P.line, 14, 1);
    U.add(hid("cap.card"), r);
    const float x = r.left + 18, iw = w - 36;
    float y = r.top + 12;
    G.text(T("cap.title"), RW(x, y, iw, 28), 13.5f, P.text, 600);
    y += 36;
    auto segRow = [&](const char* label, std::vector<std::wstring> opts, int cur, std::function<void(int)> set, int n) {
        G.text(T(label), RW(x, y, 80, 32), 12.5f, P.text2, 500);
        float sw = U.segmentedWidth(opts);
        U.segmented(hid("cap.seg", n), RW(x + iw - sw, y, sw, 32), opts, cur, std::move(set));
        y += 40;
    };
    segRow("cap.size", {T("cap.size.screen"), T("cap.size.x2"), L"4K"}, S.capScale, [this](int i) { S.capScale = i; S.save(); requestPaint(); }, 0);
    segRow("cap.bg", {T("cap.bg.view"), T("cap.bg.none"), T("cap.bg.white")}, S.capBg, [this](int i) { S.capBg = i; S.save(); requestPaint(); }, 1);
    auto toggleRow = [&](const char* label, bool on, Action a, int n) {
        G.text(T(label), RW(x, y, iw - 46, 34), 12.5f, P.text2, 500);
        U.toggleSwitch(hid("cap.sw", n), x + iw - 36, y + 6.5f, on, std::move(a));
        y += 36;
    };
    toggleRow("cap.overlay", S.capOverlay, [this] { S.capOverlay = !S.capOverlay; S.save(); requestPaint(); }, 0);
    toggleRow("cap.open", S.capOpen, [this] { S.capOpen = !S.capOpen; S.save(); requestPaint(); }, 1);
    std::wstring base = arc::archiveOf(cur.path);
    std::wstring dir = dirName(base.empty() ? cur.path : base);
    G.textEllipsis(fmtf(T("cap.where"), fileName(dir).c_str()), RW(x, y, iw, 22), 11.5f, P.text3, 500);
    y += 30;
    const float bw = (iw - 12) / 3;
    U.textButton(hid("cap.copy"), RW(x, y, bw, 34), T("cap.copy"), [this] { capture3d(1); });
    U.textButton(hid("cap.saveas"), RW(x + bw + 6, y, bw, 34), T("cap.saveas"), [this] { capture3d(2); });
    U.textButton(hid("cap.go"), RW(x + (bw + 6) * 2, y, bw, 34), T("cap.go"), [this] { capture3d(0); }, true);
}

} // namespace dv
