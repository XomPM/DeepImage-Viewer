// DeepImage Viewer — image viewport transform + markup
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#include "common.h"
#include "image.h"

namespace dv {

// screen = T + R(rot) * S(scale*flip) * (img - size/2)   (same model as nomacs DkBaseViewPort)
class ImageView {
public:
    float s = 1, tx = 0, ty = 0;
    int rot = 0;            // 0, 90, 180, 270
    float fx = 1, fy = 1;
    float imgW = 0, imgH = 0;
    D2D1_RECT_F vp{};       // viewport in DIPs
    float padT = 66, padB = 52, padL = 72, padR = 72;

    // fit modes (remembered in settings): 0 whole image, 1 fill screen, 2 fit width, 3 fit height, 4 actual size
    int mode = 0;
    bool fitted = true;     // true while the view still follows the fit mode (window resizes re-apply it)
    void setImage(float w, float h) { imgW = w; imgH = h; rot = 0; fx = fy = 1; applyFit(mode, false, true); }
    void setViewport(const D2D1_RECT_F& r);
    void rotatedSize(float& w, float& h) const;
    float fitScale() const;
    float modeScale(int m, bool initial) const;
    void fitCenter(float& cx, float& cy) const;
    void applyFit(int m, bool animate, bool initial = false);
    void fit(bool animate = true) { applyFit(mode, animate); }
    void fitWhole(bool animate = true) { applyFit(0, animate); }
    void actual(bool animate = true) { applyFit(4, animate); }
    void zoomTo(float ns, float px, float py, bool animate = false);
    void zoomBy(float f, float px, float py, bool animate = false) { zoomTo(target() * f, px, py, animate); }
    float target() const { return anim_ ? to_[0] : s; }
    void rotate(int deg);
    void flip(bool horizontal);
    void panBy(float dx, float dy);
    bool animating() const { return anim_; }
    bool step();            // advance animation; true while running
    D2D1::Matrix3x2F matrix() const;
    D2D1_POINT_2F toImage(float x, float y) const;
    D2D1_POINT_2F toScreen(float u, float v) const;

private:
    void go(float ns, float ntx, float nty, bool animate);
    void constrain();
    bool anim_ = false;
    DWORD t0_ = 0;
    float from_[3]{}, to_[3]{};
};

enum class Tool { Hand, Pen, Hl, Line, Arrow, Rect, Ellipse, Eraser, Mosaic, Mask };

struct Shape {
    Tool type;
    D2D1_COLOR_F color;
    float width;             // image px
    bool fill = false;
    std::vector<D2D1_POINT_2F> pts; // pen/hl
    D2D1_POINT_2F a{}, b{};
    // mosaic: averaged cells of the source image, rebuilt when geometry or the source changes
    mutable std::vector<uint32_t> cells;
    mutable int cols = 0, rows = 0, cacheVer = -1;
    mutable float cacheCell = 0;
    mutable D2D1_RECT_F cacheRect{}, cellOrigin{};
};

struct ToolStyle { D2D1_COLOR_F color; float width; bool fill; };

class Markup {
public:
    Tool tool = Tool::Pen;
    std::map<Tool, ToolStyle> style;
    Markup();

    struct Doc { std::vector<std::shared_ptr<Shape>> shapes; std::vector<std::vector<std::shared_ptr<Shape>>> undo, redo; bool dirty = false; };
    std::map<std::wstring, Doc> docs;
    std::wstring key;
    Doc* doc() { return key.empty() ? nullptr : &docs[key]; }
    bool hasMarks(const std::wstring& k) const { auto it = docs.find(k); return it != docs.end() && !it->second.shapes.empty(); }
    bool dirty(const std::wstring& k) const { auto it = docs.find(k); return it != docs.end() && it->second.dirty && !it->second.shapes.empty(); }
    void markSaved() { if (auto d = doc()) d->dirty = false; }
    void dropDoc(const std::wstring& k) { docs.erase(k); }

    // source pixels for the mosaic tool (current upright image frame)
    void setSource(ComPtr<IWICBitmap> b) { src_ = std::move(b); ++srcVer_; }

    // input in image coordinates; k = 1/scale (screen px -> image px)
    void begin(D2D1_POINT_2F p, float k);
    void move(D2D1_POINT_2F p, float k, bool shift);
    void end();
    bool drawing() const { return cur_ != nullptr || erasing_; }

    // selection (hand tool): pick a shape under p, drag it, restyle or delete it
    std::shared_ptr<Shape> shapeAt(D2D1_POINT_2F p, float k);
    bool pick(D2D1_POINT_2F p, float k);
    void beginMove(D2D1_POINT_2F p);
    void moveTo(D2D1_POINT_2F p);
    void endMove();
    bool moving() const { return moving_; }
    std::shared_ptr<Shape> selection();
    void deselect() { sel_.reset(); }
    void deleteSelection();
    // colour / width of the selection (hand tool) or of the active tool; width in screen px (k = 1/scale)
    D2D1_COLOR_F curColor();
    float curWidth(float k);
    void setColor(D2D1_COLOR_F c);
    void setWidth(float w, float k);
    void endStyleEdit() { selOwned_ = false; }
    Tool styleTool();        // tool whose controls are shown (selection type for the hand tool)

    void undo();
    void redo();
    void clear();
    bool canUndo() { auto d = doc(); return d && !d->undo.empty(); }
    bool canRedo() { auto d = doc(); return d && !d->redo.empty(); }
    size_t count() { auto d = doc(); return d ? d->shapes.size() : 0; }

    // draws markup in image space (caller sets transform); px > 0 also draws the selection frame (px = 1 screen px in image units)
    void draw(ID2D1DeviceContext* dc, bool includeCurrent = true, float px = 0);
    ToolStyle& ts() { return style[tool]; }

private:
    void commit(std::vector<std::shared_ptr<Shape>> next);
    void ownSelection();
    void drawMosaic(ID2D1DeviceContext* dc, const Shape& s);
    std::shared_ptr<Shape> cur_, sel_;
    bool erasing_ = false, moving_ = false, selOwned_ = false, moved_ = false;
    D2D1_POINT_2F moveLast_{};
    std::vector<std::shared_ptr<Shape>> erased_;
    ComPtr<IWICBitmap> src_;
    int srcVer_ = 0;
};

} // namespace dv
