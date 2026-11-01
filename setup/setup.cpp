// DeepImage Viewer — installer / uninstaller (per-user, x64 + ARM64 in one file)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
//
// Layout of DeepImage-Setup.exe:  [setup stub][zip payload]["DVPAYLD1"][u64 payload offset]
// Payload:  x64/*, arm64/*, common/*  (miniz deflate)
#include "../app/common.h"
#include "../app/gfx.h"
#include "../app/ui.h"
#include "../app/i18n.h"
#include "../app/settings.h"
#include "../shell/shell_contract.h"
#include "miniz.h"

#include <dwmapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <thread>
#include <atomic>

namespace dv {
namespace {

const wchar_t* kAppId = L"DMIND.DeepImageViewer";
const wchar_t* kUninstKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DeepImageViewer";
const wchar_t* kCapKey = L"Software\\DMIND\\DeepImageViewer\\Capabilities";
const wchar_t* kBackupKey = L"Software\\DMIND\\DeepImageViewer\\InstallBackup";
const wchar_t* kThumbCat = L"{E357FCCD-A995-4576-B01F-234630154E96}";
enum : UINT { WM_PROGRESS = WM_APP + 1, WM_DONE };

bool g_ko = false;
const wchar_t* L2(const wchar_t* ko, const wchar_t* en) { return g_ko ? ko : en; }

// ---------------------------------------------------------------- registry helpers
std::wstring regGet(HKEY root, const std::wstring& key, const wchar_t* name, bool* exists = nullptr) {
    wchar_t buf[4096];
    DWORD sz = sizeof buf, type = 0;
    LSTATUS s = RegGetValueW(root, key.c_str(), name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &type, buf, &sz);
    if (exists) *exists = s == ERROR_SUCCESS;
    return s == ERROR_SUCCESS ? std::wstring(buf) : L"";
}
void regSet(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& v) {
    HKEY k;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2));
        RegCloseKey(k);
    }
}
void regSetDword(HKEY root, const std::wstring& key, const wchar_t* name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof v);
        RegCloseKey(k);
    }
}
void regSetNone(HKEY root, const std::wstring& key, const wchar_t* name) {
    HKEY k;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_NONE, nullptr, 0);
        RegCloseKey(k);
    }
}
void regDelValue(HKEY root, const std::wstring& key, const wchar_t* name) {
    HKEY k;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) { RegDeleteValueW(k, name); RegCloseKey(k); }
}
void regDelTree(HKEY root, const std::wstring& key) { RegDeleteTreeW(root, key.c_str()); RegDeleteKeyW(root, key.c_str()); }
// delete empty keys upwards (only within Software\Classes)
void regPrune(const std::wstring& key) {
    std::wstring k = key;
    while (k.size() > 17 && k.rfind(L"Software\\Classes\\", 0) == 0) {
        HKEY h;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, k.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) break;
        DWORD subs = 0, vals = 0;
        RegQueryInfoKeyW(h, nullptr, nullptr, nullptr, &subs, nullptr, nullptr, &vals, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(h);
        if (subs || vals) break;
        RegDeleteKeyW(HKEY_CURRENT_USER, k.c_str());
        k = k.substr(0, k.find_last_of(L'\\'));
    }
}
std::wstring guidStr(const GUID& g) {
    wchar_t b[64];
    StringFromGUID2(g, b, 64);
    return b;
}

// Thumbnail handler values are owned by other apps too (e.g. DMIND 3D Thumbnail). Save the previous value
// once, write ours, and restore on uninstall only if the value is still ours.
void setOwned(const std::wstring& key, const std::wstring& value) {
    bool existed = false;
    std::wstring prev = regGet(HKEY_CURRENT_USER, key, nullptr, &existed);
    std::wstring bkName = key;
    bool haveBackup = false;
    regGet(HKEY_CURRENT_USER, kBackupKey, bkName.c_str(), &haveBackup);
    if (!haveBackup) regSet(HKEY_CURRENT_USER, kBackupKey, bkName.c_str(), existed ? L"1|" + prev : L"0|");
    regSet(HKEY_CURRENT_USER, key, nullptr, value);
}
void restoreOwned(const std::wstring& key, const std::wstring& ours) {
    bool have = false;
    std::wstring bk = regGet(HKEY_CURRENT_USER, kBackupKey, key.c_str(), &have);
    std::wstring cur = regGet(HKEY_CURRENT_USER, key, nullptr);
    if (_wcsicmp(cur.c_str(), ours.c_str()) != 0) return; // changed by someone else since: leave it
    if (have && bk.size() >= 2 && bk[0] == L'1') regSet(HKEY_CURRENT_USER, key, nullptr, bk.substr(2));
    else { regDelValue(HKEY_CURRENT_USER, key, nullptr); regPrune(key); }
}

bool keyExists(HKEY root, const std::wstring& key) {
    HKEY k;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k);
    return true;
}
std::vector<std::wstring> valueNames(HKEY root, const std::wstring& key) {
    std::vector<std::wstring> out;
    HKEY k;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return out;
    wchar_t name[512];
    for (DWORD i = 0;; ++i) {
        DWORD n = (DWORD)std::size(name);
        if (RegEnumValueW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        if (n) out.emplace_back(name, n);
    }
    RegCloseKey(k);
    return out;
}
std::vector<std::wstring> splitLines(const std::wstring& s) {
    std::vector<std::wstring> out;
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find(L'\n', a);
        if (b == std::wstring::npos) b = s.size();
        if (b > a) out.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return out;
}

// Explorer looks for a thumbnail handler on the file's ProgID before the extension key. Another app's ProgID
// with a broken handler (e.g. a 32-bit-only one) or a format-specific handler on a ProgID shared by several
// extensions would hide ours, so we bind our handler on every ProgID Explorer may consult for this extension.
std::vector<std::wstring> thumbnailProgIds(const wchar_t* ext) {
    std::vector<std::wstring> out;
    auto add = [&](const std::wstring& prog, bool always) {
        if (prog.empty() || prog.rfind(L"DeepImageViewer.", 0) == 0 || prog.find(L'\\') != std::wstring::npos) return;
        if (!keyExists(HKEY_CLASSES_ROOT, prog)) return;
        for (auto& p : out) if (_wcsicmp(p.c_str(), prog.c_str()) == 0) return;
        // an empty slot is left alone so Explorer falls through to the extension key
        if (!always && !keyExists(HKEY_CLASSES_ROOT, prog + L"\\ShellEx\\" + kThumbCat)) return;
        out.push_back(prog);
    };
    const std::wstring fe = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\") + ext;
    add(regGet(HKEY_CURRENT_USER, fe + L"\\UserChoice", L"ProgId"), true);
    add(regGet(HKEY_CLASSES_ROOT, ext, nullptr), false);
    add(regGet(HKEY_LOCAL_MACHINE, std::wstring(L"Software\\Classes\\") + ext, nullptr), false);
    add(regGet(HKEY_CURRENT_USER, std::wstring(L"Software\\Classes\\") + ext, nullptr), false);
    for (auto& n : valueNames(HKEY_CLASSES_ROOT, std::wstring(ext) + L"\\OpenWithProgids")) add(n, false);
    for (auto& n : valueNames(HKEY_CURRENT_USER, fe + L"\\OpenWithProgids")) add(n, false);
    return out;
}

// ---------------------------------------------------------------- paths
std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring s;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) s = p;
    CoTaskMemFree(p);
    return s;
}
std::wstring installDir() { return knownFolder(FOLDERID_UserProgramFiles) + L"\\DeepImage Viewer"; }
std::wstring selfPath() {
    wchar_t b[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, b, (DWORD)std::size(b));
    return std::wstring(b, n);
}
const wchar_t* nativeArch() {
    USHORT proc = 0, native = 0;
    auto fn = (BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
    if (fn && fn(GetCurrentProcess(), &proc, &native) && native == IMAGE_FILE_MACHINE_ARM64) return L"arm64";
    return L"x64";
}

bool createShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& desc) {
    ComPtr<IShellLinkW> sl;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(dirName(target).c_str());
    sl->SetDescription(desc.c_str());
    sl->SetIconLocation(target.c_str(), 0);
    ComPtr<IPropertyStore> ps;
    if (SUCCEEDED(sl.As(&ps))) {
        PROPVARIANT pv;
        if (SUCCEEDED(InitPropVariantFromString(kAppId, &pv))) {
            ps->SetValue(PKEY_AppUserModel_ID, pv);
            ps->Commit();
            PropVariantClear(&pv);
        }
    }
    ComPtr<IPersistFile> pf;
    return SUCCEEDED(sl.As(&pf)) && SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
}

void closeRunningApp() {
    for (int i = 0; i < 40; ++i) {
        HWND h = FindWindowW(L"DMIND.DeepImageViewer", nullptr);
        if (!h) return;
        PostMessageW(h, WM_CLOSE, 0, 0);
        Sleep(150);
    }
}

// ---------------------------------------------------------------- payload
struct Payload {
    std::vector<uint8_t> data;
    bool load() {
        if (!readFileBytes(selfPath(), data)) return false;
        if (data.size() < 16 || memcmp(&data[data.size() - 16], "DVPAYLD1", 8) != 0) return false;
        uint64_t off;
        memcpy(&off, &data[data.size() - 8], 8);
        if (off >= data.size() - 16) return false;
        stubSize = off;
        zipBegin = data.data() + off;
        zipSize = data.size() - 16 - off;
        return true;
    }
    uint64_t stubSize = 0;
    const uint8_t* zipBegin = nullptr;
    size_t zipSize = 0;
};

// ---------------------------------------------------------------- install steps
struct Options { bool desktop = true, thumbs = true, assoc = true; };

std::vector<std::wstring> imageAssocExts() {
    return {L"jpg", L"jpeg", L"jpe", L"jfif", L"png", L"apng", L"gif", L"webp", L"bmp", L"dib", L"ico", L"tif", L"tiff", L"svg", L"avif",
            L"heic", L"heif", L"jxr", L"wdp", L"dds", L"tga", L"psd", L"ppm", L"pgm", L"pnm", L"dng", L"cr2", L"cr3", L"nef", L"arw", L"orf", L"rw2", L"raf", L"gcode", L"gco", L"bgcode"};
}
std::vector<std::wstring> modelAssocExts() {
    return {L"stl", L"3mf", L"obj", L"ply", L"gltf", L"glb", L"fbx", L"step", L"stp", L"iges", L"igs", L"brep", L"brp", L"dae", L"3ds", L"amf", L"off"};
}

void registerApp(const std::wstring& dir, const Options& o) {
    const std::wstring exe = dir + L"\\DeepImage.exe";
    const std::wstring cmd = L"\"" + exe + L"\" \"%1\"";
    HKEY H = HKEY_CURRENT_USER;
    const std::wstring C = L"Software\\Classes\\";
    // ProgIDs
    struct PI { const wchar_t* id; const wchar_t* name; int icon; } progs[] = {
        {L"DeepImageViewer.Image", L"DeepImage Viewer Image", 0}, {L"DeepImageViewer.Model", L"DeepImage Viewer 3D Model", 0}};
    for (auto& p : progs) {
        regSet(H, C + p.id, nullptr, p.name);
        regSet(H, C + p.id + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
        regSet(H, C + p.id + L"\\shell\\open\\command", nullptr, cmd);
        regSet(H, C + p.id, L"AppUserModelID", kAppId);
    }
    // Applications\DeepImage.exe (Open with)
    const std::wstring app = C + L"Applications\\DeepImage.exe";
    regSet(H, app, L"FriendlyAppName", L"DeepImage Viewer");
    regSet(H, app + L"\\shell\\open\\command", nullptr, cmd);
    regSet(H, app + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
    // Capabilities for Settings > Default apps
    regSet(H, kCapKey, L"ApplicationName", L"DeepImage Viewer");
    regSet(H, kCapKey, L"ApplicationDescription", L"Images and 3D, in one place — DMIND");
    regSet(H, kCapKey, L"ApplicationIcon", L"\"" + exe + L"\",0");
    regSet(H, L"Software\\RegisteredApplications", L"DeepImage Viewer", kCapKey);
    if (o.assoc) {
        for (auto& e : imageAssocExts()) {
            regSetNone(H, C + L"." + e + L"\\OpenWithProgids", L"DeepImageViewer.Image");
            regSetNone(H, app + L"\\SupportedTypes", (L"." + e).c_str());
            regSet(H, std::wstring(kCapKey) + L"\\FileAssociations", (L"." + e).c_str(), L"DeepImageViewer.Image");
        }
        for (auto& e : modelAssocExts()) {
            regSetNone(H, C + L"." + e + L"\\OpenWithProgids", L"DeepImageViewer.Model");
            regSetNone(H, app + L"\\SupportedTypes", (L"." + e).c_str());
            regSet(H, std::wstring(kCapKey) + L"\\FileAssociations", (L"." + e).c_str(), L"DeepImageViewer.Model");
        }
    }
    // App Paths
    regSet(H, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\DeepImage.exe", nullptr, exe);
    regSet(H, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\DeepImage.exe", L"Path", dir);
    // Explorer thumbnails
    if (o.thumbs) {
        const std::wstring dll = dir + L"\\DeepImageThumbs.dll";
        for (auto& f : thumbnail::kShellFormats) {
            const std::wstring id = guidStr(f.clsid);
            regSet(H, C + L"CLSID\\" + id, nullptr, f.name);
            regSet(H, C + L"CLSID\\" + id + L"\\InprocServer32", nullptr, dll);
            regSet(H, C + L"CLSID\\" + id + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
            regSetDword(H, C + L"CLSID\\" + id, L"DisableProcessIsolation", 1);
            for (const wchar_t* ext : {f.extension, f.extra}) {
                if (!ext) continue;
                setOwned(C + ext + L"\\ShellEx\\" + kThumbCat, id);
                setOwned(C + L"SystemFileAssociations\\" + ext + L"\\ShellEx\\" + kThumbCat, id);
                // ProgIDs Explorer checks first (user choice, other apps' types): bind ours there too
                const std::wstring bkName = L"progids|" + std::wstring(ext);
                std::wstring list = regGet(H, kBackupKey, bkName.c_str());
                auto known = splitLines(list);
                for (auto& prog : thumbnailProgIds(ext)) {
                    setOwned(C + prog + L"\\ShellEx\\" + kThumbCat, id);
                    bool seen = false;
                    for (auto& k : known) if (_wcsicmp(k.c_str(), prog.c_str()) == 0) seen = true;
                    if (!seen) { known.push_back(prog); list += (list.empty() ? L"" : L"\n") + prog; }
                }
                if (!list.empty()) regSet(H, kBackupKey, bkName.c_str(), list);
            }
        }
        regSetDword(H, L"Software\\DMIND\\DeepImageViewer", L"ThumbnailsRegistered", 1);
        // record which handler Explorer actually resolves per extension (support diagnostics)
        for (auto& f : thumbnail::kShellFormats)
            for (const wchar_t* ext : {f.extension, f.extra}) {
                if (!ext) continue;
                wchar_t res[128] = {};
                DWORD n = (DWORD)std::size(res);
                if (FAILED(AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, ASSOCSTR_SHELLEXTENSION, ext, kThumbCat, res, &n))) wcscpy_s(res, L"-");
                regSet(H, L"Software\\DMIND\\DeepImageViewer\\ThumbnailCheck", ext, res);
            }
    }
    // Uninstall entry
    const std::wstring un = dir + L"\\Uninstall.exe";
    regSet(H, kUninstKey, L"DisplayName", L"DeepImage Viewer");
    regSet(H, kUninstKey, L"DisplayVersion", kVersion);
    regSet(H, kUninstKey, L"Publisher", L"DMIND");
    regSet(H, kUninstKey, L"DisplayIcon", L"\"" + exe + L"\",0");
    regSet(H, kUninstKey, L"InstallLocation", dir);
    regSet(H, kUninstKey, L"UninstallString", L"\"" + un + L"\" /uninstall");
    regSet(H, kUninstKey, L"QuietUninstallString", L"\"" + un + L"\" /uninstall /quiet");
    regSet(H, kUninstKey, L"HelpLink", L"mailto:dmind114@gmail.com");
    regSetDword(H, kUninstKey, L"NoModify", 1);
    regSetDword(H, kUninstKey, L"NoRepair", 1);
    regSetDword(H, kUninstKey, L"EstimatedSize", 32 * 1024);
}

void unregisterApp() {
    HKEY H = HKEY_CURRENT_USER;
    const std::wstring C = L"Software\\Classes\\";
    // thumbnails (restore previous owners)
    for (auto& f : thumbnail::kShellFormats) {
        const std::wstring id = guidStr(f.clsid);
        for (const wchar_t* ext : {f.extension, f.extra}) {
            if (!ext) continue;
            restoreOwned(C + ext + L"\\ShellEx\\" + kThumbCat, id);
            restoreOwned(C + L"SystemFileAssociations\\" + ext + L"\\ShellEx\\" + kThumbCat, id);
            std::wstring prog = regGet(H, kBackupKey, (L"progid|" + std::wstring(ext)).c_str()); // 1.0.0 installs
            if (!prog.empty()) restoreOwned(C + prog + L"\\ShellEx\\" + kThumbCat, id);
            for (auto& p : splitLines(regGet(H, kBackupKey, (L"progids|" + std::wstring(ext)).c_str())))
                restoreOwned(C + p + L"\\ShellEx\\" + kThumbCat, id);
        }
        regDelTree(H, C + L"CLSID\\" + id);
    }
    for (auto& e : imageAssocExts()) { regDelValue(H, C + L"." + e + L"\\OpenWithProgids", L"DeepImageViewer.Image"); regPrune(C + L"." + e + L"\\OpenWithProgids"); }
    for (auto& e : modelAssocExts()) { regDelValue(H, C + L"." + e + L"\\OpenWithProgids", L"DeepImageViewer.Model"); regPrune(C + L"." + e + L"\\OpenWithProgids"); }
    regDelTree(H, C + L"DeepImageViewer.Image");
    regDelTree(H, C + L"DeepImageViewer.Model");
    regDelTree(H, C + L"Applications\\DeepImage.exe");
    regDelValue(H, L"Software\\RegisteredApplications", L"DeepImage Viewer");
    regDelTree(H, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\DeepImage.exe");
    regDelTree(H, kUninstKey);
    regDelTree(H, L"Software\\DMIND\\DeepImageViewer");
    regPrune(L"Software\\DMIND");
    RegDeleteKeyW(H, L"Software\\DMIND"); // only succeeds when empty
}

// ---------------------------------------------------------------- UI state
struct SetupState {
    bool uninstall = false;
    int screen = 0;           // 0 welcome/confirm, 1 progress, 2 done, 3 error
    Options opt;
    std::atomic<int> progress{0};
    std::wstring step, error;
    bool quiet = false;
    std::wstring dir;
} St;

HWND g_hwnd;
Payload g_payload;

// per-architecture installers carry one build; an x64 installer on ARM64 installs x64 (runs under emulation)
std::wstring installArch() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    std::wstring arch = nativeArch();
    if (!g_payload.zipSize) return arch;
    mz_zip_archive z{};
    if (!mz_zip_reader_init_mem(&z, g_payload.zipBegin, g_payload.zipSize, 0)) return arch;
    std::string want = (arch == L"arm64" ? "arm64" : "x64") + std::string("/DeepImage.exe");
    bool has = mz_zip_reader_locate_file(&z, want.c_str(), nullptr, 0) >= 0;
    mz_zip_reader_end(&z);
    return cached = has ? arch : L"x64";
}

void post(int pct, const wchar_t* ko, const wchar_t* en) {
    St.progress = pct;
    PostMessageW(g_hwnd, WM_PROGRESS, 0, (LPARAM) new std::wstring(L2(ko, en)));
}

bool extractAll(const std::wstring& dir) {
    mz_zip_archive z{};
    if (!mz_zip_reader_init_mem(&z, g_payload.zipBegin, g_payload.zipSize, 0)) return false;
    const std::wstring arch = installArch();
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    bool ok = true;
    for (mz_uint i = 0; i < n && ok; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory) continue;
        std::wstring name = fromUtf8(st.m_filename);
        std::wstring rel;
        if (name.rfind(arch + L"/", 0) == 0) rel = name.substr(arch.size() + 1);
        else if (name.rfind(L"common/", 0) == 0) rel = name.substr(7);
        else continue;
        for (auto& c : rel) if (c == L'/') c = L'\\';
        std::wstring target = dir + L"\\" + rel;
        SHCreateDirectoryExW(nullptr, dirName(target).c_str(), nullptr);
        // a loaded shell DLL cannot be overwritten but can be renamed out of the way
        if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(target.c_str())) {
            std::wstring old = target + L".old" + std::to_wstring(GetTickCount());
            MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&z, i, &sz, 0);
        if (!p) { ok = false; break; }
        HANDLE h = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD wr = 0;
        ok = h != INVALID_HANDLE_VALUE && WriteFile(h, p, (DWORD)sz, &wr, nullptr) && wr == sz;
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        mz_free(p);
        St.progress = 10 + (int)(70.0 * (i + 1) / n);
        PostMessageW(g_hwnd, WM_PROGRESS, 0, 0);
    }
    mz_zip_reader_end(&z);
    return ok;
}

void cleanupOld(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.old*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    while (FindNextFileW(h, &fd));
    FindClose(h);
}

void installWorker() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring dir = installDir();
    St.dir = dir;
    post(4, L"실행 중인 DeepImage를 닫는 중", L"Closing DeepImage if running");
    closeRunningApp();
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    cleanupOld(dir);
    post(10, L"파일을 복사하는 중", L"Copying files");
    if (!extractAll(dir)) {
        St.error = L2(L"파일을 설치하지 못했습니다.", L"Could not install the files.");
        PostMessageW(g_hwnd, WM_DONE, 0, 1);
        return;
    }
    // uninstaller = setup stub without the payload
    {
        HANDLE h = CreateFileW((dir + L"\\Uninstall.exe").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD wr = 0;
        if (h != INVALID_HANDLE_VALUE) { WriteFile(h, g_payload.data.data(), (DWORD)g_payload.stubSize, &wr, nullptr); CloseHandle(h); }
    }
    post(84, L"Windows에 등록하는 중", L"Registering with Windows");
    registerApp(dir, St.opt);
    post(92, L"바로가기를 만드는 중", L"Creating shortcuts");
    const std::wstring exe = dir + L"\\DeepImage.exe";
    createShortcut(knownFolder(FOLDERID_Programs) + L"\\DeepImage Viewer.lnk", exe, L"DeepImage Viewer");
    if (St.opt.desktop) createShortcut(knownFolder(FOLDERID_Desktop) + L"\\DeepImage Viewer.lnk", exe, L"DeepImage Viewer");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
    post(100, L"완료", L"Done");
    CoUninitialize();
    PostMessageW(g_hwnd, WM_DONE, 0, 0);
}

void uninstallWorker() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring dir = St.dir;
    post(10, L"DeepImage를 닫는 중", L"Closing DeepImage");
    closeRunningApp();
    post(30, L"Windows 등록을 정리하는 중", L"Removing Windows registration");
    unregisterApp();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
    post(60, L"바로가기를 지우는 중", L"Removing shortcuts");
    DeleteFileW((knownFolder(FOLDERID_Programs) + L"\\DeepImage Viewer.lnk").c_str());
    DeleteFileW((knownFolder(FOLDERID_Desktop) + L"\\DeepImage Viewer.lnk").c_str());
    post(75, L"파일을 지우는 중", L"Removing files");
    if (!dir.empty() && dir.find(L"DeepImage Viewer") != std::wstring::npos) {
        const wchar_t* files[] = {L"DeepImage.exe", L"dvmodel.dll", L"DeepImageThumbs.dll", L"Uninstall.exe", L"LICENSES.txt",
                                  L"fonts\\Pretendard-Regular.otf", L"fonts\\Pretendard-Medium.otf", L"fonts\\Pretendard-SemiBold.otf"};
        for (auto f : files) {
            std::wstring p = dir + L"\\" + f;
            if (!DeleteFileW(p.c_str()) && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
                // still loaded by Explorer: move to temp so the folder can go
                std::wstring tmp = knownFolder(FOLDERID_LocalAppData) + L"\\Temp\\DeepImage-" + std::to_wstring(GetTickCount()) + L"-" + fileName(p);
                MoveFileExW(p.c_str(), tmp.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
            }
        }
        cleanupOld(dir);
        RemoveDirectoryW((dir + L"\\fonts").c_str());
        RemoveDirectoryW(dir.c_str());
    }
    post(100, L"완료", L"Done");
    CoUninitialize();
    PostMessageW(g_hwnd, WM_DONE, 0, 0);
}

// ---------------------------------------------------------------- drawing
ComPtr<ID2D1Bitmap1> g_mark;

void loadMark() {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(10), MAKEINTRESOURCEW(10));
    if (!r) return;
    const void* data = LockResource(LoadResource(nullptr, r));
    ComPtr<IStream> st;
    st.Attach(SHCreateMemStream((const BYTE*)data, SizeofResource(nullptr, r)));
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> f;
    ComPtr<IWICFormatConverter> c;
    if (st && SUCCEEDED(G.wic->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec)) && SUCCEEDED(dec->GetFrame(0, &f)) &&
        SUCCEEDED(G.wic->CreateFormatConverter(&c)) &&
        SUCCEEDED(c->Initialize(f.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        G.dc->CreateBitmapFromWicBitmap(c.Get(), nullptr, &g_mark);
}

void checkbox(uint64_t id, float x, float y, float w, bool& v, const wchar_t* label, const wchar_t* sub) {
    D2D1_RECT_F r = RW(x, y, w, 52);
    auto& g = U.add(id, r);
    g.click = [&v] { v = !v; InvalidateRect(g_hwnd, nullptr, FALSE); };
    if (U.isHot(id)) G.fill(r, P.hover, 10);
    D2D1_RECT_F b = RW(x + 12, y + 16, 20, 20);
    if (v) {
        G.fill(b, P.accent, 6);
        G.icon("check", (b.left + b.right) / 2, (b.top + b.bottom) / 2, 14, P.accentFg, 2.5f);
    } else {
        G.stroke(b, P.line2, 6, 1.5f);
    }
    G.text(label, R(x + 44, y + 7, x + w, y + 27), 13, P.text, 500);
    G.text(sub, R(x + 44, y + 27, x + w, y + 45), 11.5f, P.text3, 400);
}

void paint() {
    if (!G.beginFrame()) return;
    U.begin();
    const float W = G.W(), H = G.H();
    G.dc->Clear(P.surface);
    // header band with glow
    D2D1_GRADIENT_STOP gs[2] = {{0, withA(P.accent, P.dark ? .22f : .14f)}, {1, withA(P.accent, 0)}};
    ComPtr<ID2D1GradientStopCollection> sc;
    G.dc->CreateGradientStopCollection(gs, 2, &sc);
    ComPtr<ID2D1RadialGradientBrush> rb;
    G.dc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(80, 70), {0, 0}, 260, 160), sc.Get(), &rb);
    G.dc->FillRectangle(R(0, 0, W, H), rb.Get());
    // close button
    D2D1_RECT_F xr = RW(W - 46, 0, 46, 40);
    {
        auto& g = U.add(hid("close"), xr);
        g.click = [] { if (St.screen != 1) PostMessageW(g_hwnd, WM_CLOSE, 0, 0); };
        bool hot = U.isHot(g.id);
        if (hot && St.screen != 1) G.fill(xr, rgba(0xC42B1C));
        D2D1_COLOR_F c = hot && St.screen != 1 ? rgba(0xFFFFFF) : P.text2;
        G.line(W - 28, 15, W - 18, 25, c);
        G.line(W - 18, 15, W - 28, 25, c);
    }
    float x = 36;
    if (g_mark) G.dc->DrawBitmap(g_mark.Get(), RW(x, 34, 64, 64), 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    G.text(L"DeepImage Viewer", R(x + 80, 40, W - 40, 70), 21, P.text, 600);
    std::wstring sub = std::wstring(St.uninstall ? L2(L"제거", L"Uninstall") : L2(L"설치", L"Setup")) + L"  ·  " + kVersion + L"  ·  " +
                       (installArch() == L"arm64" ? L"ARM64" : L"x64") + L"  ·  DMIND";
    G.text(sub, R(x + 80, 70, W - 40, 92), 12.5f, P.text3, 500);
    float y = 128;
    const float w = W - 72;
    if (St.screen == 0 && !St.uninstall) {
        G.text(L2(L"이미지와 3D 모델을 빠르고 가볍게 보는 뷰어입니다.", L"A fast, lightweight viewer for images and 3D models."),
               R(x, y, W - 36, y + 22), 13.5f, P.text2, 400);
        y += 38;
        checkbox(hid("o1"), x - 12, y, w + 12, St.opt.assoc, L2(L"'연결 프로그램' 목록에 추가", L"Add to \"Open with\""),
                 L2(L"이미지와 3D 파일을 DeepImage로 열 수 있게 합니다", L"Lets images and 3D files open in DeepImage"));
        y += 54;
        checkbox(hid("o2"), x - 12, y, w + 12, St.opt.thumbs, L2(L"탐색기 썸네일 (3D · SVG · PSD · TGA)", L"Explorer thumbnails (3D · SVG · PSD · TGA)"),
                 L2(L"STL, 3MF, STEP, OBJ, FBX 등의 모양을 탐색기에 표시합니다", L"Shows STL, 3MF, STEP, OBJ, FBX… shapes in Explorer"));
        y += 54;
        checkbox(hid("o3"), x - 12, y, w + 12, St.opt.desktop, L2(L"바탕화면 바로가기", L"Desktop shortcut"),
                 L2(L"시작 메뉴에는 항상 추가됩니다", L"A Start menu entry is always added"));
        y += 66;
        G.text(installDir(), R(x, y, W - 36, y + 18), 11, P.text3, 400);
        float bw = 120;
        U.textButton(hid("install"), RW(W - 36 - bw, H - 64, bw, 40), L2(L"설치", L"Install"), [] {
            St.screen = 1;
            std::thread(installWorker).detach();
            InvalidateRect(g_hwnd, nullptr, FALSE);
        }, true, "download");
        U.textButton(hid("cancel"), RW(W - 36 - bw - 10 - 96, H - 64, 96, 40), L2(L"취소", L"Cancel"), [] { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); });
    } else if (St.screen == 0 && St.uninstall) {
        G.text(L2(L"DeepImage Viewer를 이 PC에서 제거할까요?", L"Remove DeepImage Viewer from this PC?"), R(x, y, W - 36, y + 24), 15, P.text, 600);
        y += 34;
        G.text(L2(L"프로그램 파일, 바로가기, 파일 연결과 탐색기 썸네일 등록을 지웁니다. 이전에 쓰던 썸네일 처리기(예: 3D Thumbnail)가 있었다면 원래대로 돌려 놓습니다.",
                  L"Removes program files, shortcuts, file associations and Explorer thumbnail registration. Any thumbnail handler that was there before (e.g. 3D Thumbnail) is restored."),
               R(x, y, W - 36, y + 80), 13, P.text2, 400, AL, true, false);
        float bw = 120;
        U.textButton(hid("uninstall"), RW(W - 36 - bw, H - 64, bw, 40), L2(L"제거", L"Uninstall"), [] {
            St.screen = 1;
            std::thread(uninstallWorker).detach();
            InvalidateRect(g_hwnd, nullptr, FALSE);
        }, false, "trash", true);
        U.textButton(hid("cancel"), RW(W - 36 - bw - 10 - 96, H - 64, 96, 40), L2(L"취소", L"Cancel"), [] { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); });
    } else if (St.screen == 1) {
        y += 40;
        G.text(St.step, R(x, y, W - 36, y + 22), 13.5f, P.text, 500);
        y += 34;
        D2D1_RECT_F bar = RW(x, y, w, 6);
        G.fill(bar, P.line2, 3);
        G.fill(RW(x, y, w * St.progress / 100.f, 6), P.accent, 3);
        G.text(std::to_wstring(St.progress.load()) + L"%", R(x, y + 14, x + w, y + 34), 12, P.text3, 600, AR);
    } else if (St.screen == 2) {
        G.circle(x + 18, y + 22, 18, withA(P.accent, .16f));
        G.icon("check", x + 18, y + 22, 20, P.accent, 2.5f);
        G.text(St.uninstall ? L2(L"제거가 완료되었습니다", L"DeepImage Viewer was removed") : L2(L"설치가 완료되었습니다", L"Installation complete"),
               R(x + 50, y + 8, W - 36, y + 36), 16, P.text, 600);
        y += 58;
        if (!St.uninstall) {
            G.text(L2(L"이미지나 3D 파일을 DeepImage로 바로 열고 싶다면 '기본 앱으로 설정'에서 형식을 지정하세요. 탐색기 썸네일이 바로 보이지 않으면 탐색기를 다시 열어 주세요.",
                      L"To open files directly in DeepImage, choose it under \"Set as default\". If Explorer thumbnails do not appear right away, reopen the Explorer window."),
                   R(x, y, W - 36, y + 70), 12.5f, P.text2, 400, AL, true, false);
            float bw = 120, bw2 = 150;
            U.textButton(hid("run"), RW(W - 36 - bw, H - 64, bw, 40), L2(L"실행", L"Launch"), [] {
                ShellExecuteW(nullptr, L"open", (St.dir + L"\\DeepImage.exe").c_str(), nullptr, St.dir.c_str(), SW_SHOWNORMAL);
                PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            }, true, "external-link");
            U.textButton(hid("defaults"), RW(W - 36 - bw - 10 - bw2, H - 64, bw2, 40), L2(L"기본 앱으로 설정", L"Set as default"), [] {
                ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=DeepImage%20Viewer", nullptr, nullptr, SW_SHOWNORMAL);
            });
        } else {
            U.textButton(hid("closeb"), RW(W - 36 - 110, H - 64, 110, 40), L2(L"닫기", L"Close"), [] { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }, true);
        }
    } else {
        G.icon("x", x + 18, y + 22, 22, P.danger, 2.5f);
        G.text(St.error, R(x + 50, y + 8, W - 36, y + 60), 14, P.text, 600, AL, true, false);
        U.textButton(hid("closee"), RW(W - 36 - 110, H - 64, 110, 40), L2(L"닫기", L"Close"), [] { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); });
    }
    G.stroke(R(0, 0, W, H), P.line2, 0, 1);
    U.end();
    U.drawTooltip();
    G.endFrame();
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(h, &pt);
        float x = pt.x / G.scale, y = pt.y / G.scale;
        if (U.overWidget(x, y)) return HTCLIENT;
        return y < 110 ? HTCAPTION : HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        paint();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: if (G.swap) { G.resize(); InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        if (U.mouseMove(GET_X_LPARAM(l) / G.scale, GET_Y_LPARAM(l) / G.scale)) InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE: U.mouseMove(-1, -1); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: SetCapture(h); U.mouseDown(GET_X_LPARAM(l) / G.scale, GET_Y_LPARAM(l) / G.scale); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); U.mouseUp(GET_X_LPARAM(l) / G.scale, GET_Y_LPARAM(l) / G.scale); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_PROGRESS: {
        std::unique_ptr<std::wstring> s((std::wstring*)l);
        if (s) St.step = *s;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_DONE:
        St.screen = l ? 3 : 2;
        if (St.quiet) PostMessageW(h, WM_CLOSE, 0, 0);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        if (St.screen == 1) return 0;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace
} // namespace dv

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    using namespace dv;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_ko = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN;
    setLang(g_ko ? Lang::Ko : Lang::En);
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool un = false, un2 = false;
    std::wstring dirArg;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"/uninstall")) un = true;
        else if (!wcscmp(argv[i], L"/uninstall2")) { un2 = true; if (i + 1 < argc) dirArg = argv[++i]; }
        else if (!wcscmp(argv[i], L"/quiet")) St.quiet = true;
    }
    LocalFree(argv);
    if (un && !un2) {
        // run from a temp copy so the installed Uninstall.exe can be deleted
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring copy = std::wstring(tmp) + L"DeepImage-uninstall-" + std::to_wstring(GetTickCount()) + L".exe";
        if (CopyFileW(selfPath().c_str(), copy.c_str(), FALSE)) {
            std::wstring args = L"/uninstall2 \"" + dirName(selfPath()) + L"\"" + (St.quiet ? L" /quiet" : L"");
            ShellExecuteW(nullptr, L"open", copy.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        un2 = true;
        dirArg = dirName(selfPath());
    }
    St.uninstall = un2;
    if (un2) St.dir = dirArg.empty() ? installDir() : dirArg;
    else if (!g_payload.load()) {
        MessageBoxW(nullptr, L2(L"설치 파일이 손상되었습니다.", L"The setup file is damaged."), L"DeepImage Viewer", MB_ICONERROR);
        return 1;
    }
    buildPalette(systemDarkMode(), systemAccentRgb(), true);
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DMIND.DeepImageSetup";
    RegisterClassExW(&wc);
    UINT dpi = GetDpiForSystem();
    int ww = MulDiv(560, dpi, 96), wh = MulDiv(440, dpi, 96);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"DeepImage Viewer", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                             wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh, nullptr, nullptr, inst, nullptr);
    BOOL dark = P.dark;
    DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof dark);
    int corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(g_hwnd, 33, &corner, sizeof corner);
    if (!G.init(g_hwnd)) {
        MessageBoxW(nullptr, L"Direct2D is not available.", L"DeepImage Viewer", MB_ICONERROR);
        return 1;
    }
    loadMark();
    if (St.quiet) {
        St.screen = 1;
        std::thread(St.uninstall ? uninstallWorker : installWorker).detach();
    } else {
        ShowWindow(g_hwnd, SW_SHOW);
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (St.uninstall) {
        // remove this temporary copy shortly after exit
        std::wstring self = selfPath();
        std::wstring cmd = L"/c ping 127.0.0.1 -n 3 > nul & del /f /q \"" + self + L"\"";
        ShellExecuteW(nullptr, L"open", L"cmd.exe", cmd.c_str(), nullptr, SW_HIDE);
    }
    CoUninitialize();
    return 0;
}
