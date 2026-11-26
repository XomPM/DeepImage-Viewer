// DeepImage Viewer — AI background removal (BiRefNet via ONNX Runtime, DirectML with CPU fallback)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "bgremove.h"

#include <bcrypt.h>
#include <shlobj.h>
#include <winhttp.h>

#include <cmath>
#include <mutex>

#include "onnxruntime_c_api.h"

// Only the first entry of OrtDmlApi is needed (dml_provider_factory.h pulls in DirectML.h,
// whose SAL annotations MinGW lacks). Layout from onnxruntime 1.24 dml_provider_factory.h.
struct OrtDmlApi {
    ORT_API2_STATUS(SessionOptionsAppendExecutionProvider_DML, _In_ OrtSessionOptions* options, int device_id);
};

namespace dv::bg {
namespace {

// WICBitmapInterpolationModeHighQualityCubic (Windows 10+); missing from the MinGW headers
const WICBitmapInterpolationMode kHQCubic = (WICBitmapInterpolationMode)4;

// BiRefNet general "lite" (Swin-T backbone), MIT licence, ONNX export as distributed with rembg
const char* kModelSha256 = "5600024376f572a557870a5eb0afb1e5961636bef4e1e22132025467d0f03333";
const wchar_t* kModelUrls[] = {
    L"https://github.com/XomPM/DeepImage-Viewer/releases/download/models-v1/birefnet-lite.onnx",
    L"https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx",
};

std::wstring exeDir() {
    wchar_t p[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, p, (DWORD)std::size(p));
    return dirName(std::wstring(p, n));
}

struct Ort {
    std::mutex m;
    HMODULE dll = nullptr;
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSession* sess = nullptr;
    bool gpu = false, triedGpu = false;
    std::string inName, outName;
    int64_t side = 1024;
} g;

bool ok(OrtStatus* s, std::string* msg = nullptr) {
    if (!s) return true;
    if (msg) *msg = g.api->GetErrorMessage(s);
    g.api->ReleaseStatus(s);
    return false;
}

bool loadRuntime(std::string& err) {
    if (g.api) return true;
    const std::wstring d = exeDir();
    // ours, not the older onnxruntime.dll Windows keeps in System32
    LoadLibraryExW((d + L"\\DirectML.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    g.dll = LoadLibraryExW((d + L"\\onnxruntime.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g.dll) { err = "bg.noruntime"; return false; }
    using GetBase = const OrtApiBase*(ORT_API_CALL*)(void);
    auto base = (GetBase)(void*)GetProcAddress(g.dll, "OrtGetApiBase");
    if (!base || !(g.api = base()->GetApi(ORT_API_VERSION))) { err = "bg.noruntime"; return false; }
    if (!ok(g.api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "DeepImage", &g.env))) { g.api = nullptr; err = "bg.noruntime"; return false; }
    return true;
}

void dropSession() {
    if (g.sess) g.api->ReleaseSession(g.sess);
    g.sess = nullptr;
}

bool createSession(bool gpu, std::string& err) {
    dropSession();
    const OrtApi* a = g.api;
    OrtSessionOptions* o = nullptr;
    if (!ok(a->CreateSessionOptions(&o), &err)) return false;
    a->SetSessionGraphOptimizationLevel(o, ORT_ENABLE_ALL);
    bool fine = true;
    if (gpu) {
        // DirectML: any DirectX 12 GPU (Intel, AMD, NVIDIA, Qualcomm)
        a->DisableMemPattern(o);
        a->SetSessionExecutionMode(o, ORT_SEQUENTIAL);
        const OrtDmlApi* dml = nullptr;
        fine = ok(a->GetExecutionProviderApi("DML", ORT_API_VERSION, (const void**)&dml)) && dml && ok(dml->SessionOptionsAppendExecutionProvider_DML(o, 0));
    } else {
        a->SetIntraOpNumThreads(o, 0); // all cores
        // keep peak memory down on the CPU: no arena, no pre-planned buffers
        a->DisableCpuMemArena(o);
        a->DisableMemPattern(o);
    }
    if (fine) fine = ok(a->CreateSession(g.env, modelPath().c_str(), o, &g.sess), &err);
    a->ReleaseSessionOptions(o);
    if (!fine) { g.sess = nullptr; return false; }
    g.gpu = gpu;
    // names and the square input size the model was exported with
    OrtAllocator* al = nullptr;
    a->GetAllocatorWithDefaultOptions(&al);
    char* nm = nullptr;
    if (ok(a->SessionGetInputName(g.sess, 0, al, &nm))) { g.inName = nm; a->AllocatorFree(al, nm); }
    if (ok(a->SessionGetOutputName(g.sess, 0, al, &nm))) { g.outName = nm; a->AllocatorFree(al, nm); }
    OrtTypeInfo* ti = nullptr;
    if (ok(a->SessionGetInputTypeInfo(g.sess, 0, &ti))) {
        const OrtTensorTypeAndShapeInfo* ts = nullptr;
        size_t n = 0;
        if (ok(a->CastTypeInfoToTensorInfo(ti, &ts)) && ts && ok(a->GetDimensionsCount(ts, &n)) && n == 4) {
            int64_t dims[4]{};
            a->GetDimensions(ts, dims, 4);
            if (dims[2] > 0) g.side = dims[2];
        }
        a->ReleaseTypeInfo(ti);
    }
    return !g.inName.empty() && !g.outName.empty();
}

// A DirectX 12 device on the default adapter is required for DirectML; check before ORT tries
bool haveD3D12() {
    HMODULE d = LoadLibraryW(L"d3d12.dll");
    if (!d) return false;
    using Create = HRESULT(WINAPI*)(IUnknown*, int, REFIID, void**);
    auto f = (Create)(void*)GetProcAddress(d, "D3D12CreateDevice");
    // D3D_FEATURE_LEVEL_11_0 = 0xb000; null riid/out only tests support
    static const GUID iidDevice = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
    bool ok = f && SUCCEEDED(f(nullptr, 0xb000, iidDevice, nullptr));
    return ok;
}

// a crash inside a GPU driver must not take the app down twice: a marker file is written while the
// GPU session is being created and removed afterwards; if it is still there, the GPU is skipped
std::wstring gpuMarker() { return dirName(modelPath()) + L"\gpu-trial"; }

bool ensureSession(std::string& err) {
    if (!loadRuntime(err)) return false;
    if (g.sess) return true;
    std::string e;
    if (!g.triedGpu) {
        g.triedGpu = true;
        const std::wstring mark = gpuMarker();
        const bool crashedBefore = GetFileAttributesW(mark.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (!crashedBefore && haveD3D12() && !_wgetenv(L"DEEPIMAGE_BG_CPU")) {
            HANDLE h = CreateFileW(mark.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            const bool ok = createSession(true, e);
            DeleteFileW(mark.c_str());
            if (ok) return true;
        }
    }
    if (createSession(false, e)) return true;
    err = "bg.failed";
    return false;
}

// model output (logits) for a side x side RGB input
bool infer(const std::vector<float>& input, std::vector<float>& mask, std::string& err) {
    const OrtApi* a = g.api;
    OrtMemoryInfo* mi = nullptr;
    if (!ok(a->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mi), &err)) return false;
    int64_t shape[4] = {1, 3, g.side, g.side};
    OrtValue* in = nullptr;
    bool fine = ok(a->CreateTensorWithDataAsOrtValue(mi, (void*)input.data(), input.size() * sizeof(float), shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in), &err);
    a->ReleaseMemoryInfo(mi);
    if (!fine) return false;
    const char* inN[] = {g.inName.c_str()};
    const char* outN[] = {g.outName.c_str()};
    OrtValue* out = nullptr;
    fine = ok(a->Run(g.sess, nullptr, inN, (const OrtValue* const*)&in, 1, outN, 1, &out), &err);
    a->ReleaseValue(in);
    if (!fine) return false;
    float* od = nullptr;
    OrtTensorTypeAndShapeInfo* ts = nullptr;
    size_t count = 0;
    fine = ok(a->GetTensorMutableData(out, (void**)&od), &err) && ok(a->GetTensorTypeAndShape(out, &ts), &err) && ok(a->GetTensorShapeElementCount(ts, &count), &err);
    if (ts) a->ReleaseTensorTypeAndShapeInfo(ts);
    if (fine && count >= (size_t)(g.side * g.side)) mask.assign(od, od + g.side * g.side); // first channel of the first output
    else fine = false;
    a->ReleaseValue(out);
    return fine;
}

std::string hex(const uint8_t* d, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += h[d[i] >> 4]; s += h[d[i] & 15]; }
    return s;
}

struct Sha256 {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    }
    ~Sha256() {
        if (h) BCryptDestroyHash(h);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    void add(const void* d, ULONG n) { if (h) BCryptHashData(h, (PUCHAR)d, n, 0); }
    std::string done() {
        uint8_t out[32]{};
        if (h) BCryptFinishHash(h, out, 32, 0);
        return hex(out, 32);
    }
};

// one URL; true when the file arrived complete and verified
bool fetch(const wchar_t* url, const std::wstring& dest, const std::function<void(float)>& progress, const std::function<bool()>& cancelled,
           std::string& err) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[2048];
    uc.lpszHostName = host;
    uc.dwHostNameLength = (DWORD)std::size(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = (DWORD)std::size(path);
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return false;
    HINTERNET s = WinHttpOpen(L"DeepImageViewer", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"DeepImageViewer", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { err = "bg.download"; return false; }
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool fine = false;
    DWORD code = 0, sz = sizeof code;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr) &&
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX) && code == 200) {
        const std::wstring part = dest + L".part";
        HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            Sha256 sha;
            std::vector<char> buf(1 << 18);
            uint64_t got = 0;
            bool io = true;
            for (;;) {
                if (cancelled && cancelled()) { io = false; err = "bg.cancelled"; break; }
                DWORD n = 0;
                if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &n)) { io = false; err = "bg.download"; break; }
                if (n == 0) break;
                DWORD w = 0;
                if (!WriteFile(f, buf.data(), n, &w, nullptr) || w != n) { io = false; err = "bg.disk"; break; }
                sha.add(buf.data(), n);
                got += n;
                if (progress) progress(std::min(1.f, (float)got / kModelBytes));
            }
            CloseHandle(f);
            if (io && got == kModelBytes && sha.done() == kModelSha256) fine = MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
            else if (io) err = "bg.corrupt";
            if (!fine) DeleteFileW(part.c_str());
        } else err = "bg.disk";
    } else if (err.empty()) err = "bg.download";
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return fine;
}

} // namespace

std::wstring modelPath() {
    PWSTR p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) dir = std::wstring(p) + L"\\DMIND\\DeepImage Viewer\\Models";
    CoTaskMemFree(p);
    return dir + L"\\birefnet-lite.onnx";
}

bool runtimeAvailable() {
    return GetFileAttributesW((exeDir() + L"\\onnxruntime.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool modelReady() {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    return GetFileAttributesExW(modelPath().c_str(), GetFileExInfoStandard, &fa) && (((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow) == kModelBytes;
}

bool downloadModel(const std::function<void(float)>& progress, const std::function<bool()>& cancelled, std::string& err) {
    const std::wstring dest = modelPath();
    SHCreateDirectoryExW(nullptr, dirName(dest).c_str(), nullptr);
    for (const wchar_t* u : kModelUrls) {
        err.clear();
        if (fetch(u, dest, progress, cancelled, err)) return true;
        if (err == "bg.cancelled" || err == "bg.disk") return false;
    }
    if (err.empty()) err = "bg.download";
    return false;
}

ComPtr<IWICBitmap> removeBackground(IWICBitmap* src, std::string& err, std::wstring& device) {
    std::lock_guard<std::mutex> lk(g.m);
    if (!src) { err = "bg.failed"; return nullptr; }
    if (!modelReady()) { err = "bg.nomodel"; return nullptr; }
    if (!ensureSession(err)) return nullptr;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) { err = "bg.failed"; return nullptr; }
    UINT W = 0, H = 0;
    src->GetSize(&W, &H);
    const UINT S = (UINT)g.side;
    // 1. model input: S x S, RGB, ImageNet normalisation, planar
    ComPtr<IWICBitmapScaler> sc;
    std::vector<uint8_t> small((size_t)S * S * 4);
    if (FAILED(wic->CreateBitmapScaler(&sc)) || FAILED(sc->Initialize(src, S, S, kHQCubic)) ||
        FAILED(sc->CopyPixels(nullptr, S * 4, (UINT)small.size(), small.data()))) { err = "bg.failed"; return nullptr; }
    std::vector<float> input((size_t)3 * S * S);
    const float mean[3] = {0.485f, 0.456f, 0.406f}, stdv[3] = {0.229f, 0.224f, 0.225f};
    const size_t plane = (size_t)S * S;
    for (size_t i = 0; i < plane; ++i) {
        const uint8_t* p = &small[i * 4];
        float a = p[3] ? p[3] / 255.f : 1.f; // un-premultiply
        float rgb[3] = {p[2] / 255.f / a, p[1] / 255.f / a, p[0] / 255.f / a};
        for (int c = 0; c < 3; ++c) input[c * plane + i] = (std::min(1.f, rgb[c]) - mean[c]) / stdv[c];
    }
    // 2. run (GPU first; if the GPU fails at run time, once more on the CPU)
    std::vector<float> logits;
    std::string e;
    if (!infer(input, logits, e)) {
        if (!g.gpu || !createSession(false, e) || !infer(input, logits, e)) { err = "bg.failed"; return nullptr; }
    }
    device = g.gpu ? L"GPU" : L"CPU";
    // 3. mask: sigmoid, scaled back to the picture with a smooth filter
    std::vector<uint8_t> m8(plane);
    for (size_t i = 0; i < plane; ++i) m8[i] = (uint8_t)std::lround(255.f / (1.f + std::exp(-logits[i])));
    ComPtr<IWICBitmap> mbmp;
    ComPtr<IWICBitmapScaler> ms;
    std::vector<uint8_t> mask((size_t)W * H);
    if (FAILED(wic->CreateBitmapFromMemory(S, S, GUID_WICPixelFormat8bppGray, S, (UINT)m8.size(), m8.data(), &mbmp)) || FAILED(wic->CreateBitmapScaler(&ms)) ||
        FAILED(ms->Initialize(mbmp.Get(), W, H, kHQCubic)) || FAILED(ms->CopyPixels(nullptr, W, (UINT)mask.size(), mask.data()))) {
        err = "bg.failed";
        return nullptr;
    }
    // 4. cut-out: premultiplied pixels times the mask
    std::vector<uint8_t> px((size_t)W * H * 4);
    if (FAILED(src->CopyPixels(nullptr, W * 4, (UINT)px.size(), px.data()))) { err = "bg.failed"; return nullptr; }
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        const uint32_t k = mask[i];
        if (k == 255) continue;
        uint8_t* p = &px[i * 4];
        for (int c = 0; c < 4; ++c) p[c] = (uint8_t)((p[c] * k + 127) / 255);
    }
    ComPtr<IWICBitmap> out;
    if (FAILED(wic->CreateBitmapFromMemory(W, H, GUID_WICPixelFormat32bppPBGRA, W * 4, (UINT)px.size(), px.data(), &out))) { err = "bg.failed"; return nullptr; }
    return out;
}

} // namespace dv::bg
