// DeepImage Viewer — browsing images inside archives (ZIP/CBZ, TAR/CBT)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "archive.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <mutex>
#include <set>

#include "miniz.h"

namespace dv::arc {
namespace {

struct Entry {
    std::wstring inner, leaf;
    uint64_t size = 0, offset = 0; // offset: tar data start
    uint32_t index = 0;            // zip file index
    FILETIME mtime{};
};

struct Arc {
    std::wstring path, dir;
    int type = 0; // 1 zip, 2 tar
    uint64_t fileSize = 0;
    FILETIME fileTime{};
    HANDLE h = INVALID_HANDLE_VALUE;
    mz_zip_archive z{};
    std::vector<Entry> entries;
    std::mutex m;
    ~Arc() {
        if (z.m_zip_mode != MZ_ZIP_MODE_INVALID) mz_zip_reader_end(&z);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};

struct Ref { std::shared_ptr<Arc> arc; size_t entry; };

std::mutex gMx;
std::map<std::wstring, std::shared_ptr<Arc>> gArcs; // lower(archive path)
std::map<std::wstring, Ref> gRefs;                 // lower(virtual path)

std::wstring low(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }

std::wstring tempRoot() {
    static std::wstring root = [] {
        wchar_t t[MAX_PATH + 1];
        DWORD n = GetTempPathW(MAX_PATH + 1, t);
        std::wstring d = n ? std::wstring(t, n) : L"C:\\Windows\\Temp\\";
        if (d.back() != L'\\') d += L'\\';
        return d + L"DeepImage Viewer\\arc";
    }();
    return root;
}

FILETIME fromUnix(int64_t t) {
    uint64_t v = (uint64_t)(t + 11644473600ll) * 10000000ull;
    return FILETIME{(DWORD)v, (DWORD)(v >> 32)};
}

bool readAt(HANDLE h, uint64_t ofs, void* buf, DWORD n) {
    OVERLAPPED o{};
    o.Offset = (DWORD)ofs;
    o.OffsetHigh = (DWORD)(ofs >> 32);
    DWORD r = 0;
    return ReadFile(h, buf, n, &r, &o) && r == n;
}

// entry names: UTF-8 when flagged or valid, otherwise the OEM code page (what Windows zip tools write)
std::wstring decodeName(const char* s, size_t n, bool utf8) {
    if (!n) return {};
    int w = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, (int)n, nullptr, 0);
    UINT cp = (utf8 || w > 0) ? CP_UTF8 : CP_OEMCP;
    w = MultiByteToWideChar(cp, 0, s, (int)n, nullptr, 0);
    std::wstring out(w, L'\0');
    MultiByteToWideChar(cp, 0, s, (int)n, out.data(), w);
    return out;
}

// a single, harmless file name: never a path, never reserved characters
std::wstring safeLeaf(const std::wstring& leaf) {
    std::wstring s;
    for (wchar_t c : leaf) s += (c < 32 || wcschr(L"<>:\"/\\|?*", c)) ? L'_' : c;
    while (!s.empty() && (s.back() == L'.' || s.back() == L' ')) s.pop_back();
    while (!s.empty() && s[0] == L' ') s.erase(0, 1);
    std::wstring ext = extOf(s);
    if (s.empty() || s == L"." || s == L"..") s = L"file." + ext;
    if (s.size() > 120) s = s.substr(0, 100) + L"." + ext;
    return s;
}

bool viewable(const std::wstring& inner, std::wstring& leaf) {
    std::wstring n = inner;
    for (auto& c : n) if (c == L'\\') c = L'/';
    if (n.empty() || n.back() == L'/') return false;
    if (low(n).rfind(L"__macosx/", 0) == 0 || low(n).find(L"/__macosx/") != std::wstring::npos) return false;
    size_t p = n.find_last_of(L'/');
    leaf = p == std::wstring::npos ? n : n.substr(p + 1);
    if (leaf.size() > 2 && leaf[0] == L'.' && leaf[1] == L'_') return false; // macOS resource forks
    return kindOfExt(extOf(leaf)) != Kind::None;
}

std::wstring hashKey(const Arc& a) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* d, size_t n) { for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)d)[i]; h *= 1099511628211ull; } };
    std::wstring l = low(a.path);
    mix(l.data(), l.size() * 2);
    mix(&a.fileSize, 8);
    mix(&a.fileTime, 8);
    wchar_t b[20];
    swprintf(b, 20, L"%016llx", (unsigned long long)h);
    return b;
}

bool openZip(Arc& a) {
    char sig[2] = {};
    if (!readAt(a.h, 0, sig, 2) || sig[0] != 'P' || sig[1] != 'K') return false;
    a.z.m_pRead = [](void* op, mz_uint64 ofs, void* buf, size_t n) -> size_t {
        OVERLAPPED o{};
        o.Offset = (DWORD)ofs;
        o.OffsetHigh = (DWORD)(ofs >> 32);
        DWORD r = 0;
        return ReadFile((HANDLE)op, buf, (DWORD)n, &r, &o) ? r : 0;
    };
    a.z.m_pIO_opaque = a.h;
    if (!mz_zip_reader_init(&a.z, a.fileSize, 0)) return false;
    a.type = 1;
    mz_uint n = mz_zip_reader_get_num_files(&a.z);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&a.z, i, &st) || st.m_is_directory || !st.m_is_supported || st.m_is_encrypted) continue;
        if (st.m_uncomp_size == 0 || st.m_uncomp_size > (2ull << 30)) continue;
        Entry e;
        e.inner = decodeName(st.m_filename, strlen(st.m_filename), (st.m_bit_flag & (1 << 11)) != 0);
        if (!viewable(e.inner, e.leaf)) continue;
        e.index = i;
        e.size = st.m_uncomp_size;
        e.mtime = fromUnix((int64_t)st.m_time);
        a.entries.push_back(std::move(e));
    }
    return true;
}

uint64_t tarNum(const char* p, size_t n) {
    if ((unsigned char)p[0] & 0x80) { // base-256 (GNU large sizes)
        uint64_t v = (unsigned char)p[0] & 0x7f;
        for (size_t i = 1; i < n; ++i) v = (v << 8) | (unsigned char)p[i];
        return v;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < n && p[i]; ++i)
        if (p[i] >= '0' && p[i] <= '7') v = v * 8 + (p[i] - '0');
    return v;
}

bool openTar(Arc& a) {
    char hd[512];
    if (a.fileSize < 1024 || !readAt(a.h, 0, hd, 512) || memcmp(hd + 257, "ustar", 5) != 0) return false;
    a.type = 2;
    uint64_t pos = 0;
    std::string longName;
    for (int guard = 0; pos + 512 <= a.fileSize && guard < 200000; ++guard) {
        if (!readAt(a.h, pos, hd, 512)) break;
        if (hd[0] == 0) break; // end of archive
        uint64_t size = tarNum(hd + 124, 12), data = pos + 512;
        char type = hd[156];
        auto field = [&](size_t o, size_t n) { return std::string(hd + o, strnlen(hd + o, n)); };
        if (type == 'L' || type == 'x') {
            std::string body((size_t)std::min<uint64_t>(size, 1 << 20), '\0');
            if (!body.empty() && readAt(a.h, data, body.data(), (DWORD)body.size())) {
                if (type == 'L') longName = body.c_str();
                else for (size_t p = 0; p < body.size();) { // pax: "<len> key=value\n"
                    size_t sp = body.find(' ', p);
                    if (sp == std::string::npos) break;
                    size_t len = strtoul(body.c_str() + p, nullptr, 10);
                    if (!len || p + len > body.size()) break;
                    std::string kv = body.substr(sp + 1, p + len - sp - 2);
                    if (kv.rfind("path=", 0) == 0) longName = kv.substr(5);
                    p += len;
                }
            }
        } else if (type == '0' || type == 0 || type == '7') {
            std::string name = longName.empty() ? field(0, 100) : longName;
            if (longName.empty() && memcmp(hd + 257, "ustar", 5) == 0 && hd[345]) name = field(345, 155) + "/" + name;
            longName.clear();
            Entry e;
            e.inner = decodeName(name.data(), name.size(), false);
            if (size > 0 && data + size <= a.fileSize && size <= (2ull << 30) && viewable(e.inner, e.leaf)) {
                e.offset = data;
                e.size = size;
                e.mtime = fromUnix((int64_t)tarNum(hd + 136, 12));
                a.entries.push_back(std::move(e));
            }
        } else if (type != 'g') longName.clear();
        pos = data + ((size + 511) & ~511ull);
    }
    return true;
}

std::shared_ptr<Arc> load(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return nullptr;
    const uint64_t size = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    const std::wstring key = low(path);
    {
        std::lock_guard<std::mutex> g(gMx);
        auto it = gArcs.find(key);
        if (it != gArcs.end() && it->second->fileSize == size && CompareFileTime(&it->second->fileTime, &fa.ftLastWriteTime) == 0) return it->second;
    }
    auto a = std::make_shared<Arc>();
    a->path = path;
    a->fileSize = size;
    a->fileTime = fa.ftLastWriteTime;
    a->h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (a->h == INVALID_HANDLE_VALUE) return nullptr;
    if (!openZip(*a) && !openTar(*a)) return nullptr;
    a->dir = tempRoot() + L"\\" + hashKey(*a);
    std::sort(a->entries.begin(), a->entries.end(), [](const Entry& x, const Entry& y) { return StrCmpLogicalW(x.inner.c_str(), y.inner.c_str()) < 0; });
    std::lock_guard<std::mutex> g(gMx);
    gArcs[key] = a;
    for (size_t i = 0; i < a->entries.size(); ++i) gRefs[low(a->dir + fmtf(L"\\%zu\\", i) + safeLeaf(a->entries[i].leaf))] = Ref{a, i};
    return a;
}

struct Sink { HANDLE h; };
size_t writeCb(void* op, mz_uint64, const void* buf, size_t n) {
    DWORD w = 0;
    return WriteFile(((Sink*)op)->h, buf, (DWORD)n, &w, nullptr) ? w : 0;
}

} // namespace

const std::vector<std::wstring>& archiveExts() {
    static const std::vector<std::wstring> v = {L"zip", L"cbz", L"tar", L"cbt"};
    return v;
}
bool isArchiveExt(const std::wstring& ext) {
    std::wstring e = low(ext);
    for (auto& x : archiveExts()) if (e == x) return true;
    return false;
}

bool list(const std::wstring& archive, std::vector<Item>& out) {
    out.clear();
    auto a = load(archive);
    if (!a) return false;
    for (size_t i = 0; i < a->entries.size(); ++i) {
        auto& e = a->entries[i];
        Item it;
        it.path = a->dir + fmtf(L"\\%zu\\", i) + safeLeaf(e.leaf);
        it.name = e.leaf;
        it.inner = e.inner;
        it.size = e.size;
        it.mtime = e.mtime;
        out.push_back(std::move(it));
    }
    return true;
}

bool hasViewable(const std::wstring& archive) {
    auto a = load(archive);
    return a && !a->entries.empty();
}

std::wstring archiveOf(const std::wstring& path) {
    std::lock_guard<std::mutex> g(gMx);
    auto it = gRefs.find(low(path));
    return it == gRefs.end() ? std::wstring() : it->second.arc->path;
}

bool materialize(const std::wstring& path) {
    Ref ref;
    {
        std::lock_guard<std::mutex> g(gMx);
        auto it = gRefs.find(low(path));
        if (it == gRefs.end()) return true; // ordinary file
        ref = it->second;
    }
    Arc& a = *ref.arc;
    const Entry& e = a.entries[ref.entry];
    std::lock_guard<std::mutex> g(a.m);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && (((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow) == e.size) return true;
    SHCreateDirectoryExW(nullptr, dirName(path).c_str(), nullptr);
    std::wstring tmp = path + L".part";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    if (a.type == 1) {
        Sink s{h};
        ok = mz_zip_reader_extract_to_callback(&a.z, e.index, writeCb, &s, 0) != 0;
    } else {
        std::vector<char> buf(1 << 20);
        ok = true;
        for (uint64_t done = 0; ok && done < e.size;) {
            DWORD n = (DWORD)std::min<uint64_t>(buf.size(), e.size - done), w = 0;
            ok = readAt(a.h, e.offset + done, buf.data(), n) && WriteFile(h, buf.data(), n, &w, nullptr) && w == n;
            done += n;
        }
    }
    if (ok) SetFileTime(h, nullptr, nullptr, &e.mtime);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}

static void removeTree(const std::wstring& dir) {
    std::wstring from = dir + L'\0'; // double-NUL terminated
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

void cleanup(bool atExit) {
    if (atExit) {
        std::vector<std::wstring> dirs;
        {
            std::lock_guard<std::mutex> g(gMx);
            for (auto& [k, a] : gArcs) dirs.push_back(a->dir);
            gRefs.clear();
            gArcs.clear();
        }
        for (auto& d : dirs) removeTree(d);
        return;
    }
    // leftovers of sessions that ended without cleaning up (crash, shutdown): older than a day
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((tempRoot() + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const uint64_t n = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        uint64_t t = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        if (n > t && n - t > 864000000000ull) removeTree(tempRoot() + L"\\" + fd.cFileName);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

} // namespace dv::arc
