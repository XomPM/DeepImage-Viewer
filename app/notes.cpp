// DeepImage Viewer — 3D notes: an arrow from a face / edge / vertex to a callout with text and a picture
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
//
// The anchor is stored in the file's own coordinates, the callout as a screen offset from the anchor, so the
// callout follows its point while the model turns and always faces the viewer. Notes are kept in
// "<model>.dvnotes" (JSON, pictures as base64 PNG) next to the model; archives and read-only folders use
// %LOCALAPPDATA%\DMIND\DeepImage Viewer\Notes.
#include "app.h"
#include "archive.h"
#include "edit.h"
#include "i18n.h"
#include "image.h"
#include "json.h"
#include "settings.h"

#include <shlobj.h>

#include <fstream>
#include <sstream>

namespace dv {

namespace {
constexpr int kNoteEditId = 1001;
const uint32_t kNoteColors[6] = {0xFF9F0A, 0x0A84FF, 0x30D158, 0xFF375F, 0xBF5AF2, 0x64D2FF};
float cxOf(const D2D1_RECT_F& r) { return (r.left + r.right) / 2; }
float cyOf(const D2D1_RECT_F& r) { return (r.top + r.bottom) / 2; }
COLORREF cref(D2D1_COLOR_F c) { return RGB((int)(c.r * 255 + .5f), (int)(c.g * 255 + .5f), (int)(c.b * 255 + .5f)); }

std::wstring localNotesDir() {
    PWSTR p = nullptr;
    std::wstring d;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &p))) d = std::wstring(p) + L"\\DMIND\\DeepImage Viewer\\Notes";
    CoTaskMemFree(p);
    return d;
}

std::wstring hashName(const std::wstring& s) {
    uint64_t h = 1469598103934665603ull;
    for (wchar_t c : lower(s)) { h ^= (uint64_t)c; h *= 1099511628211ull; }
    return fmtf(L"%016llx.dvnotes", (unsigned long long)h);
}

std::string encodePng(IWICBitmapSource* src) {
    std::string out;
    ComPtr<IStream> st;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &st))) return out;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> fr;
    if (FAILED(G.wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) || FAILED(enc->Initialize(st.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(enc->CreateNewFrame(&fr, nullptr)) || FAILED(fr->Initialize(nullptr)))
        return out;
    UINT w = 0, h = 0;
    src->GetSize(&w, &h);
    fr->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    fr->SetPixelFormat(&fmt);
    ComPtr<IWICFormatConverter> cv;
    G.wic->CreateFormatConverter(&cv);
    if (FAILED(cv->Initialize(src, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(fr->WriteSource(cv.Get(), nullptr)) || FAILED(fr->Commit()) || FAILED(enc->Commit()))
        return out;
    HGLOBAL hg = nullptr;
    if (FAILED(GetHGlobalFromStream(st.Get(), &hg))) return out;
    STATSTG ss{};
    st->Stat(&ss, STATFLAG_NONAME);
    if (void* p = GlobalLock(hg)) {
        out.assign((const char*)p, (size_t)ss.cbSize.QuadPart);
        GlobalUnlock(hg);
    }
    return out;
}

ComPtr<IWICBitmap> decodeMemory(const std::string& bytes) {
    ComPtr<IWICStream> st;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> fr;
    ComPtr<IWICFormatConverter> cv;
    ComPtr<IWICBitmap> out;
    if (bytes.empty() || FAILED(G.wic->CreateStream(&st)) || FAILED(st->InitializeFromMemory((BYTE*)bytes.data(), (DWORD)bytes.size())) ||
        FAILED(G.wic->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) || FAILED(dec->GetFrame(0, &fr)) ||
        FAILED(G.wic->CreateFormatConverter(&cv)) ||
        FAILED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(G.wic->CreateBitmapFromSource(cv.Get(), WICBitmapCacheOnLoad, &out)))
        return nullptr;
    return out;
}

App* g_noteApp = nullptr;
LRESULT CALLBACK noteEditThunk(HWND h, UINT m, WPARAM w, LPARAM l) { return g_noteApp ? g_noteApp->noteEditMsg(h, m, w, l) : DefWindowProcW(h, m, w, l); }
} // namespace

// ------------------------------------------------------------------ mode, storage
void App::toggleNotes(int on) {
    bool v = on < 0 ? !noting : on != 0;
    if (mode != Mode::Model || !mv.hasModel()) v = false;
    if (v == noting) return;
    noting = v;
    hintGone = true;
    hover3d.ok = false;
    if (v) {
        if (measuring) toggleMeasure(0);
        if (!S.notesShow) { S.notesShow = true; S.save(); }
        toast(T("note.hint"));
    } else closeNoteEditor();
    requestPaint();
}

void App::loadNotes() {
    closeNoteEditor();
    notes.clear();
    notesKey = cur.path;
    noteNextId = 1;
    // next to the model, or in the local folder (archives, read-only places)
    const bool inArchive = !arc::archiveOf(cur.path).empty();
    const std::wstring local = localNotesDir() + L"\\" + hashName(inArchive ? arc::archiveOf(cur.path) + L"|" + cur.name : cur.path);
    const std::wstring side = inArchive ? L"" : cur.path + L".dvnotes";
    notesFile = side.empty() ? local : side;
    std::vector<uint8_t> bytes;
    std::wstring from;
    if (!side.empty() && readFileBytes(side, bytes, 64ull << 20)) from = side;
    else if (readFileBytes(local, bytes, 64ull << 20)) from = local;
    if (from.empty()) return;
    notesFile = from;
    json::J root;
    if (!json::parse(std::string(bytes.begin(), bytes.end()), root) || root.t != json::J::Obj) return;
    const json::J* arr = root.get("notes");
    if (!arr || arr->t != json::J::Arr) return;
    for (auto& o : arr->a) {
        if (o.t != json::J::Obj) continue;
        Note3D n;
        n.model = {(float)json::num(o, "x"), (float)json::num(o, "y"), (float)json::num(o, "z")};
        n.snap = std::clamp((int)json::num(o, "snap"), 0, 2);
        n.dx = (float)json::num(o, "dx", 80);
        n.dy = (float)json::num(o, "dy", -70);
        n.color = std::clamp((int)json::num(o, "color"), 0, 5);
        n.text = fromUtf8(o.str("text"));
        std::string img = o.str("image");
        if (!img.empty()) {
            n.png = json::unbase64(img);
            n.img = decodeMemory(n.png);
            if (!n.img) n.png.clear();
        }
        n.id = noteNextId++;
        notes.push_back(std::move(n));
    }
}

void App::saveNotes() {
    if (notesKey.empty() || notesKey != cur.path) return;
    const bool inArchive = !arc::archiveOf(cur.path).empty();
    const std::wstring local = localNotesDir() + L"\\" + hashName(inArchive ? arc::archiveOf(cur.path) + L"|" + cur.name : cur.path);
    if (notes.empty()) {
        // nothing left: no empty notes file lying around
        DeleteFileW(notesFile.c_str());
        if (notesFile != local) DeleteFileW(local.c_str());
        return;
    }
    std::ostringstream o;
    o << "{\n  \"app\": \"DeepImage Viewer\",\n  \"version\": 1,\n  \"model\": " << json::quote(toUtf8(cur.name)) << ",\n  \"notes\": [";
    for (size_t i = 0; i < notes.size(); ++i) {
        const auto& n = notes[i];
        char nums[256];
        snprintf(nums, sizeof nums, "\"x\": %.9g, \"y\": %.9g, \"z\": %.9g, \"snap\": %d, \"dx\": %.1f, \"dy\": %.1f, \"color\": %d", n.model.x, n.model.y,
                 n.model.z, n.snap, n.dx, n.dy, n.color);
        o << (i ? ",\n" : "\n") << "    {" << nums << ", \"text\": " << json::quote(toUtf8(n.text));
        if (!n.png.empty()) o << ", \"image\": \"" << json::base64(n.png) << "\"";
        o << "}";
    }
    o << "\n  ]\n}\n";
    const std::string data = o.str();
    auto write = [&](const std::wstring& path) {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        DWORD w = 0;
        bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &w, nullptr) && w == data.size();
        CloseHandle(f);
        return ok;
    };
    if (write(notesFile)) return;
    // the model's folder is read-only: keep them locally
    CreateDirectoryW((localNotesDir()).c_str(), nullptr);
    if (write(local)) notesFile = local;
    else toast(T("note.savefail"), true);
}

// ------------------------------------------------------------------ adding, editing
void App::addNoteAt(float x, float y) {
    const D2D1_RECT_F pane = compare && compareModels ? rcPaneL : rcViewport;
    if (!inside(pane, x, y) || !mv.hasModel()) return;
    auto h = mv.pick(x - pane.left, y - pane.top, rw(pane), rh(pane));
    if (!h.ok) { toast(T("note.miss"), true); return; }
    Note3D n;
    n.model = h.model;
    n.snap = h.snap;
    n.color = (int)(notes.size() % 6);
    n.id = noteNextId++;
    // callout away from the middle of the view, so it does not sit on the model
    float vx = x - cxOf(pane), vy = y - cyOf(pane), l = std::hypot(vx, vy);
    if (l < 20) { vx = 1; vy = -1; l = std::sqrt(2.f); }
    n.dx = vx / l * 120;
    n.dy = vy / l * 90;
    notes.push_back(std::move(n));
    saveNotes();
    openNoteEditor((int)notes.size() - 1);
}

void App::openNoteEditor(int i) {
    if (i < 0 || i >= (int)notes.size()) return;
    noteEdit = i;
    if (!noteFont) {
        noteFont = CreateFontW(-(int)std::lround(13.5f * G.scale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
    }
    if (!noteEditHwnd) {
        // keep the swap chain from painting over the edit control
        SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) | WS_CLIPCHILDREN);
        noteEditHwnd = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)kNoteEditId,
                                       GetModuleHandleW(nullptr), nullptr);
        SendMessageW(noteEditHwnd, WM_SETFONT, (WPARAM)noteFont, TRUE);
        SendMessageW(noteEditHwnd, EM_SETLIMITTEXT, 2000, 0);
        g_noteApp = this;
        noteEditProc0 = (WNDPROC)SetWindowLongPtrW(noteEditHwnd, GWLP_WNDPROC, (LONG_PTR)noteEditThunk);
    }
    SetWindowTextW(noteEditHwnd, notes[i].text.c_str());
    int len = GetWindowTextLengthW(noteEditHwnd);
    SendMessageW(noteEditHwnd, EM_SETSEL, len, len);
    noteEditRect = {};
    requestPaint();
    // shown and focused once the card has been laid out (next frame)
}

void App::closeNoteEditor() {
    if (noteEdit >= 0 && noteEdit < (int)notes.size() && noteEditHwnd) {
        int n = GetWindowTextLengthW(noteEditHwnd);
        std::wstring t(n, 0);
        GetWindowTextW(noteEditHwnd, t.data(), n + 1);
        notes[noteEdit].text = t;
        saveNotes();
    }
    noteEdit = -1;
    if (noteEditHwnd) {
        ShowWindow(noteEditHwnd, SW_HIDE);
        if (GetFocus() == noteEditHwnd) SetFocus(hwnd);
    }
    requestPaint();
}

void App::deleteNote(int i) {
    if (i < 0 || i >= (int)notes.size()) return;
    if (noteEdit == i) {
        noteEdit = -1;
        if (noteEditHwnd) { ShowWindow(noteEditHwnd, SW_HIDE); SetFocus(hwnd); }
    } else if (noteEdit > i) --noteEdit;
    notes.erase(notes.begin() + i);
    saveNotes();
    requestPaint();
}

void App::setNoteImage(int i, ComPtr<IWICBitmap> img) {
    if (i < 0 || i >= (int)notes.size()) return;
    auto& n = notes[i];
    n.bmp.Reset();
    if (!img) { n.img.Reset(); n.png.clear(); saveNotes(); requestPaint(); return; }
    UINT w = 0, h = 0;
    img->GetSize(&w, &h);
    const UINT maxSide = 480;
    if (w > maxSide || h > maxSide) {
        float s = (float)maxSide / std::max(w, h);
        img = resizeBitmap(img.Get(), std::max(1u, (UINT)std::lround(w * s)), std::max(1u, (UINT)std::lround(h * s)));
    }
    if (!img) return;
    n.img = img;
    n.png = encodePng(img.Get());
    saveNotes();
    requestPaint();
}

void App::noteImageFromFile(int i) {
    ComPtr<IFileOpenDialog> d;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return;
    COMDLG_FILTERSPEC spec[] = {{L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.tif;*.tiff;*.heic;*.avif"}, {L"*.*", L"*.*"}};
    d->SetFileTypes(2, spec);
    if (FAILED(d->Show(hwnd))) return;
    ComPtr<IShellItem> it;
    PWSTR p = nullptr;
    if (FAILED(d->GetResult(&it)) || FAILED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) return;
    std::wstring path = p;
    CoTaskMemFree(p);
    DecodedImage img;
    std::string err;
    if (!decodeImageFile(path, img, err, 1024) || img.frames.empty()) { toast(T("load.failed"), true); return; }
    setNoteImage(i, img.frames[0]);
}

bool App::noteImageFromClipboard(int i) {
    if (!IsClipboardFormatAvailable(CF_BITMAP) && !IsClipboardFormatAvailable(CF_DIB)) return false;
    if (!OpenClipboard(hwnd)) return false;
    ComPtr<IWICBitmap> bmp;
    if (HBITMAP hb = (HBITMAP)GetClipboardData(CF_BITMAP)) G.wic->CreateBitmapFromHBITMAP(hb, nullptr, WICBitmapIgnoreAlpha, &bmp);
    CloseClipboard();
    if (!bmp) return false;
    ComPtr<IWICFormatConverter> cv;
    ComPtr<IWICBitmap> out;
    G.wic->CreateFormatConverter(&cv);
    if (FAILED(cv->Initialize(bmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(G.wic->CreateBitmapFromSource(cv.Get(), WICBitmapCacheOnLoad, &out)))
        return false;
    setNoteImage(i, out);
    toast(T("note.pasted"));
    return true;
}

LRESULT App::noteEditMsg(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN) {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (w == VK_ESCAPE || (w == VK_RETURN && ctrl)) { closeNoteEditor(); return 0; }
        if (w == VK_TAB) { closeNoteEditor(); return 0; }
    }
    if (m == WM_CHAR && (w == 27 || w == 10 || w == 9)) return 0; // no beep for Esc / Ctrl+Enter / Tab
    if (m == WM_PASTE && !IsClipboardFormatAvailable(CF_UNICODETEXT) && noteImageFromClipboard(noteEdit)) return 0;
    return CallWindowProcW(noteEditProc0, h, m, w, l);
}

// ------------------------------------------------------------------ visibility, drawing
void App::updateNoteVisibility(bool force) {
    if (notes.empty() || !mv.hasModel()) return;
    const DWORD now = GetTickCount();
    if (!force && (now - noteVisTick < 160 || drag == DragKind::Orbit || drag == DragKind::PanModel)) {
        SetTimer(hwnd, 7, 200, nullptr); // look again once the camera has settled
        return;
    }
    noteVisTick = now;
    for (auto& n : notes) n.hidden = !mv.visible(n.model);
}

void App::drawNotes(bool capture, D2D1_RECT_F pane) {
    if (mode != Mode::Model || !mv.hasModel() || notesKey != cur.path) return;
    if (!S.notesShow && !noting) return;
    updateNoteVisibility(capture);
    auto dc = G.dc.Get();
    const D2D1_COLOR_F darkHover = rgba(0x000000, .5f);
    if (!capture) dc->PushAxisAlignedClip(pane, D2D1_ANTIALIAS_MODE_ALIASED);
    // hidden ones first, so a faded callout never covers a visible one
    for (int pass = 0; pass < 2; ++pass)
    for (size_t i = 0; i < notes.size(); ++i) {
        auto& n = notes[i];
        if ((pass == 0) != n.hidden) continue;
        float ax, ay;
        if (!mv.project(mv.toWorld(n.model), rw(pane), rh(pane), ax, ay)) continue;
        ax += pane.left;
        ay += pane.top;
        const float fa = n.hidden ? .38f : 1.f; // behind the model: faded
        const D2D1_COLOR_F c = withA(rgba(kNoteColors[n.color % 6]), fa);
        const D2D1_COLOR_F dark = withA(rgba(0x000000, .5f), fa);
        // callout size: wrapped text + picture
        const float maxW = 232;
        std::wstring text = n.text;
        if ((int)i == noteEdit && noteEditHwnd) {
            int len = GetWindowTextLengthW(noteEditHwnd);
            text.assign(len, 0);
            GetWindowTextW(noteEditHwnd, text.data(), len + 1);
        }
        const bool empty = text.empty() && !n.img;
        if (empty && !capture) text = T("note.empty"); // a capture shows just the numbered tag
        float tw = 0, th = 0;
        ComPtr<IDWriteTextLayout> lay;
        if (!text.empty()) {
            lay = G.layout(text, 12.5f, 500, maxW, 600);
            DWRITE_TEXT_METRICS tm{};
            if (lay && SUCCEEDED(lay->GetMetrics(&tm))) { tw = std::ceil(tm.widthIncludingTrailingWhitespace); th = std::ceil(tm.height); }
        }
        float iw = 0, ih = 0;
        if (n.img) {
            UINT w = 0, h = 0;
            n.img->GetSize(&w, &h);
            float s = std::min({maxW / w, 150.f / h, 1.f});
            iw = std::max(1.f, w * s);
            ih = std::max(1.f, h * s);
            if (!n.bmp) dc->CreateBitmapFromWicBitmap(n.img.Get(), nullptr, &n.bmp);
        }
        const float bw = std::max({tw, iw, 36.f}) + 24, bh = 12 + th + (n.img ? ih + (th > 0 ? 8 : 0) : 0) + 12;
        float bx = ax + n.dx - bw / 2, by = ay + n.dy - bh / 2;
        bx = std::clamp(bx, pane.left + 6, std::max(pane.left + 6, pane.right - bw - 6));
        by = std::clamp(by, pane.top + 6, std::max(pane.top + 6, pane.bottom - bh - 6));
        D2D1_RECT_F box = RW(bx, by, bw, bh);
        const bool hidden = n.hidden;
        // leader: from the box edge to the anchor, arrowhead at the anchor
        const float cx = cxOf(box), cy = cyOf(box);
        float ex = cx, ey = cy;
        {
            float vx = ax - cx, vy = ay - cy;
            float tx = vx != 0 ? (bw / 2) / std::fabs(vx) : 1e9f, ty = vy != 0 ? (bh / 2) / std::fabs(vy) : 1e9f;
            float t = std::min({tx, ty, 1.f});
            ex = cx + vx * t;
            ey = cy + vy * t;
        }
        const float len = std::hypot(ax - ex, ay - ey);
        if (len > 6) {
            const float ux = (ax - ex) / len, uy = (ay - ey) / len;
            const float hx = ax - ux * 5, hy = ay - uy * 5; // arrow tip stops just short of the dot
            G.line(ex, ey, hx, hy, dark, 4.5f);
            G.line(ex, ey, hx, hy, c, 2.25f);
            const float a = 10, wv = 5.5f;
            const float lx = hx - ux * a + uy * wv, ly = hy - uy * a - ux * wv, rx = hx - ux * a - uy * wv, ry = hy - uy * a + ux * wv;
            ComPtr<ID2D1PathGeometry> geo;
            ComPtr<ID2D1GeometrySink> sk;
            ComPtr<ID2D1Factory> f;
            dc->GetFactory(&f);
            if (SUCCEEDED(f->CreatePathGeometry(&geo)) && SUCCEEDED(geo->Open(&sk))) {
                sk->BeginFigure(D2D1::Point2F(hx, hy), D2D1_FIGURE_BEGIN_FILLED);
                D2D1_POINT_2F pts[2] = {D2D1::Point2F(lx, ly), D2D1::Point2F(rx, ry)};
                sk->AddLines(pts, 2);
                sk->EndFigure(D2D1_FIGURE_END_CLOSED);
                sk->Close();
                dc->DrawGeometry(geo.Get(), G.brush(dark), 2.5f);
                dc->FillGeometry(geo.Get(), G.brush(c));
            }
        }
        // anchor: square = vertex, ring = edge, dot = face (same as the measurement markers)
        if (n.snap == 1) { G.fill(RW(ax - 5, ay - 5, 10, 10), dark, 2); G.fill(RW(ax - 3.5f, ay - 3.5f, 7, 7), c, 1.5f); }
        else if (n.snap == 2) { G.circle(ax, ay, 5.5f, dark); G.circle(ax, ay, 4, c); G.circle(ax, ay, 1.6f, rgba(0xFFFFFF)); }
        else { G.circle(ax, ay, 5, dark); G.circle(ax, ay, 3.5f, c); }
        // callout
        if (!hidden) G.shadow(box, 10, .9f);
        G.fill(box, withA(P.dark ? rgba(0x1C1C1F, .96f) : rgba(0xFFFFFF, .97f), hidden ? .7f : 1.f), 10);
        G.stroke(box, (int)i == noteEdit ? P.accent : withA(c, .9f), 10, (int)i == noteEdit ? 2.f : 1.5f);
        G.fill(RW(box.left + 1, box.top + 9, 3.5f, bh - 18), c, 1.5f);
        float y = box.top + 12;
        if (lay) {
            dc->DrawTextLayout(D2D1::Point2F(box.left + 12, y), lay.Get(), G.brush(withA(empty ? P.text3 : P.text, hidden ? .5f : 1.f)));
            y += th + 8;
        }
        if (n.bmp) dc->DrawBitmap(n.bmp.Get(), RW(box.left + 12, y, iw, ih), fa, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        // number badge on the corner
        G.circle(box.left + 2, box.top + 2, 10, dark);
        G.circle(box.left + 2, box.top + 2, 9, c);
        G.text(fmtf(L"%d", (int)i + 1), RW(box.left - 8, box.top - 8, 20, 20), 10.5f, withA(rgba(0x111214), fa), 800, AC);
        if (!capture) {
            // drag the callout to move it, click to edit
            const uint32_t id = n.id;
            auto& g = U.add(hid("note", (int)id), box);
            g.cursor = LoadCursorW(nullptr, IDC_HAND);
            g.drag = [this, id](float x, float y, bool first) {
                auto it = std::find_if(notes.begin(), notes.end(), [id](const Note3D& q) { return q.id == id; });
                if (it == notes.end()) return;
                if (first) { noteDragX = x; noteDragY = y; noteDragDX = it->dx; noteDragDY = it->dy; noteDragMoved = false; return; }
                if (std::hypot(x - noteDragX, y - noteDragY) > 3) noteDragMoved = true;
                if (noteDragMoved) { it->dx = noteDragDX + x - noteDragX; it->dy = noteDragDY + y - noteDragY; requestPaint(); }
            };
            g.click = [this, id] {
                auto it = std::find_if(notes.begin(), notes.end(), [id](const Note3D& q) { return q.id == id; });
                if (it == notes.end()) return;
                if (noteDragMoved) { noteDragMoved = false; saveNotes(); return; }
                openNoteEditor((int)(it - notes.begin()));
            };
        }
    }
    // where a click would put the next note
    if (!capture && noting && hover3d.ok && !U.overWidget(lastX, lastY)) {
        float hx, hy;
        if (mv.project(hover3d.world, rw(pane), rh(pane), hx, hy)) {
            hx += pane.left;
            hy += pane.top;
            const D2D1_COLOR_F c = rgba(kNoteColors[notes.size() % 6]);
            G.circle(hx, hy, 7, darkHover);
            G.circle(hx, hy, 5.5f, c);
            static const char* snapKey[3] = {"meas.surface", "meas.vertex", "meas.edge"};
            std::wstring t = std::wstring(T("note.here")) + L" · " + T(snapKey[std::clamp(hover3d.snap, 0, 2)]);
            float tw = G.textWidth(t, 11, 600) + 12;
            D2D1_RECT_F cr = RW(hx + 12, hy + 10, tw, 20);
            G.fill(cr, rgba(0x111214, .78f), 5);
            G.text(t, cr, 11, rgba(0xFFFFFF), 600, AC);
        }
    }
    if (!capture) dc->PopAxisAlignedClip();
}

void App::drawNoteCard() {
    if (noteEdit < 0 || noteEdit >= (int)notes.size() || mode != Mode::Model) {
        if (noteEditHwnd && IsWindowVisible(noteEditHwnd)) ShowWindow(noteEditHwnd, SW_HIDE);
        return;
    }
    auto& n = notes[noteEdit];
    const float w = 320, h = n.img ? 330 : 286;
    D2D1_RECT_F r = RW(rcViewport.left + 16, rcViewport.top + 70, w, h);
    noteCardRect = r;
    U.glassPanel(r, 14);
    U.add(hid("note.card"), r);
    const D2D1_COLOR_F c = rgba(kNoteColors[n.color % 6]);
    G.circle(r.left + 26, r.top + 25, 9, c);
    G.text(fmtf(L"%d", noteEdit + 1), RW(r.left + 17, r.top + 16, 18, 18), 10.5f, rgba(0x111214), 800, AC);
    G.text(T("note.title"), RW(r.left + 44, r.top + 10, 200, 30), 13, P.text, 600);
    U.toolButton(hid("note.close"), RW(r.right - 42, r.top + 9, 30, 30), "x", "note.done", [this] { closeNoteEditor(); }, false, true, nullptr, 15);
    // text box (a real edit control: IME, clipboard, multi-line)
    D2D1_RECT_F tb = RW(r.left + 14, r.top + 48, w - 28, 104);
    G.fill(tb, P.dark ? rgba(0x2A2A2E) : rgba(0xF2F2F5), 8);
    G.stroke(tb, P.line, 8, 1);
    D2D1_RECT_F er = R(tb.left + 9, tb.top + 7, tb.right - 9, tb.bottom - 7);
    if (er.left != noteEditRect.left || er.top != noteEditRect.top || er.right != noteEditRect.right || er.bottom != noteEditRect.bottom || !IsWindowVisible(noteEditHwnd)) {
        noteEditRect = er;
        const float s = G.scale;
        SetWindowPos(noteEditHwnd, HWND_TOP, (int)std::lround(er.left * s), (int)std::lround(er.top * s), (int)std::lround(rw(er) * s), (int)std::lround(rh(er) * s),
                     SWP_SHOWWINDOW);
        if (!IsWindowVisible(noteEditHwnd) || GetFocus() != noteEditHwnd) SetFocus(noteEditHwnd);
    }
    if (!noteEditBrush) noteEditBrush = CreateSolidBrush(cref(P.dark ? rgba(0x2A2A2E) : rgba(0xF2F2F5)));
    float y = tb.bottom + 12;
    // colour
    G.text(T("note.color"), RW(r.left + 16, y, 80, 28), 12, P.text2, 500);
    for (int k = 0; k < 6; ++k) {
        const int col = k;
        U.swatch(hid("note.col", k), r.left + 110 + k * 30, y + 14, rgba(kNoteColors[k]), n.color == k, [this, col] {
            if (noteEdit >= 0) { notes[noteEdit].color = col; saveNotes(); requestPaint(); }
        });
    }
    y += 38;
    // picture
    if (n.img) {
        if (!n.bmp) G.dc->CreateBitmapFromWicBitmap(n.img.Get(), nullptr, &n.bmp);
        UINT iw = 0, ih = 0;
        n.img->GetSize(&iw, &ih);
        float s = std::min(96.f / iw, 64.f / ih);
        if (n.bmp) G.dc->DrawBitmap(n.bmp.Get(), RW(r.left + 16, y, iw * s, ih * s), 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        U.textButton(hid("note.imgchg"), RW(r.left + 124, y, 86, 30), T("note.img.change"), [this] { noteImageFromFile(noteEdit); });
        U.textButton(hid("note.imgdel"), RW(r.left + 216, y, 88, 30), T("note.img.remove"), [this] { setNoteImage(noteEdit, nullptr); });
        y += 74;
    } else {
        U.textButton(hid("note.imgadd"), RW(r.left + 16, y, 120, 30), T("note.img.add"), [this] { noteImageFromFile(noteEdit); });
        G.text(T("note.img.paste"), RW(r.left + 146, y, w - 160, 30), 11.5f, P.text3, 500);
        y += 40;
    }
    // bottom
    float by = r.bottom - 48;
    U.textButton(hid("note.del"), RW(r.left + 16, by, 90, 34), T("note.delete"), [this] { deleteNote(noteEdit); });
    U.textButton(hid("note.ok"), RW(r.right - 106, by, 90, 34), T("note.done"), [this] { closeNoteEditor(); }, true);
}

} // namespace dv
