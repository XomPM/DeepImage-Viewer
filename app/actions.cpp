// DeepImage Viewer — file handling, loading, editing actions
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "app.h"
#include "i18n.h"
#include "settings.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <thread>

namespace dv {

// ======================================================================== theme / language
void App::applyTheme() {
    bool dark = S.theme == 0 ? systemDarkMode() : S.theme == 2;
    buildPalette(dark, systemAccentRgb(), S.accent == 0);
    BOOL d = dark;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &d, sizeof d);
    COLORREF cap = RGB((int)(P.bg.r * 255), (int)(P.bg.g * 255), (int)(P.bg.b * 255));
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof cap);
    mv.setTheme(dark, P.accent);
    requestPaint();
}

void App::applyLanguage() {
    Lang l = S.language == 1 ? Lang::Ko : S.language == 2 ? Lang::En : (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN ? Lang::Ko : Lang::En);
    setLang(l);
    if (exif.loaded && !cur.path.empty() && mode == Mode::Image) exif = readExif(cur.path);
    updateTitle();
    requestPaint();
}

void App::toast(const std::wstring& t, bool err) {
    toasts.push_back({t, err, GetTickCount()});
    if (toasts.size() > 3) toasts.erase(toasts.begin());
    ensureTimer();
    requestPaint();
}

void App::updateTitle() {
    std::wstring t = cur.path.empty() ? std::wstring(kAppName) : cur.name + L" — " + kAppName;
    SetWindowTextW(hwnd, t.c_str());
}

// ======================================================================== files
void App::openDialog() {
    ComPtr<IFileOpenDialog> d;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return;
    std::wstring all, imgs, mdls;
    for (auto& e : imageExts()) { imgs += (imgs.empty() ? L"*." : L";*.") + e; }
    for (auto& e : modelExts()) { mdls += (mdls.empty() ? L"*." : L";*.") + e; }
    all = imgs + L";" + mdls;
    std::wstring nAll = T("filter.all"), nImg = T("filter.images"), nMdl = T("filter.models"), nAny = T("filter.any");
    COMDLG_FILTERSPEC spec[] = {{nAll.c_str(), all.c_str()}, {nImg.c_str(), imgs.c_str()}, {nMdl.c_str(), mdls.c_str()}, {nAny.c_str(), L"*.*"}};
    d->SetFileTypes(4, spec);
    if (!S.lastDir.empty()) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(S.lastDir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) d->SetFolder(folder.Get());
    }
    if (FAILED(d->Show(hwnd))) return;
    ComPtr<IShellItem> it;
    PWSTR p = nullptr;
    if (SUCCEEDED(d->GetResult(&it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        std::wstring s = p;
        CoTaskMemFree(p);
        openPath(s);
    }
}

void App::listFolder(const std::wstring& p) {
    files.clear();
    std::wstring dir = dirName(p);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) continue;
            FileEntry e;
            e.name = fd.cFileName;
            e.ext = extOf(e.name);
            e.kind = kindOfExt(e.ext);
            if (e.kind == Kind::None) continue;
            e.path = dir + L"\\" + e.name;
            e.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            e.mtime = fd.ftLastWriteTime;
            files.push_back(std::move(e));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // Explorer's natural ordering ("img2" before "img10")
    std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) { return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0; });
}

void App::openPath(const std::wstring& raw) {
    wchar_t full[MAX_PATH * 4];
    std::wstring p = GetFullPathNameW(raw.c_str(), (DWORD)std::size(full), full, nullptr) ? full : raw;
    listFolder(p);
    int i = -1;
    for (size_t k = 0; k < files.size(); ++k) if (!_wcsicmp(files[k].path.c_str(), p.c_str())) { i = (int)k; break; }
    if (i < 0) {
        FileEntry e;
        e.path = p;
        e.name = fileName(p);
        e.ext = extOf(p);
        e.kind = kindOfExt(e.ext);
        if (e.kind == Kind::None) { toast(T("load.failed"), true); return; }
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) {
            e.size = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
            e.mtime = fa.ftLastWriteTime;
        }
        files.insert(files.begin(), e);
        i = 0;
    }
    S.lastDir = dirName(p);
    thumbs.cancelAll();
    stripUser = false;
    show(i, true);
}

void App::refreshFolder() {
    if (cur.path.empty()) return;
    std::wstring p = cur.path;
    listFolder(p);
    for (size_t k = 0; k < files.size(); ++k) if (!_wcsicmp(files[k].path.c_str(), p.c_str())) index = (int)k;
    requestPaint();
}

void App::go(int d) {
    if (files.empty()) return;
    if (compare) { goCompare(d); return; }
    int n = index + d;
    if (n < 0 || n >= (int)files.size()) return;
    show(n);
}

void App::show(int i, bool force) {
    if (i < 0 || i >= (int)files.size()) return;
    if (!force && i == index && !cur.path.empty()) return;
    if (compare && files[i].kind != Kind::Image) toggleCompare(0);
    index = i;
    cur = files[i];
    const uint64_t s = ++seq;
    errorKey.clear();
    annot = annot && cur.kind == Kind::Image;
    editMode = false;
    panel = EditPanel::None;
    adj = Adjust{};
    fxActive = false;
    editUndo.clear();
    editDirty = false;
    exif = ExifInfo{};
    hist = Histogram{};
    infoScroll = 0;
    stripUser = false;
    updateTitle();
    loading = true;
    loadStart = GetTickCount();
    loadingLabel = T("loading");
    if (cur.kind == Kind::Image) {
        mode = Mode::Image;
        auto it = cache.find(cur.path);
        if (it != cache.end()) {
            std::lock_guard<std::mutex> g(pendMx);
            pendImg = PendingImage{s, true, it->second, "", cur.path};
            PostMessageW(hwnd, WM_APP_IMAGE, 0, 0);
        } else {
            std::wstring path = cur.path;
            HWND hw = hwnd;
            std::thread([this, s, path, hw] {
                auto img = std::make_shared<DecodedImage>();
                std::string err;
                bool ok = decodeImageFile(path, *img, err);
                {
                    std::lock_guard<std::mutex> g(pendMx);
                    if (s != seq) return;
                    pendImg = PendingImage{s, ok, img, err, path};
                }
                PostMessageW(hw, WM_APP_IMAGE, 0, 0);
            }).detach();
        }
    } else {
        mode = Mode::Model;
        hintGone = false;
        std::wstring path = cur.path, ext = cur.ext;
        if (ext == L"step" || ext == L"stp" || ext == L"iges" || ext == L"igs" || ext == L"brep" || ext == L"brp") loadingLabel = T("loading.cad");
        HWND hw = hwnd;
        std::thread([s, path, hw] {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            DvModel* m = nullptr;
            std::string err;
            if (!ML.ensure()) err = "dvmodel.dll";
            else {
                std::lock_guard<std::mutex> g(ML.lock);
                char e[512] = {};
                m = ML.load(path.c_str(), e, sizeof e);
                if (!m) err = e;
            }
            auto* msg = new std::string(std::to_string(s) + "|" + err);
            if (!PostMessageW(hw, WM_APP_MODEL, (WPARAM)msg, (LPARAM)m)) { delete msg; if (m) ML.free(m); }
            CoUninitialize();
        }).detach();
    }
    requestThumbs();
    ensureTimer();
    requestPaint();
}

void App::onImageDecoded() {
    PendingImage p;
    {
        std::lock_guard<std::mutex> g(pendMx);
        p = std::move(pendImg);
        pendImg = PendingImage{};
    }
    if (!p.img || p.seq != seq) return;
    loading = false;
    if (!p.ok) {
        doc.clear();
        errorKey = p.err.empty() ? "load.failed" : p.err;
        requestPaint();
        return;
    }
    doc.clear();
    doc.path = p.path;
    doc.img = *p.img;
    doc.upload(G.dc.Get());
    layout();
    view.mode = S.fitMode;
    view.setViewport(compare ? rcPaneL : rcViewport);
    view.setImage((float)doc.W(), (float)doc.H());
    markup.key = cur.path;
    markup.deselect();
    markup.setSource(doc.img.frames.empty() ? nullptr : doc.img.frames[0]);
    exif = readExif(cur.path);
    if (!doc.img.frames.empty()) hist = computeHistogram(doc.img.frames[0].Get());
    // remember in the neighbour cache
    if (!cache.count(p.path)) {
        cache[p.path] = p.img;
        cacheOrder.push_back(p.path);
    }
    startPreload();
    ensureTimer();
    requestPaint();
}

void App::startPreload() {
    if (preloadBusy || mode != Mode::Image || index < 0) return;
    // keep current + neighbours; evict the rest (bounded memory)
    std::vector<std::wstring> keep;
    std::vector<int> want = {index, index + 1, index - 1};
    if (compare && indexR >= 0) want = {index, indexR, indexR + 1, indexR + 2, index - 1, index - 2};
    for (int k : want) {
        if (k >= 0 && k < (int)files.size() && files[k].kind == Kind::Image) keep.push_back(files[k].path);
    }
    for (auto it = cacheOrder.begin(); it != cacheOrder.end();) {
        if (std::find(keep.begin(), keep.end(), *it) == keep.end()) { cache.erase(*it); it = cacheOrder.erase(it); }
        else ++it;
    }
    for (auto& path : keep) {
        if (cache.count(path) || preloadFailed.count(path)) continue;
        if (files[index].size > 200ull << 20) break;
        preloadBusy = true;
        HWND hw = hwnd;
        std::thread([path, hw] {
            auto img = std::make_shared<DecodedImage>();
            std::string err;
            bool ok = decodeImageFile(path, *img, err) && (uint64_t)img->width * img->height <= 80'000'000ull;
            auto* holder = new std::pair<std::wstring, std::shared_ptr<DecodedImage>>(path, ok ? img : nullptr);
            if (!PostMessageW(hw, WM_APP_PRELOAD, 0, (LPARAM)holder)) delete holder;
        }).detach();
        return;
    }
}

void App::onModelLoaded(uint64_t s, DvModel* m, const std::string& err) {
    if (s != seq) { if (m) ML.free(m); return; }
    loading = false;
    if (!m) {
        mv.clear();
        errorKey = err == "dvmodel.dll" ? "load.failed" : "load.failed";
        requestPaint();
        return;
    }
    mv.setTheme(P.dark, P.accent);
    mv.grid = S.grid;
    mv.setModel(ML.info(m));
    ML.free(m);
    layout();
    mv.view("iso", false);
    ensureTimer();
    requestPaint();
}

void App::requestThumbs() {
    if (files.empty()) return;
    const int span = 30;
    int lo = std::max(0, index - span), hi = std::min((int)files.size() - 1, index + span);
    for (int k = lo; k <= hi; ++k) {
        auto& f = files[k];
        if (thumbBmp.count(f.path) || thumbFailed.count(f.path)) continue;
        thumbs.request(f.path, f.kind, 1000 - std::abs(k - index) * 10);
    }
    std::map<std::wstring, int> pos;
    for (int k = lo; k <= hi; ++k) pos[files[k].path] = k;
    thumbs.reprioritize([&](const std::wstring& p) { auto it = pos.find(p); return it == pos.end() ? -100000 : 1000 - std::abs(it->second - index) * 10; });
}

// ======================================================================== modes
void App::toggleAnnot(int on) {
    bool v = on < 0 ? !annot : on != 0;
    if (v && (mode != Mode::Image || !doc.loaded())) return;
    if (v && compare) toggleCompare(0);
    if (!v) markup.deselect();
    if (v && editMode) toggleEdit(0);
    annot = v;
    if (v && markup.tool == Tool::Hand) markup.tool = Tool::Pen;
    requestPaint();
}

void App::toggleEdit(int on) {
    bool v = on < 0 ? !editMode : on != 0;
    if (v && (mode != Mode::Image || !doc.loaded())) return;
    if (v && compare) toggleCompare(0);
    if (v && annot) annot = false;
    if (!v) {
        if (panel == EditPanel::Adjust) { adj = Adjust{}; fxActive = false; }
        panel = EditPanel::None;
    }
    editMode = v;
    requestPaint();
}

void App::setTool(Tool t) {
    if (t != Tool::Hand) markup.deselect();
    markup.tool = t;
    requestPaint();
}

void App::toggleInfo() {
    S.infoPanel = !S.infoPanel;
    S.save();
    requestPaint();
}

void App::toggleFullscreen() {
    if (!fullscreen) {
        savedPlacement.length = sizeof savedPlacement;
        GetWindowPlacement(hwnd, &savedPlacement);
        savedStyle = GetWindowLongW(hwnd, GWL_STYLE);
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        fullscreen = true;
        SetWindowLongW(hwnd, GWL_STYLE, (savedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        fullscreen = false;
        SetWindowLongW(hwnd, GWL_STYLE, savedStyle);
        SetWindowPlacement(hwnd, &savedPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    requestPaint();
}

void App::confirm(const std::wstring& title, const std::wstring& msg, std::vector<ModalButton> buttons) {
    modal = Modal{};
    modal.kind = 4;
    modal.title = title;
    modal.message = msg;
    modal.buttons = std::move(buttons);
    requestPaint();
}

void App::guardUnsaved(Action proceed) {
    bool dirty = editDirty || (!cur.path.empty() && markup.dirty(cur.path));
    if (!dirty || modal.kind) { if (!modal.kind) proceed(); return; }
    const bool ko = lang() == Lang::Ko;
    confirm(ko ? L"저장하지 않은 변경" : L"Unsaved changes",
            ko ? L"편집하거나 표시한 내용을 저장하지 않았습니다. 저장하지 않고 계속할까요?" : L"Your edits or markup have not been saved. Continue without saving?",
            {{T("cancel"), nullptr},
             {T("save"), [this] { saveAs(); }},
             {ko ? L"저장 안 함" : L"Don't save", [this, proceed] {
                  editDirty = false;
                  markup.markSaved();
                  proceed();
              }, false, true}});
}

// ======================================================================== fit modes / compare
void App::setFitMode(int m) {
    S.fitMode = m;
    S.save();
    view.mode = viewR.mode = m;
    eachView([](ImageView& v, const D2D1_RECT_F&) { v.fit(true); });
    ensureTimer();
    requestPaint();
}

int App::nextImage(int from, int dir) const {
    for (int k = from + dir; k >= 0 && k < (int)files.size(); k += dir)
        if (files[k].kind == Kind::Image) return k;
    return -1;
}

void App::toggleCompare(int on) {
    bool v = on < 0 ? !compare : on != 0;
    if (v == compare) return;
    if (v) {
        if (mode != Mode::Image || !doc.loaded()) return;
        int r = nextImage(index, 1);
        int l = index;
        if (r < 0) { r = index; l = nextImage(index, -1); }
        if (l < 0) { toast(T("compare.need2")); return; }
        if (annot) toggleAnnot(0);
        if (editMode) toggleEdit(0);
        compare = true;
        layout();
        view.setViewport(rcPaneL);
        if (l != index) guardUnsaved([this, l] { show(l); });
        showRight(r);
        toast(T("compare.hint"));
    } else {
        compare = false;
        docR.clear();
        indexR = -1;
        loadingR = false;
        errorR.clear();
        ++seqR;
        layout();
    }
    stripUser = false;
    ensureTimer();
    requestPaint();
}

void App::showRight(int k) {
    if (k < 0 || k >= (int)files.size()) return;
    if (files[k].kind != Kind::Image) { toast(T("compare.imageonly"), true); return; }
    indexR = k;
    stripUser = false;
    const uint64_t s = ++seqR;
    errorR.clear();
    const std::wstring path = files[k].path;
    auto it = cache.find(path);
    if (it != cache.end()) { setRight(it->second, path); return; }
    loadingR = true;
    docR.clear();
    HWND hw = hwnd;
    std::thread([s, path, hw] {
        auto img = std::make_shared<DecodedImage>();
        std::string err;
        bool ok = decodeImageFile(path, *img, err);
        auto* holder = new std::pair<std::wstring, std::shared_ptr<DecodedImage>>(path, ok ? img : nullptr);
        if (!PostMessageW(hw, WM_APP_IMAGE_R, (WPARAM)(s & 0xFFFFFFFF), (LPARAM)holder)) delete holder;
    }).detach();
    requestThumbs();
    ensureTimer();
    requestPaint();
}

void App::setRight(const std::shared_ptr<DecodedImage>& img, const std::wstring& path) {
    loadingR = false;
    docR.clear();
    docR.path = path;
    docR.img = *img;
    docR.upload(G.dc.Get());
    layout();
    viewR.mode = S.fitMode;
    viewR.padT = view.padT;
    viewR.padB = view.padB;
    viewR.setViewport(rcPaneR);
    viewR.setImage((float)docR.W(), (float)docR.H());
    // start from the same zoom as the left pane when the user has zoomed manually
    if (!view.fitted && docR.loaded()) {
        float k = view.s / viewR.s;
        viewR.zoomBy(k, (rcPaneR.left + rcPaneR.right) / 2, (rcPaneR.top + rcPaneR.bottom) / 2, false);
    }
    if (!cache.count(path)) { cache[path] = img; cacheOrder.push_back(path); }
    startPreload();
    ensureTimer();
    requestPaint();
}

void App::goCompare(int d) {
    // step two images at a time: [L R] -> [R+1 R+2]
    int l, r;
    if (d > 0) {
        l = nextImage(std::max(index, indexR), 1);
        if (l < 0) return;
        r = nextImage(l, 1);
        if (r < 0) { r = l; l = std::max(index, indexR); }
    } else {
        r = nextImage(std::min(index, indexR), -1);
        if (r < 0) return;
        l = nextImage(r, -1);
        if (l < 0) { l = r; r = std::min(index, indexR); }
    }
    if (l == index && r == indexR) return;
    show(l);
    showRight(r);
}

void App::open3dPrint() {
    ShellExecuteW(hwnd, L"open", L"https://www.godgok.com/3dmind/", nullptr, nullptr, SW_SHOWNORMAL);
}

// ======================================================================== output
ComPtr<IWICBitmap> App::composite(bool white, bool withMarkup, bool withEffects) {
    if (!doc.loaded()) return nullptr;
    float w, h;
    view.rotatedSize(w, h);
    auto M = D2D1::Matrix3x2F::Translation(-view.imgW / 2, -view.imgH / 2) * D2D1::Matrix3x2F::Scale(view.fx, view.fy) *
             D2D1::Matrix3x2F::Rotation((float)view.rot) * D2D1::Matrix3x2F::Translation(w / 2, h / 2);
    bool useFx = withEffects && fxActive && fx.head();
    return renderToWic((UINT)std::lround(w), (UINT)std::lround(h), [&](ID2D1DeviceContext* dc) {
        D2D1_MATRIX_3X2_F old;
        dc->GetTransform(&old);
        dc->SetTransform(M * D2D1::Matrix3x2F(old._11, old._12, old._21, old._22, old._31, old._32));
        doc.draw(dc, 1.f, useFx ? fx.head() : nullptr, useFx ? fx.tail() : nullptr);
        if (withMarkup) markup.draw(dc, false);
        dc->SetTransform(old);
    }, white);
}

void App::saveAs() {
    if (!doc.loaded()) return;
    auto encs = availableEncoders();
    ComPtr<IFileSaveDialog> d;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return;
    std::vector<COMDLG_FILTERSPEC> spec;
    for (auto& e : encs) spec.push_back({e.name.c_str(), e.exts.c_str()});
    d->SetFileTypes((UINT)spec.size(), spec.data());
    int def = 0;
    for (size_t i = 0; i < encs.size(); ++i) if (encs[i].container == doc.img.container && doc.img.container != GUID_ContainerFormatGif) def = (int)i;
    d->SetFileTypeIndex(def + 1);
    std::wstring stem = cur.name.substr(0, cur.name.find_last_of(L'.'));
    std::wstring name = stem + (lang() == Lang::Ko ? L"_편집" : L"_edited");
    d->SetFileName(name.c_str());
    d->SetDefaultExtension(L"png");
    ComPtr<IShellItem> folder;
    if (SUCCEEDED(SHCreateItemFromParsingName(dirName(cur.path).c_str(), nullptr, IID_PPV_ARGS(&folder)))) d->SetFolder(folder.Get());
    // quality + metadata options in the dialog itself
    ComPtr<IFileDialogCustomize> cz;
    if (SUCCEEDED(d.As(&cz))) {
        cz->StartVisualGroup(100, T("save.quality"));
        cz->AddComboBox(101);
        const int qs[] = {100, 95, 90, 85, 80, 70, 60};
        for (int i = 0; i < 7; ++i) cz->AddControlItem(101, 200 + qs[i], std::to_wstring(qs[i]).c_str());
        cz->SetSelectedControlItem(101, 295); // 95
        cz->EndVisualGroup();
        cz->AddCheckButton(102, T("save.keepmeta"), TRUE);
    }
    if (FAILED(d->Show(hwnd))) return;
    ComPtr<IShellItem> it;
    PWSTR p = nullptr;
    if (FAILED(d->GetResult(&it)) || FAILED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) return;
    std::wstring out = p;
    CoTaskMemFree(p);
    UINT ti = 1;
    d->GetFileTypeIndex(&ti);
    GUID container = encs[std::min<size_t>(ti - 1, encs.size() - 1)].container;
    // honour a typed extension
    std::wstring e = extOf(out);
    for (auto& en : encs) if (lower(en.exts).find(L"*." + e + L";") != std::wstring::npos || lower(en.exts) == L"*." + e || lower(en.exts).ends_with(L";*." + e)) container = en.container;
    DWORD q = 295;
    BOOL keep = TRUE;
    if (cz) { cz->GetSelectedControlItem(101, &q); cz->GetCheckButtonState(102, &keep); }
    float quality = (q - 200) / 100.f;
    bool flatten = container == GUID_ContainerFormatJpeg || container == GUID_ContainerFormatBmp;
    auto px = composite(flatten);
    if (!px || !saveBitmap(out, container, px.Get(), quality, cur.path, keep != FALSE)) { toast(T("save.failed"), true); return; }
    markup.markSaved();
    editDirty = false;
    toast(std::wstring(T("saved")) + L" · " + fileName(out));
    if (!_wcsicmp(dirName(out).c_str(), dirName(cur.path).c_str())) refreshFolder();
}

void App::copyImage() {
    auto px = composite(false);
    if (px && copyToClipboard(hwnd, px.Get())) toast(T("copied"));
    else toast(T("save.failed"), true);
}

void App::saveScreenshot() {
    if (!mv.hasModel()) return;
    UINT w = (UINT)(rw(rcViewport) * G.scale), h = (UINT)(rh(rcViewport) * G.scale);
    auto px = mv.screenshot(w, h);
    if (!px) return;
    ComPtr<IFileSaveDialog> d;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return;
    COMDLG_FILTERSPEC spec[] = {{L"PNG", L"*.png"}};
    d->SetFileTypes(1, spec);
    d->SetDefaultExtension(L"png");
    std::wstring stem = cur.name.substr(0, cur.name.find_last_of(L'.')) + L"_view";
    d->SetFileName(stem.c_str());
    ComPtr<IShellItem> folder;
    if (SUCCEEDED(SHCreateItemFromParsingName(dirName(cur.path).c_str(), nullptr, IID_PPV_ARGS(&folder)))) d->SetFolder(folder.Get());
    if (FAILED(d->Show(hwnd))) return;
    ComPtr<IShellItem> it;
    PWSTR p = nullptr;
    if (FAILED(d->GetResult(&it)) || FAILED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) return;
    std::wstring out = p;
    CoTaskMemFree(p);
    if (saveBitmap(out, GUID_ContainerFormatPng, px.Get(), 1, L"", false)) {
        toast(std::wstring(T("saved")) + L" · " + fileName(out));
        if (!_wcsicmp(dirName(out).c_str(), dirName(cur.path).c_str())) refreshFolder();
    } else toast(T("save.failed"), true);
}

// ======================================================================== pixel edits
void App::pushEdit() {
    EditSnapshot s;
    s.image = doc.img.frames.empty() ? nullptr : doc.img.frames[0];
    if (auto d = markup.doc()) s.shapes = d->shapes;
    editUndo.push_back(s);
    if (editUndo.size() > 10) editUndo.erase(editUndo.begin());
}

void App::undoEdit() {
    if (editUndo.empty()) return;
    auto s = editUndo.back();
    editUndo.pop_back();
    if (!s.image) return;
    doc.replace(s.image);
    if (auto d = markup.doc()) d->shapes = s.shapes;
    afterPixelEdit();
    editDirty = !editUndo.empty();
}

void App::afterPixelEdit() {
    view.setImage((float)doc.W(), (float)doc.H());
    markup.setSource(doc.img.frames[0]);
    hist = computeHistogram(doc.img.frames[0].Get());
    cache.erase(cur.path);
    requestPaint();
}

void App::bakeOrientationIntoImage() {
    if (view.rot == 0 && view.fx > 0 && view.fy > 0) return;
    auto src = doc.img.frames[0];
    auto out = bakeOrientation(src.Get(), view.rot, view.fx, view.fy);
    if (!out) return;
    pushEdit();
    // map markup with the same transform
    float w, h;
    view.rotatedSize(w, h);
    auto M = D2D1::Matrix3x2F::Translation(-view.imgW / 2, -view.imgH / 2) * D2D1::Matrix3x2F::Scale(view.fx, view.fy) *
             D2D1::Matrix3x2F::Rotation((float)view.rot) * D2D1::Matrix3x2F::Translation(w / 2, h / 2);
    if (auto d = markup.doc()) transformShapes(d->shapes, [&](D2D1_POINT_2F p) { return M.TransformPoint(p); }, 1);
    doc.replace(out);
    afterPixelEdit();
    editDirty = true;
}

void App::beginCrop() {
    if (!doc.loaded()) return;
    if (panel == EditPanel::Adjust) { adj = Adjust{}; fxActive = false; }
    bakeOrientationIntoImage();
    crop = R(0, 0, (float)doc.W(), (float)doc.H());
    cropAspect = 0;
    panel = EditPanel::Crop;
    view.fitWhole(true);
    requestPaint();
}

void App::applyCrop() {
    UINT x = (UINT)std::lround(clampf(crop.left, 0, (float)doc.W())), y = (UINT)std::lround(clampf(crop.top, 0, (float)doc.H()));
    UINT w = (UINT)std::lround(rw(crop)), h = (UINT)std::lround(rh(crop));
    w = std::min(w, doc.W() - x);
    h = std::min(h, doc.H() - y);
    panel = EditPanel::None;
    if (w < 1 || h < 1 || (x == 0 && y == 0 && w == doc.W() && h == doc.H())) { requestPaint(); return; }
    auto out = cropBitmap(doc.img.frames[0].Get(), x, y, w, h);
    if (!out) return;
    pushEdit();
    if (auto d = markup.doc()) transformShapes(d->shapes, [&](D2D1_POINT_2F p) { return D2D1::Point2F(p.x - x, p.y - y); }, 1);
    doc.replace(out);
    afterPixelEdit();
    editDirty = true;
}

void App::beginAdjust() {
    if (!doc.loaded()) return;
    panel = EditPanel::Adjust;
    adj = Adjust{};
    fxActive = false;
    requestPaint();
}

void App::applyAdjust() {
    panel = EditPanel::None;
    if (adj.identity() || !fxActive) { adj = Adjust{}; fxActive = false; requestPaint(); return; }
    // bake on the unrotated image (rotation stays a view setting)
    auto out = renderToWic(doc.W(), doc.H(), [&](ID2D1DeviceContext* dc) { doc.draw(dc, 1.f, fx.head(), fx.tail()); }, false);
    adj = Adjust{};
    fxActive = false;
    if (!out) { toast(T("save.failed"), true); return; }
    pushEdit();
    int rot = view.rot;
    float fxs = view.fx, fys = view.fy;
    doc.replace(out);
    hist = computeHistogram(out.Get());
    view.rot = rot; view.fx = fxs; view.fy = fys;
    cache.erase(cur.path);
    editDirty = true;
    requestPaint();
}

void App::openResize() {
    if (!doc.loaded()) return;
    bakeOrientationIntoImage();
    modal = Modal{};
    modal.kind = 3;
    fieldW = std::to_wstring(doc.W());
    fieldH = std::to_wstring(doc.H());
    focusField = 0;
    keepRatio = true;
    requestPaint();
}

void App::applyResize() {
    long w = fieldW.empty() ? 0 : std::wcstol(fieldW.c_str(), nullptr, 10), h = fieldH.empty() ? 0 : std::wcstol(fieldH.c_str(), nullptr, 10);
    closeModal();
    if (w < 1 || h < 1 || w > 30000 || h > 30000 || ((UINT)w == doc.W() && (UINT)h == doc.H())) { requestPaint(); return; }
    float sx = (float)w / doc.W(), sy = (float)h / doc.H();
    auto out = resizeBitmap(doc.img.frames[0].Get(), (UINT)w, (UINT)h);
    if (!out) { toast(T("save.failed"), true); return; }
    pushEdit();
    if (auto d = markup.doc()) transformShapes(d->shapes, [&](D2D1_POINT_2F p) { return D2D1::Point2F(p.x * sx, p.y * sy); }, std::sqrt(sx * sy));
    doc.replace(out);
    afterPixelEdit();
    editDirty = true;
}

} // namespace dv
