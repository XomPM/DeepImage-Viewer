// DeepImage Viewer — main application state (nomacs DkNoMacs/DkCentralWidget equivalent)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#include "common.h"
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

enum : UINT { WM_APP_IMAGE = WM_APP + 1, WM_APP_MODEL, WM_APP_THUMB, WM_APP_PRELOAD, WM_APP_IMAGE_R };

struct FileEntry {
    std::wstring path, name, ext;
    Kind kind = Kind::None;
    uint64_t size = 0;
    FILETIME mtime{};
};

enum class Mode { Empty, Image, Model };
enum class EditPanel { None, Crop, Adjust };
enum class DragKind { None, Pan, Draw, Orbit, PanModel, Crop, MoveShape };

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
    void openSettings() { modal = Modal{}; modal.kind = 1; }
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
