// DeepImage Viewer — background-removal editor: refines the AI mask and composes the result
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
//
// Plain C++ (no Windows API) so the pixel pipeline can be tested on its own.
//   stage (worker thread, when a slider changes):
//     AI mask → strength curve → [chosen objects only] → edge shift (min/max) → feather (blur)
//     + foreground colours with the background colour removed from soft edges ("decontamination")
//     + blurred background estimate for the "blur" background
//   compose (UI thread, whole picture or just a brush's dirty rectangle):
//     alpha = stage mask, then the erase / keep brushes on top; background transparent, colour or blur
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace dv {

struct BgParams {
    int strength = 0;     // -100..100: >0 removes more of the uncertain parts, <0 keeps more
    int edge = 0;         // -30..30 px: shrink (<0) or grow (>0) the cut-out
    int feather = 0;      // 0..30 px: soften the edge
    bool decontam = true; // remove the background colour that bleeds into soft edges
    int bgMode = 0;       // 0 transparent, 1 colour, 2 blurred original
    uint32_t bgColor = 0xFFFFFF; // 0xRRGGBB
    int blur = 50;        // 0..100 (bgMode 2)
    std::vector<std::pair<int, int>> objects; // picture points: keep only the objects under them (empty = all)
    bool stageEquals(const BgParams& o) const {
        return strength == o.strength && edge == o.edge && feather == o.feather && decontam == o.decontam && objects == o.objects &&
               (bgMode == 2) == (o.bgMode == 2) && (bgMode != 2 || blur == o.blur);
    }
};

struct BgGrid { // low-resolution BGR image, sampled bilinearly
    int w = 0, h = 0, k = 1;
    std::vector<float> c; // w*h*3
    void sample(int x, int y, float out[3]) const;
};

struct BgStage {
    std::vector<uint8_t> mask;     // refined AI mask, W*H
    std::vector<uint8_t> fg;       // straight BGR foreground colours, W*H*3
    std::vector<uint32_t> labels;  // object number per pixel (0 = background), W*H
    std::vector<uint8_t> blurBg;   // bgMode 2: blurred background, BGR W*H*3
};

// immutable inputs shared with the worker
struct BgSource {
    int W = 0, H = 0;
    std::vector<uint8_t> px; // premultiplied BGRA
};

std::unique_ptr<BgStage> bgComputeStage(const BgSource& src, const std::vector<uint8_t>& ai, const BgParams& p);

class BgEdit {
public:
    std::shared_ptr<const BgSource> src;
    std::shared_ptr<const std::vector<uint8_t>> ai; // AI mask (W*H)
    std::unique_ptr<BgStage> stage;
    std::vector<uint8_t> keep, erase; // brushes (allocated on first use)
    std::vector<uint8_t> out;         // composed premultiplied BGRA
    BgParams p;
    bool showMask = false;            // red tint over the removed parts instead of the result

    int W() const { return src ? src->W : 0; }
    int H() const { return src ? src->H : 0; }
    // composes [x0,x1) x [y0,y1) into out
    void compose(int x0, int y0, int x1, int y1);
    void composeAll() { compose(0, 0, W(), H()); }
    // brush dab along a segment (picture coordinates); returns the dirty rectangle
    struct Rect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool empty() const { return x1 <= x0 || y1 <= y0; } };
    Rect stroke(float ax, float ay, float bx, float by, float radius, bool keepBrush);
    // object number at a picture point (searches a few pixels around); 0 = none
    uint32_t labelAt(int x, int y, int radius) const;

    // undo for brush strokes, area runs and object picks
    struct Undo {
        int kind = 0; // 0 brush patch, 1 AI mask, 2 objects
        Rect r;
        std::vector<uint8_t> keep, erase;
        std::shared_ptr<const std::vector<uint8_t>> ai;
        std::vector<std::pair<int, int>> objects;
    };
    std::vector<Undo> undo;
    void beginStroke();            // snapshot for the stroke's undo patch
    void endStroke(const Rect& r); // r = union of the stroke's dirty rectangles
    void pushUndo(Undo u) { undo.push_back(std::move(u)); if (undo.size() > 30) undo.erase(undo.begin()); }
private:
    std::vector<uint8_t> keep0_, erase0_;
};

} // namespace dv
