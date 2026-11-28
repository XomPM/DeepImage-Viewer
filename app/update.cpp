// DeepImage Viewer — update check, download and hand-over to Setup (see update.h) + the app's update UI
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "update.h"
#include "json.h"

#include "app.h"
#include "archive.h"
#include "i18n.h"

#include "settings.h"

#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <ctime>
#include <thread>

namespace dv {
namespace upd {
namespace {

constexpr const wchar_t* kLatestApi = L"https://api.github.com/repos/XomPM/DeepImage-Viewer/releases/latest";
constexpr const wchar_t* kReleasesPage = L"https://github.com/XomPM/DeepImage-Viewer/releases/latest";

// ------------------------------------------------------------------ HTTP
struct Http {
    HINTERNET s = nullptr, c = nullptr, r = nullptr;
    ~Http() { close(); }
    // true with status 200
    bool open(const std::wstring& url, const wchar_t* headers) {
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof uc;
        wchar_t host[256], path[2048];
        uc.lpszHostName = host;
        uc.dwHostNameLength = (DWORD)std::size(host);
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = (DWORD)std::size(path);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
        const std::wstring ua = std::wstring(L"DeepImageViewer/") + kVersion;
        // the system's automatic proxy settings first, then the WinHTTP default (netsh) configuration
        for (DWORD access : {(DWORD)WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, (DWORD)WINHTTP_ACCESS_TYPE_DEFAULT_PROXY}) {
            close();
            s = WinHttpOpen(ua.c_str(), access, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!s) continue;
            WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
            c = WinHttpConnect(s, host, uc.nPort, 0);
            r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
            if (!r) continue;
            DWORD code = 0, sz = sizeof code;
            if (!WinHttpSendRequest(r, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(r, nullptr))
                continue; // could not connect: try the other proxy setting
            return WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX) &&
                   code == 200;
        }
        return false;
    }
    void close() {
        if (r) WinHttpCloseHandle(r);
        if (c) WinHttpCloseHandle(c);
        if (s) WinHttpCloseHandle(s);
        r = c = s = nullptr;
    }
};

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring tempDir() {
    wchar_t t[MAX_PATH];
    GetTempPathW(MAX_PATH, t);
    return std::wstring(t) + L"DeepImage-update";
}

const wchar_t* nativeArch() {
    USHORT proc = 0, native = 0;
    if (IsWow64Process2(GetCurrentProcess(), &proc, &native) && native == IMAGE_FILE_MACHINE_ARM64) return L"arm64";
#if defined(__aarch64__) || defined(_M_ARM64)
    return L"arm64";
#else
    return L"x64";
#endif
}

} // namespace

bool supported() { return packagedAumid().empty(); }

bool installed() { return GetFileAttributesW((exeDir() + L"\\Uninstall.exe").c_str()) != INVALID_FILE_ATTRIBUTES; }

int compareVersions(const std::wstring& a, const std::wstring& b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && iswdigit(a[i])) x = x * 10 + (a[i++] - L'0');
        while (j < b.size() && iswdigit(b[j])) y = y * 10 + (b[j++] - L'0');
        if (x != y) return x < y ? -1 : 1;
        while (i < a.size() && !iswdigit(a[i])) ++i;
        while (j < b.size() && !iswdigit(b[j])) ++j;
    }
    return 0;
}

bool fetchLatest(Info& out) {
    Http h;
    if (!h.open(kLatestApi, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n")) return false;
    std::string body;
    for (;;) {
        char buf[16384];
        DWORD n = 0;
        if (!WinHttpReadData(h.r, buf, sizeof buf, &n)) return false;
        if (!n) break;
        body.append(buf, n);
        if (body.size() > (4u << 20)) return false;
    }
    json::J root;
    if (!json::parse(body, root) || root.t != json::J::Obj) return false;
    std::string tag = root.str("tag_name");
    if (tag.empty() || root.get("draft") && root.get("draft")->n) return false;
    if (tag[0] == 'v' || tag[0] == 'V') tag.erase(0, 1);
    out.version = widen(tag);
    out.page = widen(root.str("html_url"));
    if (out.page.rfind(L"https://github.com/", 0) != 0) out.page = kReleasesPage;
    const std::wstring want = std::wstring(L"-Setup-") + nativeArch() + L".exe";
    const json::J* assets = root.get("assets");
    for (int pass = 0; pass < 2 && out.url.empty(); ++pass) {
        const std::wstring suffix = pass == 0 ? want : L"-Setup-x64.exe"; // x64 runs under emulation on ARM64
        if (!assets || assets->t != json::J::Arr) break;
        for (auto& a : assets->a) {
            std::wstring name = widen(a.str("name"));
            if (name.size() < suffix.size() || _wcsicmp(name.c_str() + name.size() - suffix.size(), suffix.c_str())) continue;
            std::wstring url = widen(a.str("browser_download_url"));
            if (url.rfind(L"https://github.com/XomPM/DeepImage-Viewer/releases/download/", 0) != 0) continue;
            out.name = name;
            out.url = url;
            if (auto* sz = a.get("size")) out.size = (uint64_t)sz->n;
            std::string d = a.str("digest");
            if (d.rfind("sha256:", 0) == 0 && d.size() == 7 + 64) out.sha256 = d.substr(7);
            break;
        }
    }
    return !out.version.empty();
}

std::wstring downloadSetup(const Info& in, const std::function<void(float)>& progress, const std::function<bool()>& cancelled, std::string& err) {
    err = "upd.download";
    if (in.url.empty() || in.sha256.empty()) { err = "upd.nosetup"; return {}; }
    const std::wstring dir = tempDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring dest = dir + L"\\" + in.name;
    Http h;
    if (!h.open(in.url, nullptr)) return {};
    const std::wstring part = dest + L".part";
    HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = "upd.disk"; return {}; }
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    std::vector<char> buf(1 << 17);
    uint64_t got = 0;
    bool ok = hash != nullptr;
    while (ok) {
        if (cancelled && cancelled()) { ok = false; err = "upd.cancelled"; break; }
        DWORD n = 0;
        if (!WinHttpReadData(h.r, buf.data(), (DWORD)buf.size(), &n)) { ok = false; break; }
        if (!n) break;
        DWORD w = 0;
        if (!WriteFile(f, buf.data(), n, &w, nullptr) || w != n) { ok = false; err = "upd.disk"; break; }
        BCryptHashData(hash, (PUCHAR)buf.data(), n, 0);
        got += n;
        if (progress && in.size) progress(std::min(1.f, (float)got / in.size));
        if (got > (512ull << 20)) { ok = false; break; }
    }
    CloseHandle(f);
    std::string hex;
    if (hash) {
        uint8_t d[32]{};
        BCryptFinishHash(hash, d, 32, 0);
        static const char* hx = "0123456789abcdef";
        for (uint8_t b : d) { hex += hx[b >> 4]; hex += hx[b & 15]; }
        BCryptDestroyHash(hash);
    }
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (ok && (in.size && got != in.size || _stricmp(hex.c_str(), in.sha256.c_str()) != 0)) { ok = false; err = "upd.corrupt"; }
    if (ok && !MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) { ok = false; err = "upd.disk"; }
    if (!ok) { DeleteFileW(part.c_str()); return {}; }
    err.clear();
    return dest;
}

bool runSetup(const std::wstring& setup, const std::wstring& reopen) {
    std::wstring args = L"/update";
    if (!reopen.empty()) args += L" /relaunch \"" + reopen + L"\"";
    return (INT_PTR)ShellExecuteW(nullptr, L"open", setup.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL) > 32;
}

void cleanup() {
    const std::wstring dir = tempDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    while (FindNextFileW(h, &fd));
    FindClose(h);
    RemoveDirectoryW(dir.c_str());
}

} // namespace upd

// ====================================================================== app side
namespace {
uint32_t today() { return (uint32_t)(time(nullptr) / 86400); }
}

void App::scheduleUpdateCheck() {
    if (!upd::supported()) return;
    std::thread([] { upd::cleanup(); }).detach();
    if (S.updateCheck && S.updateLast != today()) SetTimer(hwnd, 5, 6000, nullptr); // a moment after start-up
}

void App::checkForUpdate(bool manual) {
    if (!upd::supported() || updState == 1 || updState == 4) return;
    updState = 1;
    updManual = manual;
    HWND hw = hwnd;
    std::thread([hw] {
        auto* r = new UpdResult;
        r->ok = upd::fetchLatest(r->info);
        if (!PostMessageW(hw, WM_APP_UPD, 0, (LPARAM)r)) delete r;
    }).detach();
    requestPaint();
}

void App::onUpdateMsg(WPARAM kind, LPARAM l) {
    if (kind == 1) { // download progress (per mille)
        if (updState == 4) { updProgress = (int)l / 1000.f; requestPaint(); }
        return;
    }
    std::unique_ptr<UpdResult> r((UpdResult*)l);
    if (kind == 0) {
        if (!r->ok) {
            updState = 5;
            if (updManual) toast(T("upd.checkfail"), true);
        } else {
            S.updateLast = today();
            S.save();
            if (upd::compareVersions(r->info.version, kVersion) > 0) {
                updInfo = r->info;
                updState = 3;
                updBanner = true;
                if (modal.kind == 2) closeModal(); // the banner says it
            } else {
                updState = 2;
                if (updManual) toast(T("upd.latest"));
            }
        }
    } else if (kind == 2) {
        if (r->path.empty()) {
            updState = 3;
            if (r->err != "upd.cancelled") toast(T(r->err.c_str()), true);
        } else {
            // reopen the same picture (an archive entry: the archive) once Setup is done
            std::wstring reopen;
            if (index >= 0 && index < (int)files.size()) {
                reopen = files[index].path;
                std::wstring a = arc::archiveOf(reopen);
                if (!a.empty()) reopen = a;
            }
            if (upd::runSetup(r->path, reopen)) {
                updState = 0;
                updBanner = false;
                PostMessageW(hwnd, WM_CLOSE, 0, 0); // Setup also closes any other window of the app
            } else {
                updState = 3;
                toast(T("upd.runfail"), true);
            }
        }
    }
    requestPaint();
}

void App::startUpdate() {
    if (updState != 3) return;
    if (!upd::installed() || updInfo.url.empty() || updInfo.sha256.empty()) {
        // portable copy (or a release without a Setup): the release page
        ShellExecuteW(nullptr, L"open", updInfo.page.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    guardUnsaved([this] {
        if (panel == EditPanel::Bg) closeBg(true);
        updState = 4;
        updProgress = 0;
        updCancel = std::make_shared<std::atomic<bool>>(false);
        auto cancel = updCancel;
        upd::Info info = updInfo;
        HWND hw = hwnd;
        std::thread([hw, info, cancel] {
            auto* r = new UpdResult;
            DWORD last = 0;
            r->path = upd::downloadSetup(info, [&](float p) {
                    DWORD now = GetTickCount();
                    if (now - last > 80) { last = now; PostMessageW(hw, WM_APP_UPD, 1, (LPARAM)(int)(p * 1000)); }
                }, [cancel] { return cancel->load(); }, r->err);
            if (!PostMessageW(hw, WM_APP_UPD, 2, (LPARAM)r)) delete r;
        }).detach();
        ensureTimer();
        requestPaint();
    });
}

void App::drawUpdateBar() {
    if (!updBanner || (updState != 3 && updState != 4) || modal.kind) return;
    const float w = 392, h = 92;
    // bottom right, above the filmstrip: out of the way of the toolbar
    const float bottom = (rh(rcStrip) > 0 ? rcStrip.top : G.H()) - 14;
    D2D1_RECT_F r = RW(G.W() - w - 16, bottom - h, w, h);
    if (G.W() < w + 32) r = RW(16, bottom - h, G.W() - 32, h);
    G.shadow(r, 14, 1.4f);
    G.fill(r, P.surface, 14);
    G.stroke(r, P.line, 14, 1);
    U.add(hid("upd.bar"), r);
    G.circle(r.left + 30, r.top + 30, 15, withA(P.accent, P.dark ? .25f : .14f));
    G.icon("download", r.left + 30, r.top + 30, 16, P.accent);
    G.text(fmtf(T("upd.available"), updInfo.version.c_str()), R(r.left + 56, r.top + 10, r.right - 44, r.top + 32), 13.5f, P.text, 600);
    if (updState == 4) {
        G.text(fmtf(T("upd.downloading"), (int)(updProgress * 100)), R(r.left + 56, r.top + 32, r.right - 16, r.top + 50), 12, P.text3, 500);
        D2D1_RECT_F bar = R(r.left + 56, r.top + 62, r.right - 110, r.top + 68);
        G.fill(bar, P.line, 3);
        G.fill(R(bar.left, bar.top, bar.left + rw(bar) * updProgress, bar.bottom), P.accent, 3);
        U.textButton(hid("upd.cancel"), R(r.right - 96, r.top + 48, r.right - 14, r.top + 80), T("cancel"), [this] {
            if (updCancel) updCancel->store(true);
        });
        return;
    }
    G.text(fmtf(T("upd.current"), kVersion), R(r.left + 56, r.top + 32, r.right - 16, r.top + 50), 12, P.text3, 500);
    U.toolButton(hid("upd.x"), RW(r.right - 40, r.top + 8, 30, 30), "x", nullptr, [this] { updBanner = false; requestPaint(); }, false, true, nullptr, 14);
    const bool inst = upd::installed() && !updInfo.url.empty() && !updInfo.sha256.empty();
    std::wstring go = T(inst ? "upd.install" : "upd.page");
    std::wstring notes = T("upd.notes");
    float gw = G.textWidth(go, 12.5f, 600) + 32, nw = G.textWidth(notes, 12.5f, 600) + 32;
    U.textButton(hid("upd.go"), R(r.right - 14 - gw, r.top + 50, r.right - 14, r.top + 82), go, [this] { startUpdate(); }, true);
    U.textButton(hid("upd.notes"), R(r.right - 22 - gw - nw, r.top + 50, r.right - 22 - gw, r.top + 82), notes, [this] {
        ShellExecuteW(nullptr, L"open", updInfo.page.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });
}

} // namespace dv
