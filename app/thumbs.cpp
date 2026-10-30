// DeepImage Viewer — thumbnail service
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "thumbs.h"
#include "image.h"
#include <shobjidl.h>

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
        DvThumbOptions o{size, 0, 1};
        out.bgra.resize((size_t)size * size * 4);
        char err[256] = {};
        if (!ML.thumb(path.c_str(), &o, out.bgra.data(), err, sizeof err)) return false;
        out.w = out.h = size;
        return true;
    }
    const std::wstring e = extOf(path);
    const bool shellCapable = !(e == L"svg" || e == L"tga" || e == L"psd" || e == L"ppm" || e == L"pgm" || e == L"pnm" || e == L"gif");
    if (allowShell && shellCapable) {
        ComPtr<IShellItemImageFactory> f;
        if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&f)))) {
            HBITMAP hb = nullptr;
            if (SUCCEEDED(f->GetImage({(LONG)size, (LONG)size}, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &hb)) && hb) {
                bool ok = fromHBitmap(hb, out);
                DeleteObject(hb);
                if (ok) return true;
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

void ThumbService::start(HWND notify, UINT msg) {
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
        r.ok = renderThumbFile(job.path, job.kind, 160, r);
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
