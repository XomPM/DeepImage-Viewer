// DeepImage Viewer — entry point
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "app.h"
#include "i18n.h"
#include "settings.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <shobjidl.h>

namespace dv {
int runWorker(int argc, wchar_t** argv);

static ComPtr<ID2D1Bitmap1> loadMark(HINSTANCE inst) {
    HRSRC r = FindResourceW(inst, MAKEINTRESOURCEW(10), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!r) return nullptr;
    HGLOBAL g = LoadResource(inst, r);
    const void* data = LockResource(g);
    DWORD size = SizeofResource(inst, r);
    ComPtr<IStream> st;
    st.Attach(SHCreateMemStream((const BYTE*)data, size));
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> f;
    ComPtr<IWICFormatConverter> conv;
    if (!st || FAILED(G.wic->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec)) || FAILED(dec->GetFrame(0, &f)) ||
        FAILED(G.wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(f.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return nullptr;
    ComPtr<ID2D1Bitmap1> b;
    G.dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &b);
    return b;
}
} // namespace dv

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    using namespace dv;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2 && !wcscmp(argv[1], L"--thumbnail")) return runWorker(argc, argv);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (packagedAumid().empty()) SetCurrentProcessExplicitAppUserModelID(L"DMIND.DeepImageViewer"); // Store builds keep the package identity
    if (FAILED(OleInitialize(nullptr))) return 1;
    S.load();
    setLang(S.language == 1 ? Lang::Ko : S.language == 2 ? Lang::En : (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN ? Lang::Ko : Lang::En));
    buildPalette(S.theme == 0 ? systemDarkMode() : S.theme == 2, systemAccentRgb(), S.accent == 0);

    std::wstring file;
    bool settings = false;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--settings")) settings = true;
        else if (argv[i][0] != L'-' && file.empty()) file = argv[i];
    }
    LocalFree(argv);

    if (!A.create(inst, L"")) return 1;
    A.markBmp = loadMark(inst);
    if (!file.empty()) A.openPath(file);
    if (settings) A.openSettings();
    A.requestPaint();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    OleUninitialize();
    return 0;
}
