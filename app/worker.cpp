// DeepImage Viewer — headless thumbnail worker used by the Explorer bridge
//   DeepImage.exe --thumbnail <input> <output.png> <size>
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "common.h"
#include "thumbs.h"

namespace dv {

static bool writePng(const std::wstring& out, const ThumbResult& r) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmapFromMemory(r.w, r.h, GUID_WICPixelFormat32bppPBGRA, r.w * 4, (UINT)r.bgra.size(), (BYTE*)r.bgra.data(), &bmp))) return false;
    ComPtr<IWICStream> st;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> fr;
    if (FAILED(wic->CreateStream(&st)) || FAILED(st->InitializeFromFilename(out.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) || FAILED(enc->Initialize(st.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(enc->CreateNewFrame(&fr, nullptr)) || FAILED(fr->Initialize(nullptr)))
        return false;
    fr->SetSize(r.w, r.h);
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppPBGRA;
    fr->SetPixelFormat(&pf);
    ComPtr<IWICBitmapSource> src = bmp;
    if (pf != GUID_WICPixelFormat32bppPBGRA) {
        ComPtr<IWICFormatConverter> c;
        wic->CreateFormatConverter(&c);
        c->Initialize(bmp.Get(), pf, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
        src = c;
    }
    return SUCCEEDED(fr->WriteSource(src.Get(), nullptr)) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
}

int runWorker(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc < 5) return 2;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const std::wstring in = argv[2], out = argv[3];
    UINT size = (UINT)std::wcstoul(argv[4], nullptr, 10);
    size = std::clamp(size, 32u, 1024u);
    // Explorer may hand us a temp copy without the original extension; the bridge passes "--ext .stl"
    std::wstring ext = extOf(in);
    for (int i = 5; i + 1 < argc; ++i) if (!wcscmp(argv[i], L"--ext")) ext = lower(argv[i + 1][0] == L'.' ? argv[i + 1] + 1 : argv[i + 1]);
    Kind k = kindOfExt(ext);
    if (k == Kind::None) return 3;
    ThumbResult r;
    if (!renderThumbFile(in, k, size, r, false) || !r.w) return 4;
    return writePng(out, r) ? 0 : 5;
}

} // namespace dv
