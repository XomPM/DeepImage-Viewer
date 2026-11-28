// DeepImage Viewer — pasting a picture from the clipboard: into the current picture or as a new one
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "app.h"
#include "edit.h"
#include "i18n.h"
#include "settings.h"

#include <shlobj.h>

namespace dv {

namespace {
ComPtr<IWICBitmap> toPbgra(IWICBitmapSource* src) {
    ComPtr<IWICFormatConverter> cv;
    ComPtr<IWICBitmap> out;
    if (!src || FAILED(G.wic->CreateFormatConverter(&cv)) ||
        FAILED(cv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(G.wic->CreateBitmapFromSource(cv.Get(), WICBitmapCacheOnLoad, &out)))
        return nullptr;
    return out;
}

// "PNG" (browsers, Office, Snipping Tool: keeps transparency), else the bitmap
ComPtr<IWICBitmap> clipboardImage(HWND h) {
    const UINT png = RegisterClipboardFormatW(L"PNG");
    if (!IsClipboardFormatAvailable(png) && !IsClipboardFormatAvailable(CF_BITMAP) && !IsClipboardFormatAvailable(CF_DIB)) return nullptr;
    if (!OpenClipboard(h)) return nullptr;
    ComPtr<IWICBitmap> out;
    if (IsClipboardFormatAvailable(png)) {
        if (HANDLE d = GetClipboardData(png)) {
            const SIZE_T n = GlobalSize(d);
            if (const void* p = GlobalLock(d)) {
                ComPtr<IWICStream> st;
                ComPtr<IWICBitmapDecoder> dec;
                ComPtr<IWICBitmapFrameDecode> fr;
                std::string copy((const char*)p, n);
                GlobalUnlock(d);
                if (SUCCEEDED(G.wic->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)copy.data(), (DWORD)copy.size())) &&
                    SUCCEEDED(G.wic->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)))
                    out = toPbgra(fr.Get());
            }
        }
    }
    if (!out) {
        if (HBITMAP hb = (HBITMAP)GetClipboardData(CF_BITMAP)) {
            ComPtr<IWICBitmap> b;
            if (SUCCEEDED(G.wic->CreateBitmapFromHBITMAP(hb, nullptr, WICBitmapIgnoreAlpha, &b))) out = toPbgra(b.Get());
        }
    }
    CloseClipboard();
    return out;
}
} // namespace

void App::pasteImage() {
    if (modal.kind) return;
    auto img = clipboardImage(hwnd);
    if (!img) { toast(T("paste.none"), true); return; }
    if (mode == Mode::Image && doc.loaded() && panel != EditPanel::Bg && panel != EditPanel::Crop) {
        confirm(T("paste.title"), T("paste.msg"),
                {{T("cancel"), [this] { closeModal(); requestPaint(); }},
                 {T("paste.new"), [this, img] { closeModal(); pasteAsNew(img); }},
                 {T("paste.into"), [this, img] { closeModal(); pasteIntoCurrent(img); }, true}});
    } else pasteAsNew(img);
    requestPaint();
}

void App::pasteIntoCurrent(ComPtr<IWICBitmap> img) {
    if (!img || mode != Mode::Image || !doc.loaded()) return;
    if (compare) toggleCompare(0);
    if (editMode) toggleEdit(0);
    if (!annot) toggleAnnot(1);
    if (noting) setTool(Tool::Hand);
    UINT w = 0, h = 0;
    img->GetSize(&w, &h);
    // as big as it is on screen, but never bigger than 70% of the picture
    const float dw = (float)doc.W(), dh = (float)doc.H();
    float s = 1.f / std::max(1e-3f, view.s);
    s = std::min({s, .7f * dw / w, .7f * dh / h});
    auto c = view.toImage((rcViewport.left + rcViewport.right) / 2, (rcViewport.top + rcViewport.bottom) / 2);
    c.x = clampf(c.x, 0, dw);
    c.y = clampf(c.y, 0, dh);
    markup.addImage(img, c, w * s, h * s);
    toast(T("paste.placed"));
    requestPaint();
}

void App::pasteAsNew(ComPtr<IWICBitmap> img) {
    if (!img) return;
    guardUnsaved([this, img] {
        // a real file, so it can be marked up, saved and found again: Pictures\DeepImage Viewer
        PWSTR pics = nullptr;
        std::wstring dir;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pics))) dir = std::wstring(pics) + L"\\DeepImage Viewer";
        CoTaskMemFree(pics);
        if (dir.empty()) { toast(T("save.failed"), true); return; }
        CreateDirectoryW(dir.c_str(), nullptr);
        SYSTEMTIME t;
        GetLocalTime(&t);
        std::wstring out = dir + fmtf(L"\\clipboard_%04d%02d%02d-%02d%02d%02d.png", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        for (int n = 2; GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES && n < 100; ++n)
            out = dir + fmtf(L"\\clipboard_%04d%02d%02d-%02d%02d%02d-%d.png", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, n);
        if (!saveBitmap(out, GUID_ContainerFormatPng, img.Get(), 1, L"", false)) { toast(T("save.failed"), true); return; }
        if (annot) toggleAnnot(0);
        if (editMode) toggleEdit(0);
        startMarkup = true; // straight to markup once it is open
        openPath(out);
        toast(fmtf(T("paste.newdone"), fileName(out).c_str()));
    });
}

} // namespace dv
