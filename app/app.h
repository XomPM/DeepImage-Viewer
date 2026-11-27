// DeepImage Viewer — main application state (nomacs DkNoMacs/DkCentralWidget equivalent)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#include "common.h"
#include "bgedit.h"
#include "update.h"
#include "edit.h"
#include "gfx.h"
#include "image.h"
#include "imageview.h"
#include "info.h"
#include "model3d.h"
#include "thumbs.h"
#include "ui.h"
#include <mutex>

namespace dv {

enum : UINT { WM_APP_IMAGE = WM_APP + 1, WM_APP_MODEL, WM_APP_THUMB, WM_APP_PRELOAD, WM_APP_IMAGE_R, WM_APP_MODEL_R, WM_APP_BG, WM_APP_BG_PROGRESS, WM_APP_BG_STAGE, WM_APP_UPD };

struct FileEntry {
    std::wstring path, name, ext;
    Kind kind = Kind::None;
    uint64_t size = 0;
    FILETIME mtime{};
};

enum class Mode { Empty, Image, Model };
enum class EditPanel { None, Crop, Adjust, Bg };
enum class DragKind { None, Pan, Draw, Orbit, PanModel, Crop, MoveShape, BgBrush, BgArea };

struct Toast { std::wstring text; bool error; DWORD t0; };

struct ModalButton { std::wstring label; Action action; bool primary = false; bool danger = false; };
struct Modal {
    int kind = 0;                 // 0 none, 1 settings, 2 about, 3 resize, 4 confirm
    std::wstring title, message;
    std::vector<ModalButton> buttons;
};

class App {
public:
    bool create(HINSTANCE inst, const std::wstring& file);
    LRESULT proc(HWND h, UINT m, WPARAM w, LPARAM l);
    HWND hwnd = nullptr;

    // ---------------------------------------------------------------- state
    std::vector<FileEntry> files;
    std::wstring container; // archive being browsed like a folder ("" = ordinary folder)
    int index = -1;
    FileEntry cur;
    Mode mode = Mode::Empty;
    uint64_t seq = 0;
    bool loading = false;
    DWORD loadStart = 0;
    std::string errorKey;
    std::wstring loadingLabel;

    ImageDoc doc;
    ImageView view;
    Markup markup;
    bool annot = false, editMode = false;
    EditPanel panel = EditPanel::None;
    Adjust adj;
    Effects fx;
    bool fxActive = false;
    std::vector<EditSnapshot> editUndo;
    bool editDirty = false;
    D2D1_RECT_F crop{};
    int cropAspect = 0, cropHandle = -1;
    ExifInfo exif;
    Histogram hist;

    // compare mode: left pane = doc/view/index, right pane = docR/viewR/indexR (zoom & pan stay in sync)
    bool compare = false;
    int indexR = -1;
    ImageDoc docR;
    ImageView viewR;
    uint64_t seqR = 0;
    bool loadingR = false;
    std::string errorR;
    D2D1_RECT_F rcPaneL{}, rcPaneR{};
    static constexpr uint32_t kCmpLeft = 0x5AB4FF, kCmpRight = 0xFF453A;
    void toggleCompare(int on = -1);
    void showRight(int k);
    void setRight(const std::shared_ptr<DecodedImage>& img, const std::wstring& path);
    void goCompare(int d);
    int nextImage(int from, int dir) const;
    template <class F> void eachView(F f) { f(view, compare ? rcPaneL : rcViewport); if (compare && docR.loaded()) f(viewR, rcPaneR); }
    void zoomViews(float factor, float x, float y, bool animate);
    void setFitMode(int m);
    void drawComparePane(ImageDoc& d, ImageView& v, const D2D1_RECT_F& pane, uint32_t color, int idx, bool loadingPane, const std::string& err);
    std::vector<std::pair<int, D2D1_RECT_F>> stripHits;
    float stripFrom = 0;
    DWORD stripT0 = 0;

    ModelView mv;
    ModelView mvR;               // right pane when comparing two 3D files
    bool compareModels = false;  // compare mode is showing 3D files
    void allModels(const std::function<void(ModelView&)>& f); // mv, plus mvR while comparing
    void showRightModel(int k);
    void onModelLoadedR(uint64_t s, DvModel* m, const std::string& err);
    // section view (shared by both panes)
    bool section = false, secFlip = false;
    int secAxis = 2;
    float secPos = .5f;
    void toggleSection(int on = -1);
    void applySection();
    void drawSectionBar();
    // distance measurement
    bool measuring = false;
    struct Measurement { ModelView::Hit a, b; bool right = false; int color = 0; };
    int measColor = 0; // next colour in the measurement palette
    std::vector<Measurement> measures;
    ModelView::Hit measA, hover3d;
    bool measARight = false, hoverRight = false;
    void toggleMeasure(int on = -1);
    // updates (GitHub installs; Store packages update through the Store)
    struct UpdResult { bool ok = false; upd::Info info; std::wstring path; std::string err; };
    int updState = 0;      // 0 idle, 1 checking, 2 up to date, 3 available, 4 downloading, 5 check failed
    upd::Info updInfo;
    bool updBanner = false, updManual = false;
    float updProgress = 0;
    std::shared_ptr<std::atomic<bool>> updCancel;
    void scheduleUpdateCheck();
    void checkForUpdate(bool manual);
    void onUpdateMsg(WPARAM kind, LPARAM l);
    void startUpdate();
    void drawUpdateBar();

    // AI background removal: the AI mask opens the editor panel (EditPanel::Bg) for refinement
    bool bgBusy = false, bgDownloading = false;
    bool bgCutout = false; // current picture had its background removed: show the checkerboard
    float bgProgress = 0;
    uint64_t bgSeq = 0;
    std::shared_ptr<std::atomic<bool>> bgCancel;
    struct BgJob;          // worker result (bgpanel.cpp)
    void removeBackground();
    void startBgJob(int kind, bool download, WICRect roi = {});
    void onBgJob(BgJob* job);
    BgEdit bge;
    int bgTool = 0;        // 0 pan, 1 keep brush, 2 erase brush, 3 pick objects, 4 area
    float bgBrush = 48;    // brush diameter in screen DIPs
    uint64_t bgSession = 0;
    bool bgStageBusy = false, bgStageDirty = false;
    BgParams bgStageReq;   // parameters of the last stage started
    BgEdit::Rect bgStrokeRect;
    D2D1_POINT_2F bgLast{}, bgAreaA{}, bgAreaB{};
    void startBgStage();
    void onBgStage(uint64_t gen, BgStage* st);
    void bgParamsChanged();
    void bgShow(const BgEdit::Rect& r);
    void bgShowAll() { bgShow({0, 0, bge.W(), bge.H()}); }
    void applyBg();
    void closeBg(bool restore);
    void bgUndo();
    void bgPickObject(float x, float y);
    void drawBgPanel();
    void drawBgOverlay();
    bool bgCheckerboard() const { return bgCutout || (panel == EditPanel::Bg && bge.p.bgMode == 0 && !bge.showMask); }
    void drawBgBusy();
    void pickMeasure(float x, float y, bool commit);
    void drawMeasure();
    std::wstring lengthText(float v) const;
    bool hintGone = false;

    ThumbService thumbs;
    std::map<std::wstring, ComPtr<ID2D1Bitmap1>> thumbBmp;
    std::map<std::wstring, bool> thumbFailed;
    float stripScroll = 0, stripTarget = 0;
    bool stripUser = false;

    std::vector<Toast> toasts;
    Modal modal;
    std::wstring fieldW, fieldH;
    int focusField = 0;
    bool keepRatio = true;

    bool fullscreen = false;
    WINDOWPLACEMENT savedPlacement{sizeof(WINDOWPLACEMENT)};
    LONG savedStyle = 0;
    bool dropHover = false;
    bool mouseInViewport = false;
    float infoScroll = 0;
    // auto-hiding chrome: side arrows always; toolbar + filmstrip in the immersive "fill" mode
    struct Reveal {
        DWORD shown = 0, until = 0;
        bool on = false;
        void poke(DWORD now, DWORD ms = 3000) { if (!on || now >= until) shown = now; on = true; until = now + ms; }
        void hide(DWORD now) { if (on && until > now) until = now; }
        float alpha(DWORD now) const {
            if (!on) return 0;
            if (now < until) return std::min(1.f, (now - shown) / 140.f);
            return now - until < 260 ? 1.f - (now - until) / 260.f : 0.f;
        }
        bool animating(DWORD now) const { return on && ((now < until && now - shown < 140) || (now >= until && now - until < 260)); }
    } navRv, barRv, stripRv, pillRv;
    bool immersive() const;
    // slideshow (compare mode: both panes step to their next image)
    bool slideshow = false, slidePaused = false;
    void toggleSlideshow(int on = -1);
    void setSlideDelay(int sec);
    void slideStep();
    void armSlideTimer();
    void drawSlidePill();
    // colour picker (eyedropper)
    bool picking = false, pickShown = false, hoverOk = false;
    uint32_t pickRgb = 0, hoverRgb = 0;
    void togglePicker(int on = -1);
    bool sampleAt(float x, float y, uint32_t& rgb);
    void drawPicker();
    void copyText(const std::wstring& t);
    void togglePin();
    void pokeChrome(float x, float y);
    void armChromeTimer();

    // caption buttons (custom title bar)
    int capHover = 0, capPress = 0;

    // input
    DragKind drag = DragKind::None;
    float lastX = 0, lastY = 0, downX = 0, downY = 0;
    bool spaceDown = false;

    // pending async results
    std::mutex pendMx;
    struct PendingImage { uint64_t seq = 0; bool ok = false; std::shared_ptr<DecodedImage> img; std::string err; std::wstring path; } pendImg;
    std::map<std::wstring, std::shared_ptr<DecodedImage>> cache;  // decoded neighbours
    std::vector<std::wstring> cacheOrder;
    bool preloadBusy = false;
    std::map<std::wstring, bool> preloadFailed;

    ComPtr<ID2D1Bitmap1> markBmp;    // app mark for empty state / about

    // ---------------------------------------------------------------- layout (DIPs)
    D2D1_RECT_F rcTitle{}, rcStage{}, rcViewport{}, rcSide{}, rcStrip{};
    void layout();
    float titleH() const { return fullscreen ? 0.f : 44.f; }

    // ---------------------------------------------------------------- frame
    void paint();
    void requestPaint() { InvalidateRect(hwnd, nullptr, FALSE); }
    bool needsAnimation();
    void ensureTimer();
    bool timerOn = false;

    // drawing (draw.cpp)
    void drawBackground();
    void drawTitlebar();
    void drawEmpty();
    void drawImageStage();
    void drawModelStage();
    void drawGizmo();
    void drawPrintButton();
    void drawToolbars();
    void drawRail();
    void drawCropOverlay();
    void drawAdjustPanel();
    void drawInfoPanel();
    void drawFilmstrip();
    void drawStatus();
    void drawNav();
    void drawLoading();
    void drawModal();
    void drawToasts();
    void drawDrop();
    struct TbItem {
        enum T { Btn, Sep, Zoom, Swatches, Width, Primary } type = Btn;
        const char* icon = nullptr;
        const char* tip = nullptr;
        Action action;
        bool on = false, enabled = true;
        const wchar_t* label = nullptr;
    };
    float toolbarWidth(const std::vector<TbItem>& items);
    void toolbar(uint64_t id, const std::vector<TbItem>& items, float cx, float top);

    // ---------------------------------------------------------------- actions (actions.cpp)
    void openDialog();
    void openPath(const std::wstring& p);
    void listFolder(const std::wstring& p);
    bool openArchive(const std::wstring& archive);
    bool openContainer(const std::wstring& c);
    void goFolder(int d);
    std::wstring homeDir() const; // folder that holds the current file (or its archive)
    void show(int i, bool force = false);
    void go(int d);
    void onImageDecoded();
    void onModelLoaded(uint64_t s, DvModel* m, const std::string& err);
    void startPreload();
    void applyTheme();
    void applyLanguage();
    void toast(const std::wstring& t, bool err = false);
    void toggleAnnot(int on = -1);
    void toggleEdit(int on = -1);
    void toggleFullscreen();
    void toggleInfo();
    void setTool(Tool t);
    void saveAs();
    void copyImage();
    void open3dPrint();
    void saveScreenshot();
    void refreshFolder();
    ComPtr<IWICBitmap> composite(bool white, bool withMarkup = true, bool withEffects = true);
    ComPtr<IWICBitmap> compositeRight();
    void pushEdit();
    void undoEdit();
    void bakeOrientationIntoImage();
    void beginCrop();
    void applyCrop();
    void beginAdjust();
    void applyAdjust();
    void openResize();
    void applyResize();
    void afterPixelEdit();
    void confirm(const std::wstring& title, const std::wstring& msg, std::vector<ModalButton> buttons);
    void guardUnsaved(Action proceed);
    uint64_t cacheBytes = 0;
    void openSettings() { modal = Modal{}; modal.kind = 1; cacheBytes = thumbCacheBytes(); }
    void openAbout() { modal = Modal{}; modal.kind = 2; }
    void closeModal() { modal = Modal{}; }
    void updateTitle();
    std::wstring statusText();
    void requestThumbs();

    // ---------------------------------------------------------------- input (window.cpp)
    void onMouseDown(float x, float y, int button, WPARAM keys);
    void onMouseMove(float x, float y, WPARAM keys);
    void onMouseUp(float x, float y, int button);
    void onWheel(float x, float y, float delta, bool ctrl, bool horizontal);
    void onDoubleClick(float x, float y);
    bool onKey(UINT vk, bool down);
    void onChar(wchar_t c);
    int cropHitTest(float x, float y);
    LRESULT hitTestNC(int x, int y);
};

extern App A;

} // namespace dv
