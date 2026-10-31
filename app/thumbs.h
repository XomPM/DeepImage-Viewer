// DeepImage Viewer — thumbnails (nomacs DkThumbLoader equivalent) + model engine loader
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#include "common.h"
#include "../model/dvmodel.h"
#include <mutex>
#include <thread>
#include <condition_variable>
#include <deque>

namespace dv {

// Lazily loaded dvmodel.dll
struct ModelLib {
    decltype(&dvLoadModel) load = nullptr;
    decltype(&dvModelInfo) info = nullptr;
    decltype(&dvFreeModel) free = nullptr;
    decltype(&dvRenderThumbnail) thumb = nullptr;
    bool ensure();
    std::mutex lock;   // OpenCascade / Assimp are used from one thread at a time
private:
    HMODULE mod_ = nullptr;
    bool tried_ = false;
};
extern ModelLib ML;

struct ThumbResult { std::wstring path; UINT w = 0, h = 0; std::vector<uint8_t> bgra; bool ok = false; bool fromShell = false; };

// Persistent thumbnail cache (%LOCALAPPDATA%\DMIND\DeepImage Viewer\ThumbCache). Entries are keyed by
// path + size + modification time, so an edited file gets a fresh thumbnail automatically.
uint64_t thumbCacheBytes();
void clearThumbCache();

class ThumbService {
public:
    void start(HWND notify, UINT msg);
    void stop();
    void request(const std::wstring& path, Kind kind, int priority);
    void reprioritize(const std::function<int(const std::wstring&)>& prio);
    bool take(ThumbResult& out);   // UI thread: fetch a finished result
    void cancelAll();
private:
    struct Job { std::wstring path; Kind kind; int prio; };
    void run();
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<Job> queue_;
    std::deque<ThumbResult> done_;
    bool stop_ = false;
    HWND hwnd_ = nullptr;
    UINT msg_ = 0;
};

bool renderThumbFile(const std::wstring& path, Kind kind, UINT size, ThumbResult& out, bool allowShell = true);

} // namespace dv
