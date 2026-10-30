#include <windows.h>
#include <wincodec.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../model/dvmodel.h"
static void savePng(const wchar_t* path, const uint8_t* bgra, UINT n) {
    IWICImagingFactory* f; CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    IWICStream* s; f->CreateStream(&s); s->InitializeFromFilename(path, GENERIC_WRITE);
    IWICBitmapEncoder* e; f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &e); e->Initialize(s, WICBitmapEncoderNoCache);
    IWICBitmapFrameEncode* fr; e->CreateNewFrame(&fr, nullptr); fr->Initialize(nullptr); fr->SetSize(n, n);
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppPBGRA; fr->SetPixelFormat(&pf);
    fr->WritePixels(n, n * 4, n * n * 4, (BYTE*)bgra); fr->Commit(); e->Commit();
}
int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (int i = 1; i < argc; ++i) {
        char err[512] = {};
        DWORD t0 = GetTickCount();
        DvModel* m = dvLoadModel(argv[i], err, sizeof err);
        DWORD t1 = GetTickCount();
        if (!m) { wprintf(L"FAIL load %ls : %hs\n", argv[i], err); continue; }
        auto in = dvModelInfo(m);
        int tex = 0; for (uint32_t k = 0; k < in->submeshCount; ++k) tex += in->submeshes[k].texture ? 1 : 0;
        wprintf(L"OK %ls: sub=%u tri=%llu vert=%llu mat=%u up=%d tex=%d ext=%.1fx%.1fx%.1f %lums\n", argv[i], in->submeshCount,
                (unsigned long long)in->triangleCount, (unsigned long long)in->vertexCount, in->materialCount, in->upAxis, tex,
                in->boundsMax[0]-in->boundsMin[0], in->boundsMax[1]-in->boundsMin[1], in->boundsMax[2]-in->boundsMin[2], t1 - t0);
        dvFreeModel(m);
        std::vector<uint8_t> px(256 * 256 * 4);
        DvThumbOptions o{256, 0, 1};
        if (dvRenderThumbnail(argv[i], &o, px.data(), err, sizeof err)) {
            std::wstring out = std::wstring(argv[i]) + L".thumb.png";
            savePng(out.c_str(), px.data(), 256);
        } else wprintf(L"FAIL thumb: %hs\n", err);
    }
    return 0;
}
