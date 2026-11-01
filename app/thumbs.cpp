// DeepImage Viewer — thumbnail service
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "thumbs.h"
#include "image.h"
#include "settings.h"
#include "miniz.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <atomic>
#include <filesystem>

namespace dv {

ModelLib ML;

bool ModelLib::ensure() {
    if (mod_) return true;
    if (tried_) return false;
    tried_ = true;
    std::wstring p = exeDir() + L"\\dvmodel.dll";
    mod_ = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod_) return false;
    load = (decltype(load))GetProcAddress(mod_, "dvLoadModel");
    info = (decltype(info))GetProcAddress(mod_, "dvModelInfo");
    free = (decltype(free))GetProcAddress(mod_, "dvFreeModel");
    thumb = (decltype(thumb))GetProcAddress(mod_, "dvRenderThumbnail");
    if (!load || !info || !free || !thumb) { FreeLibrary(mod_); mod_ = nullptr; return false; }
    return true;
}

static bool fromHBitmap(HBITMAP hb, ThumbResult& out) {
    BITMAP bm{};
    if (!GetObjectW(hb, sizeof bm, &bm) || bm.bmWidth <= 0 || bm.bmHeight == 0) return false;
    UINT w = bm.bmWidth, h = std::abs(bm.bmHeight);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -(LONG)h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    out.bgra.resize((size_t)w * h * 4);
    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, hb, 0, h, out.bgra.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != (int)h) return false;
    bool anyAlpha = false;
    for (size_t i = 3; i < out.bgra.size(); i += 4) if (out.bgra[i]) { anyAlpha = true; break; }
    if (!anyAlpha) for (size_t i = 3; i < out.bgra.size(); i += 4) out.bgra[i] = 255;
    out.w = w;
    out.h = h;
    return true;
}

bool renderThumbFile(const std::wstring& path, Kind kind, UINT size, ThumbResult& out, bool allowShell) {
    out.path = path;
    if (kind == Kind::Model) {
        if (!ML.ensure()) return false;
        std::lock_guard<std::mutex> g(ML.lock);
        DvThumbOptions o{size, 0, 1, S.thumbColor ? (0xFF000000u | (S.thumbColor & 0xFFFFFF)) : 0u, S.thumbColorAll ? 1 : 0};
        out.bgra.resize((size_t)size * size * 4);
        char err[256] = {};
        if (!ML.thumb(path.c_str(), &o, out.bgra.data(), err, sizeof err)) return false;
        out.w = out.h = size;
        return true;
    }
    const std::wstring e = extOf(path);
    const bool shellCapable = !(e == L"svg" || e == L"tga" || e == L"psd" || e == L"ppm" || e == L"pgm" || e == L"pnm" || e == L"gif" ||
                                 e == L"gcode" || e == L"gco" || e == L"bgcode");
    if (allowShell && shellCapable) {
        ComPtr<IShellItemImageFactory> f;
        if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&f)))) {
            HBITMAP hb = nullptr;
            if (SUCCEEDED(f->GetImage({(LONG)size, (LONG)size}, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &hb)) && hb) {
                bool ok = fromHBitmap(hb, out);
                DeleteObject(hb);
                if (ok) { out.fromShell = true; return true; }
            }
        }
    }
    DecodedImage img;
    std::string ek;
    if (!decodeImageFile(path, img, ek, 1024) || img.frames.empty()) return false;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    float k = std::min(1.f, (float)size / std::max(img.width, img.height));
    UINT w = std::max(1u, (UINT)std::lround(img.width * k)), h = std::max(1u, (UINT)std::lround(img.height * k));
    ComPtr<IWICBitmapScaler> sc;
    if (FAILED(wic->CreateBitmapScaler(&sc)) || FAILED(sc->Initialize(img.frames[0].Get(), w, h, WICBitmapInterpolationModeFant))) return false;
    out.bgra.resize((size_t)w * h * 4);
    if (FAILED(sc->CopyPixels(nullptr, w * 4, (UINT)out.bgra.size(), out.bgra.data()))) return false;
    out.w = w;
    out.h = h;
    return true;
}

// ======================================================================== disk cache
namespace {
constexpr uint32_t kCacheMagic = 0x31545644; // "DVT1"
constexpr uint64_t kCacheLimit = 300ull << 20, kCacheTrimTo = 240ull << 20;
constexpr UINT kThumbSize = 160;
std::mutex cacheMx;

std::wstring cacheDir() {
    static std::wstring dir = [] {
        PWSTR p = nullptr;
        std::wstring d;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &p))) d = std::wstring(p) + L"\\DMIND\\DeepImage Viewer\\ThumbCache";
        CoTaskMemFree(p);
        if (!d.empty()) SHCreateDirectoryExW(nullptr, d.c_str(), nullptr);
        return d;
    }();
    return dir;
}

// empty when the file cannot be stat'ed (then we do not cache)
std::wstring cacheFile(const std::wstring& path, Kind kind) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) || cacheDir().empty()) return {};
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* d, size_t n) { for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)d)[i]; h *= 1099511628211ull; } };
    std::wstring lp = path;
    for (auto& c : lp) c = (wchar_t)towlower(c);
    mix(lp.data(), lp.size() * 2);
    mix(&fa.nFileSizeHigh, 4); mix(&fa.nFileSizeLow, 4); mix(&fa.ftLastWriteTime, 8);
    const uint32_t v = 2 * 1000 + kThumbSize; // bump when the renderer or embedded-preview rules change
    mix(&v, 4);
    if (kind == Kind::Model) { mix(&S.thumbColor, 4); uint32_t all = S.thumbColorAll; mix(&all, 4); }
    wchar_t name[40];
    swprintf(name, 40, L"\\%016llx.dvt", (unsigned long long)h);
    return cacheDir() + name;
}

bool cacheLoad(const std::wstring& file, ThumbResult& r) {
    HANDLE f = CreateFileW(file.c_str(), GENERIC_READ | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    std::vector<uint8_t> buf;
    DWORD got = 0;
    bool ok = GetFileSizeEx(f, &sz) && sz.QuadPart > 16 && sz.QuadPart < (8 << 20);
    if (ok) { buf.resize((size_t)sz.QuadPart); ok = ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr) && got == buf.size(); }
    if (ok) { FILETIME now; GetSystemTimeAsFileTime(&now); SetFileTime(f, nullptr, nullptr, &now); } // LRU touch
    CloseHandle(f);
    if (!ok) return false;
    uint32_t hdr[4];
    memcpy(hdr, buf.data(), 16);
    if (hdr[0] != kCacheMagic || !hdr[1] || !hdr[2] || hdr[1] > 2048 || hdr[2] > 2048) return false;
    mz_ulong n = (mz_ulong)hdr[1] * hdr[2] * 4;
    r.bgra.resize(n);
    if (mz_uncompress(r.bgra.data(), &n, buf.data() + 16, (mz_ulong)(buf.size() - 16)) != MZ_OK || n != r.bgra.size()) return false;
    r.w = hdr[1];
    r.h = hdr[2];
    return true;
}

void cacheStore(const std::wstring& file, const ThumbResult& r) {
    mz_ulong cap = mz_compressBound((mz_ulong)r.bgra.size());
    std::vector<uint8_t> buf(16 + cap);
    if (mz_compress2(buf.data() + 16, &cap, r.bgra.data(), (mz_ulong)r.bgra.size(), 6) != MZ_OK) return;
    uint32_t hdr[4] = {kCacheMagic, r.w, r.h, 0};
    memcpy(buf.data(), hdr, 16);
    buf.resize(16 + cap);
    std::wstring tmp = file + L".tmp" + std::to_wstring(GetCurrentThreadId());
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD put = 0;
    bool ok = WriteFile(f, buf.data(), (DWORD)buf.size(), &put, nullptr) && put == buf.size();
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(tmp.c_str());
}

// keep the cache bounded: drop least recently used entries
void cacheTrim() {
    std::lock_guard<std::mutex> g(cacheMx);
    std::error_code ec;
    struct E { std::filesystem::path p; uint64_t size; std::filesystem::file_time_type t; };
    std::vector<E> list;
    uint64_t total = 0;
    for (auto& e : std::filesystem::directory_iterator(cacheDir(), ec)) {
        if (!e.is_regular_file(ec)) continue;
        uint64_t s = e.file_size(ec);
        list.push_back({e.path(), s, e.last_write_time(ec)});
        total += s;
    }
    if (total <= kCacheLimit) return;
    std::sort(list.begin(), list.end(), [](const E& a, const E& b) { return a.t < b.t; });
    for (auto& e : list) {
        if (total <= kCacheTrimTo) break;
        if (std::filesystem::remove(e.p, ec)) total -= e.size;
    }
}
} // namespace

uint64_t thumbCacheBytes() {
    std::error_code ec;
    uint64_t total = 0;
    for (auto& e : std::filesystem::directory_iterator(cacheDir(), ec))
        if (e.is_regular_file(ec)) total += e.file_size(ec);
    return total;
}

void clearThumbCache() {
    std::lock_guard<std::mutex> g(cacheMx);
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(cacheDir(), ec)) std::filesystem::remove(e.path(), ec);
}

void ThumbService::start(HWND notify, UINT msg) {
    std::thread([] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        Sleep(4000);
        cacheTrim();
    }).detach();
    hwnd_ = notify;
    msg_ = msg;
    stop_ = false;
    for (int i = 0; i < 2; ++i)
        threads_.emplace_back([this] {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            run();
            CoUninitialize();
        });
}

void ThumbService::stop() {
    {
        std::lock_guard<std::mutex> g(m_);
        stop_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    for (auto& t : threads_) if (t.joinable()) t.join();
    threads_.clear();
}

void ThumbService::request(const std::wstring& path, Kind kind, int prio) {
    {
        std::lock_guard<std::mutex> g(m_);
        for (auto& j : queue_) if (j.path == path) { j.prio = std::max(j.prio, prio); return; }
        queue_.push_back({path, kind, prio});
    }
    cv_.notify_one();
}

void ThumbService::reprioritize(const std::function<int(const std::wstring&)>& prio) {
    std::lock_guard<std::mutex> g(m_);
    for (auto& j : queue_) j.prio = prio(j.path);
}

void ThumbService::cancelAll() {
    std::lock_guard<std::mutex> g(m_);
    queue_.clear();
}

bool ThumbService::take(ThumbResult& out) {
    std::lock_guard<std::mutex> g(m_);
    if (done_.empty()) return false;
    out = std::move(done_.front());
    done_.pop_front();
    return true;
}

void ThumbService::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> g(m_);
            cv_.wait(g, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            auto it = std::max_element(queue_.begin(), queue_.end(), [](const Job& a, const Job& b) { return a.prio < b.prio; });
            job = *it;
            queue_.erase(it);
        }
        ThumbResult r;
        // 1) our disk cache  2) Windows' cache for ordinary photos  3) embedded preview / our own render
        const std::wstring cf = cacheFile(job.path, job.kind);
        if (!cf.empty() && cacheLoad(cf, r)) r.ok = true;
        else {
            r = ThumbResult{};
            r.ok = renderThumbFile(job.path, job.kind, kThumbSize, r);
            if (r.ok && !r.fromShell && !cf.empty() && r.w && r.bgra.size() == (size_t)r.w * r.h * 4) cacheStore(cf, r);
        }
        r.path = job.path;
        {
            std::lock_guard<std::mutex> g(m_);
            if (stop_) return;
            done_.push_back(std::move(r));
        }
        PostMessageW(hwnd_, msg_, 0, 0);
    }
}

} // namespace dv
