// DeepImage Viewer — image viewport + markup implementation
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "imageview.h"
#include "gfx.h"

namespace dv {

static float ease(float x) { return 1 - std::pow(1 - x, 3.f); }

void ImageView::setViewport(const D2D1_RECT_F& r) {
    D2D1_RECT_F old = vp;
    // layout() runs every frame: only react to real changes, or every zoom/pan on a fitted view would be undone
    if (std::fabs(r.left - old.left) < .25f && std::fabs(r.top - old.top) < .25f && std::fabs(r.right - old.right) < .25f &&
        std::fabs(r.bottom - old.bottom) < .25f)
        return;
    vp = r;
    if (imgW <= 0) return;
    if (fitted || rw(old) <= 0) applyFit(mode, false, true);
    else {
        float dx = (r.left + r.right - old.left - old.right) / 2, dy = (r.top + r.bottom - old.top - old.bottom) / 2;
        tx += dx; ty += dy;
        if (anim_) { to_[1] += dx; to_[2] += dy; from_[1] += dx; from_[2] += dy; }
    }
}
void ImageView::rotatedSize(float& w, float& h) const {
    if (rot % 180) { w = imgH; h = imgW; } else { w = imgW; h = imgH; }
}
float ImageView::fitScale() const {
    float w, h;
    rotatedSize(w, h);
    if (w <= 0 || h <= 0) return 1;
    return std::max(0.001f, std::min((rw(vp) - padL - padR) / w, (rh(vp) - padT - padB) / h));
}
float ImageView::modeScale(int m, bool initial) const {
    float w, h;
    rotatedSize(w, h);
    if (w <= 0 || h <= 0) return 1;
    switch (m) {
    case 1: return fitScale(); // whole image as large as the window allows (small images are enlarged)
    case 2: return std::max(0.001f, rw(vp) / w);
    case 3: return std::max(0.001f, rh(vp) / h);
    case 4: return 1.f;
    default: { float f = fitScale(); return initial ? std::min(1.f, f) : f; }
    }
}
void ImageView::fitCenter(float& cx, float& cy) const {
    cx = vp.left + padL + (rw(vp) - padL - padR) / 2;
    cy = vp.top + padT + (rh(vp) - padT - padB) / 2;
}
void ImageView::applyFit(int m, bool animate, bool initial) {
    float ns = modeScale(m, initial);
    float w, h, cx, cy;
    rotatedSize(w, h);
    fitCenter(cx, cy);
    if (m >= 2) { cx = (vp.left + vp.right) / 2; cy = (vp.top + vp.bottom) / 2; }
    // long images: start at the top (fit width) or the left edge (fit height)
    if (m == 2 && h * ns > rh(vp)) cy = vp.top + h * ns / 2;
    if (m == 3 && w * ns > rw(vp)) cx = vp.left + w * ns / 2;
    go(ns, cx, cy, animate);
    fitted = true;
}
void ImageView::zoomTo(float ns, float px, float py, bool animate) {
    ns = clampf(ns, 0.01f, 64.f);
    // zoom relative to where the view is heading so repeated button presses accumulate during the animation
    float cs = target(), ctx = anim_ ? to_[1] : tx, cty = anim_ ? to_[2] : ty;
    float k = ns / cs;
    float ntx = px + (ctx - px) * k, nty = py + (cty - py) * k;
    float w, h, cx, cy;
    rotatedSize(w, h);
    fitCenter(cx, cy);
    if (w * ns <= rw(vp)) ntx = cx;
    if (h * ns <= rh(vp)) nty = cy;
    fitted = false;
    go(ns, ntx, nty, animate);
}
void ImageView::rotate(int deg) { rot = ((rot + deg) % 360 + 360) % 360; applyFit(mode, false, true); }
void ImageView::flip(bool horizontal) {
    bool r90 = rot % 180 != 0;
    if (horizontal != r90) fx = -fx; else fy = -fy;
}
void ImageView::panBy(float dx, float dy) {
    if (anim_) { s = to_[0]; tx = to_[1]; ty = to_[2]; anim_ = false; }
    tx += dx; ty += dy; constrain();
}
void ImageView::constrain() {
    float w, h;
    rotatedSize(w, h);
    float W = w * s, H = h * s, m = 64;
    auto c = [&](float& t, float size, float lo, float len) {
        if (size <= len) t = clampf(t, lo + size / 2 - m, lo + len - size / 2 + m);
        else t = clampf(t, lo + len - size / 2 - m, lo + size / 2 + m);
    };
    c(tx, W, vp.left, rw(vp));
    c(ty, H, vp.top, rh(vp));
}
void ImageView::go(float ns, float ntx, float nty, bool animate) {
    if (!animate) { s = ns; tx = ntx; ty = nty; anim_ = false; return; }
    from_[0] = s; from_[1] = tx; from_[2] = ty;
    to_[0] = ns; to_[1] = ntx; to_[2] = nty;
    t0_ = GetTickCount();
    anim_ = true;
}
bool ImageView::step() {
    if (!anim_) return false;
    float p = ease(clampf((GetTickCount() - t0_) / 180.f, 0, 1));
    s = std::exp(std::log(from_[0]) + (std::log(to_[0]) - std::log(from_[0])) * p);
    tx = from_[1] + (to_[1] - from_[1]) * p;
    ty = from_[2] + (to_[2] - from_[2]) * p;
    if (p >= 1) anim_ = false;
    return true;
}
D2D1::Matrix3x2F ImageView::matrix() const {
    return D2D1::Matrix3x2F::Translation(-imgW / 2, -imgH / 2) * D2D1::Matrix3x2F::Scale(s * fx, s * fy) *
           D2D1::Matrix3x2F::Rotation((float)rot) * D2D1::Matrix3x2F::Translation(tx, ty);
}
D2D1_POINT_2F ImageView::toImage(float x, float y) const {
    auto m = matrix();
    m.Invert();
    return m.TransformPoint(D2D1::Point2F(x, y));
}
D2D1_POINT_2F ImageView::toScreen(float u, float v) const { return matrix().TransformPoint(D2D1::Point2F(u, v)); }

// ======================================================================== Markup
Markup::Markup() {
    style[Tool::Pen] = {rgba(0xFF3B30), 4, false};
    style[Tool::Hl] = {rgba(0xFFE14D), 22, false};
    style[Tool::Line] = {rgba(0xFF3B30), 4, false};
    style[Tool::Arrow] = {rgba(0xFF3B30), 4, false};
    style[Tool::Rect] = {rgba(0xFF3B30), 4, false};
    style[Tool::Ellipse] = {rgba(0xFF3B30), 4, false};
    style[Tool::Eraser] = {rgba(0xFFFFFF), 18, false};
    style[Tool::Hand] = {rgba(0xFFFFFF), 4, false};
    style[Tool::Mosaic] = {rgba(0xFFFFFF), 14, false};
    style[Tool::Mask] = {rgba(0x1C1C1E), 4, true};
}

void Markup::commit(std::vector<std::shared_ptr<Shape>> next) {
    auto d = doc();
    if (!d) return;
    d->undo.push_back(d->shapes);
    if (d->undo.size() > 200) d->undo.erase(d->undo.begin());
    d->shapes = std::move(next);
    d->redo.clear();
    d->dirty = true;
}
void Markup::undo() { auto d = doc(); sel_.reset(); selOwned_ = false; if (!d || d->undo.empty()) return; d->redo.push_back(d->shapes); d->shapes = d->undo.back(); d->undo.pop_back(); d->dirty = true; }
void Markup::redo() { auto d = doc(); sel_.reset(); selOwned_ = false; if (!d || d->redo.empty()) return; d->undo.push_back(d->shapes); d->shapes = d->redo.back(); d->redo.pop_back(); d->dirty = true; }
void Markup::clear() { auto d = doc(); sel_.reset(); if (d && !d->shapes.empty()) commit({}); }

static float distSeg(D2D1_POINT_2F p, D2D1_POINT_2F a, D2D1_POINT_2F b) {
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 ? clampf(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0, 1) : 0;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}
static bool hit(const Shape& s, D2D1_POINT_2F p, float r) {
    float tol = r + s.width / 2;
    switch (s.type) {
    case Tool::Pen: case Tool::Hl:
        if (s.pts.size() == 1) return std::hypot(p.x - s.pts[0].x, p.y - s.pts[0].y) <= tol;
        for (size_t i = 1; i < s.pts.size(); ++i) if (distSeg(p, s.pts[i - 1], s.pts[i]) <= tol) return true;
        return false;
    case Tool::Line: case Tool::Arrow: return distSeg(p, s.a, s.b) <= tol;
    case Tool::Rect: {
        float x0 = std::min(s.a.x, s.b.x), x1 = std::max(s.a.x, s.b.x), y0 = std::min(s.a.y, s.b.y), y1 = std::max(s.a.y, s.b.y);
        if (s.fill && p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) return true;
        D2D1_POINT_2F c[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        for (int i = 0; i < 4; ++i) if (distSeg(p, c[i], c[(i + 1) % 4]) <= tol) return true;
        return false;
    }
    case Tool::Mosaic: case Tool::Mask: {
        float x0 = std::min(s.a.x, s.b.x), x1 = std::max(s.a.x, s.b.x), y0 = std::min(s.a.y, s.b.y), y1 = std::max(s.a.y, s.b.y);
        return p.x >= x0 - r && p.x <= x1 + r && p.y >= y0 - r && p.y <= y1 + r;
    }
    case Tool::Ellipse: {
        float cx = (s.a.x + s.b.x) / 2, cy = (s.a.y + s.b.y) / 2;
        float rx = std::max(1.f, std::fabs(s.b.x - s.a.x) / 2), ry = std::max(1.f, std::fabs(s.b.y - s.a.y) / 2);
        float d = std::hypot((p.x - cx) / rx, (p.y - cy) / ry);
        if (s.fill && d <= 1) return true;
        return std::fabs(d - 1) * std::min(rx, ry) <= tol;
    }
    default: return false;
    }
}

void Markup::begin(D2D1_POINT_2F p, float k) {
    if (!doc() || tool == Tool::Hand) return;
    if (tool == Tool::Eraser) {
        erasing_ = true;
        erased_.clear();
        move(p, k, false);
        return;
    }
    auto& st = style[tool];
    cur_ = std::make_shared<Shape>();
    cur_->type = tool;
    cur_->color = st.color;
    cur_->width = st.width * k;
    cur_->fill = st.fill;
    cur_->pts.push_back(p);
    cur_->a = cur_->b = p;
}
void Markup::move(D2D1_POINT_2F p, float k, bool shift) {
    if (erasing_) {
        float r = style[Tool::Eraser].width * k / 2;
        for (auto& s : doc()->shapes)
            if (std::find(erased_.begin(), erased_.end(), s) == erased_.end() && hit(*s, p, r)) erased_.push_back(s);
        return;
    }
    if (!cur_) return;
    if (cur_->type == Tool::Pen || cur_->type == Tool::Hl) {
        auto& l = cur_->pts.back();
        if (std::hypot(p.x - l.x, p.y - l.y) / k >= 1.2f) cur_->pts.push_back(p);
        if (shift && cur_->type == Tool::Hl) cur_->pts = {cur_->pts.front(), p};
        return;
    }
    if (shift) {
        float dx = p.x - cur_->a.x, dy = p.y - cur_->a.y;
        if (cur_->type == Tool::Rect || cur_->type == Tool::Ellipse || cur_->type == Tool::Mosaic || cur_->type == Tool::Mask) {
            float m = std::max(std::fabs(dx), std::fabs(dy));
            p = {cur_->a.x + (dx < 0 ? -m : m), cur_->a.y + (dy < 0 ? -m : m)};
        } else {
            float ang = std::round(std::atan2(dy, dx) / (3.14159265f / 4)) * (3.14159265f / 4), len = std::hypot(dx, dy);
            p = {cur_->a.x + std::cos(ang) * len, cur_->a.y + std::sin(ang) * len};
        }
    }
    cur_->b = p;
}
void Markup::end() {
    if (erasing_) {
        erasing_ = false;
        if (!erased_.empty()) {
            std::vector<std::shared_ptr<Shape>> next;
            for (auto& s : doc()->shapes) if (std::find(erased_.begin(), erased_.end(), s) == erased_.end()) next.push_back(s);
            commit(next);
        }
        erased_.clear();
        return;
    }
    if (!cur_) return;
    auto c = cur_;
    cur_.reset();
    if (c->type == Tool::Mosaic || c->type == Tool::Mask) {
        if (std::fabs(c->b.x - c->a.x) < 2 || std::fabs(c->b.y - c->a.y) < 2) return;
    } else if (c->type != Tool::Pen && c->type != Tool::Hl && std::hypot(c->b.x - c->a.x, c->b.y - c->a.y) < c->width * 0.8f) return;
    auto next = doc()->shapes;
    next.push_back(c);
    commit(next);
}

static void drawShape(ID2D1DeviceContext* dc, const Shape& s) {
    ComPtr<ID2D1SolidColorBrush> br;
    dc->CreateSolidColorBrush(s.color, &br);
    ComPtr<ID2D1Factory> f;
    dc->GetFactory(&f);
    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
        s.type == Tool::Hl ? D2D1_CAP_STYLE_SQUARE : D2D1_CAP_STYLE_ROUND, s.type == Tool::Hl ? D2D1_CAP_STYLE_SQUARE : D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    ComPtr<ID2D1StrokeStyle> ss;
    f->CreateStrokeStyle(sp, nullptr, 0, &ss);
    switch (s.type) {
    case Tool::Pen: case Tool::Hl: {
        if (s.pts.size() == 1) {
            if (s.type == Tool::Hl) dc->FillRectangle(R(s.pts[0].x - s.width / 2, s.pts[0].y - s.width / 2, s.pts[0].x + s.width / 2, s.pts[0].y + s.width / 2), br.Get());
            else dc->FillEllipse(D2D1::Ellipse(s.pts[0], s.width / 2, s.width / 2), br.Get());
            break;
        }
        ComPtr<ID2D1PathGeometry> g;
        f->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> k;
        g->Open(&k);
        k->BeginFigure(s.pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
        for (size_t i = 1; i + 1 < s.pts.size(); ++i) {
            D2D1_POINT_2F m{(s.pts[i].x + s.pts[i + 1].x) / 2, (s.pts[i].y + s.pts[i + 1].y) / 2};
            k->AddQuadraticBezier(D2D1::QuadraticBezierSegment(s.pts[i], m));
        }
        k->AddLine(s.pts.back());
        k->EndFigure(D2D1_FIGURE_END_OPEN);
        k->Close();
        dc->DrawGeometry(g.Get(), br.Get(), s.width, ss.Get());
        break;
    }
    case Tool::Line:
        dc->DrawLine(s.a, s.b, br.Get(), s.width, ss.Get());
        break;
    case Tool::Arrow: {
        float ang = std::atan2(s.b.y - s.a.y, s.b.x - s.a.x), len = std::hypot(s.b.x - s.a.x, s.b.y - s.a.y);
        float h = std::min(std::max(s.width * 3.6f, 10.f), len * 0.6f);
        D2D1_POINT_2F e{s.b.x - std::cos(ang) * h * 0.6f, s.b.y - std::sin(ang) * h * 0.6f};
        dc->DrawLine(s.a, e, br.Get(), s.width, ss.Get());
        ComPtr<ID2D1PathGeometry> g;
        f->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> k;
        g->Open(&k);
        k->BeginFigure(s.b, D2D1_FIGURE_BEGIN_FILLED);
        k->AddLine({s.b.x - h * std::cos(ang - 0.42f), s.b.y - h * std::sin(ang - 0.42f)});
        k->AddLine({s.b.x - h * std::cos(ang + 0.42f), s.b.y - h * std::sin(ang + 0.42f)});
        k->EndFigure(D2D1_FIGURE_END_CLOSED);
        k->Close();
        dc->FillGeometry(g.Get(), br.Get());
        dc->DrawGeometry(g.Get(), br.Get(), std::max(1.f, s.width * 0.5f), ss.Get());
        break;
    }
    case Tool::Rect: {
        D2D1_RECT_F r = R(std::min(s.a.x, s.b.x), std::min(s.a.y, s.b.y), std::max(s.a.x, s.b.x), std::max(s.a.y, s.b.y));
        float rad = std::min({s.width * 1.2f, rw(r) / 2, rh(r) / 2});
        auto rr = D2D1::RoundedRect(r, rad, rad);
        if (s.fill) { br->SetOpacity(0.22f); dc->FillRoundedRectangle(rr, br.Get()); br->SetOpacity(1); }
        dc->DrawRoundedRectangle(rr, br.Get(), s.width, ss.Get());
        break;
    }
    case Tool::Ellipse: {
        auto e = D2D1::Ellipse({(s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2}, std::fabs(s.b.x - s.a.x) / 2, std::fabs(s.b.y - s.a.y) / 2);
        if (s.fill) { br->SetOpacity(0.22f); dc->FillEllipse(e, br.Get()); br->SetOpacity(1); }
        dc->DrawEllipse(e, br.Get(), s.width, ss.Get());
        break;
    }
    case Tool::Mask: {
        D2D1_RECT_F r = R(std::min(s.a.x, s.b.x), std::min(s.a.y, s.b.y), std::max(s.a.x, s.b.x), std::max(s.a.y, s.b.y));
        dc->FillRectangle(r, br.Get());
        break;
    }
    default: break;
    }
}

static D2D1_RECT_F shapeBounds(const Shape& s) {
    D2D1_RECT_F r;
    if (s.type == Tool::Pen || s.type == Tool::Hl) {
        r = R(s.pts[0].x, s.pts[0].y, s.pts[0].x, s.pts[0].y);
        for (auto& p : s.pts) { r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y); r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y); }
    } else r = R(std::min(s.a.x, s.b.x), std::min(s.a.y, s.b.y), std::max(s.a.x, s.b.x), std::max(s.a.y, s.b.y));
    if (s.type != Tool::Mosaic && s.type != Tool::Mask) { float h = s.width / 2; r = R(r.left - h, r.top - h, r.right + h, r.bottom + h); }
    return r;
}

void Markup::drawMosaic(ID2D1DeviceContext* dc, const Shape& s) {
    if (!src_) return;
    UINT W = 0, H = 0;
    src_->GetSize(&W, &H);
    D2D1_RECT_F r = R(std::min(s.a.x, s.b.x), std::min(s.a.y, s.b.y), std::max(s.a.x, s.b.x), std::max(s.a.y, s.b.y));
    D2D1_RECT_F c = R(std::max(0.f, r.left), std::max(0.f, r.top), std::min((float)W, r.right), std::min((float)H, r.bottom));
    if (rw(c) < 1 || rh(c) < 1) return;
    float cell = std::max(2.f, s.width);
    bool stale = s.cacheVer != srcVer_ || s.cacheCell != cell || memcmp(&s.cacheRect, &c, sizeof c) != 0;
    if (stale) {
        // cells are anchored to the image grid so moving the box does not shimmer
        float gx0 = std::floor(c.left / cell) * cell, gy0 = std::floor(c.top / cell) * cell;
        int cols = (int)std::ceil((c.right - gx0) / cell), rows = (int)std::ceil((c.bottom - gy0) / cell);
        if (cols <= 0 || rows <= 0 || (int64_t)cols * rows > 250000) return;
        s.cells.assign((size_t)cols * rows, 0);
        UINT x0 = (UINT)std::max(0.f, gx0), y0 = (UINT)std::max(0.f, gy0);
        UINT x1 = (UINT)std::min((float)W, gx0 + cols * cell), y1 = (UINT)std::min((float)H, gy0 + rows * cell);
        WICRect lr{(INT)x0, (INT)y0, (INT)(x1 - x0), (INT)(y1 - y0)};
        ComPtr<IWICBitmapLock> lock;
        if (lr.Width <= 0 || lr.Height <= 0 || FAILED(src_->Lock(&lr, WICBitmapLockRead, &lock))) return;
        UINT stride = 0, size = 0;
        BYTE* px = nullptr;
        lock->GetStride(&stride);
        lock->GetDataPointer(&size, &px);
        int step = std::max(1, (int)(cell / 6));
        for (int ry = 0; ry < rows; ++ry)
            for (int cx = 0; cx < cols; ++cx) {
                int ax = (int)(gx0 + cx * cell) - (int)x0, ay = (int)(gy0 + ry * cell) - (int)y0;
                int bx = std::min((int)(x1 - x0), (int)(ax + cell)), by = std::min((int)(y1 - y0), (int)(ay + cell));
                ax = std::max(0, ax);
                ay = std::max(0, ay);
                uint64_t sum[4] = {}, n = 0;
                for (int y = ay; y < by; y += step) {
                    const BYTE* row = px + (size_t)y * stride;
                    for (int x = ax; x < bx; x += step) {
                        const BYTE* q = row + x * 4;
                        sum[0] += q[0]; sum[1] += q[1]; sum[2] += q[2]; sum[3] += q[3];
                        ++n;
                    }
                }
                if (n) {
                    // opaque cells so the original detail can never show through
                    uint32_t a = (uint32_t)(sum[3] / n);
                    auto ch = [&](uint64_t v) { return a ? (uint32_t)std::min<uint64_t>(255, v / n * 255 / a) : 0u; };
                    s.cells[(size_t)ry * cols + cx] = 0xFF000000u | (ch(sum[2]) << 16) | (ch(sum[1]) << 8) | ch(sum[0]);
                }
            }
        s.cols = cols;
        s.rows = rows;
        s.cellOrigin = R(gx0, gy0, gx0 + cols * cell, gy0 + rows * cell);
        s.cacheVer = srcVer_;
        s.cacheCell = cell;
        s.cacheRect = c;
    }
    if (!s.cols) return;
    ComPtr<ID2D1Bitmap> bmp;
    if (FAILED(dc->CreateBitmap(D2D1::SizeU(s.cols, s.rows), s.cells.data(), s.cols * 4,
                                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bmp)))
        return;
    dc->PushAxisAlignedClip(c, D2D1_ANTIALIAS_MODE_ALIASED);
    dc->DrawBitmap(bmp.Get(), s.cellOrigin, 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    dc->PopAxisAlignedClip();
}

void Markup::draw(ID2D1DeviceContext* dc, bool includeCurrent, float px) {
    auto d = doc();
    if (!d) return;
    std::vector<std::shared_ptr<Shape>> list;
    for (auto& s : d->shapes) if (std::find(erased_.begin(), erased_.end(), s) == erased_.end()) list.push_back(s);
    if (includeCurrent && cur_) list.push_back(cur_);
    // mosaic and masks hide content, so they go underneath the annotations
    for (auto& s : list) if (s->type == Tool::Mosaic) drawMosaic(dc, *s);
    for (auto& s : list) if (s->type == Tool::Mask) drawShape(dc, *s);
    // highlighter strokes go in one translucent layer so overlaps do not stack
    bool anyHl = false;
    for (auto& s : list) if (s->type == Tool::Hl) anyHl = true;
    if (anyHl) {
        dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 0.45f),
                      nullptr);
        for (auto& s : list) if (s->type == Tool::Hl) drawShape(dc, *s);
        dc->PopLayer();
    }
    for (auto& s : list) if (s->type != Tool::Hl && s->type != Tool::Mosaic && s->type != Tool::Mask) drawShape(dc, *s);
    if (px > 0 && sel_ && std::find(d->shapes.begin(), d->shapes.end(), sel_) != d->shapes.end()) {
        D2D1_RECT_F r = shapeBounds(*sel_);
        r = R(r.left - 5 * px, r.top - 5 * px, r.right + 5 * px, r.bottom + 5 * px);
        ComPtr<ID2D1SolidColorBrush> w, k;
        dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, .95f), &w);
        dc->CreateSolidColorBrush(D2D1::ColorF(0.04f, 0.52f, 1.f, 1), &k);
        ComPtr<ID2D1Factory> f;
        dc->GetFactory(&f);
        float dash[] = {4, 3};
        ComPtr<ID2D1StrokeStyle> ds;
        f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER,
                                                         10, D2D1_DASH_STYLE_CUSTOM), dash, 2, &ds);
        dc->DrawRectangle(r, w.Get(), 3 * px);
        dc->DrawRectangle(r, k.Get(), 1.5f * px, ds.Get());
        D2D1_POINT_2F cs[4] = {{r.left, r.top}, {r.right, r.top}, {r.right, r.bottom}, {r.left, r.bottom}};
        for (auto& c : cs) {
            D2D1_RECT_F h = R(c.x - 3.5f * px, c.y - 3.5f * px, c.x + 3.5f * px, c.y + 3.5f * px);
            dc->FillRectangle(h, w.Get());
            dc->DrawRectangle(h, k.Get(), 1.2f * px);
        }
    }
}

// ------------------------------------------------------------------------ selection
std::shared_ptr<Shape> Markup::selection() {
    auto d = doc();
    if (!d || !sel_ || std::find(d->shapes.begin(), d->shapes.end(), sel_) == d->shapes.end()) return nullptr;
    return sel_;
}
std::shared_ptr<Shape> Markup::shapeAt(D2D1_POINT_2F p, float k) {
    auto d = doc();
    if (!d) return nullptr;
    float r = 6 * k;
    for (auto it = d->shapes.rbegin(); it != d->shapes.rend(); ++it) {
        bool h = hit(**it, p, r);
        // outlined shapes are grabbable from inside too
        if (!h && ((*it)->type == Tool::Rect || (*it)->type == Tool::Ellipse)) {
            D2D1_RECT_F b = shapeBounds(**it);
            h = p.x >= b.left && p.x <= b.right && p.y >= b.top && p.y <= b.bottom;
        }
        if (h) return *it;
    }
    return nullptr;
}
bool Markup::pick(D2D1_POINT_2F p, float k) {
    auto s = shapeAt(p, k);
    if (s != sel_) selOwned_ = false;
    sel_ = s;
    return s != nullptr;
}
void Markup::ownSelection() {
    auto d = doc();
    if (!d || !sel_ || selOwned_) return;
    auto copy = std::make_shared<Shape>(*sel_);
    auto next = d->shapes;
    for (auto& s : next) if (s == sel_) s = copy;
    commit(next);
    sel_ = copy;
    selOwned_ = true;
}
void Markup::beginMove(D2D1_POINT_2F p) {
    if (!selection()) return;
    moving_ = true;
    moved_ = false;
    moveLast_ = p;
}
void Markup::moveTo(D2D1_POINT_2F p) {
    if (!moving_ || !selection()) return;
    float dx = p.x - moveLast_.x, dy = p.y - moveLast_.y;
    if (!moved_) {
        if (std::fabs(dx) + std::fabs(dy) < 0.01f) return;
        selOwned_ = false; // every drag is one undo step
        ownSelection();
        moved_ = true;
    }
    for (auto& q : sel_->pts) { q.x += dx; q.y += dy; }
    sel_->a.x += dx; sel_->a.y += dy;
    sel_->b.x += dx; sel_->b.y += dy;
    moveLast_ = p;
}
void Markup::endMove() { moving_ = false; selOwned_ = false; }
void Markup::deleteSelection() {
    auto d = doc();
    if (!d || !selection()) return;
    std::vector<std::shared_ptr<Shape>> next;
    for (auto& s : d->shapes) if (s != sel_) next.push_back(s);
    sel_.reset();
    selOwned_ = false;
    commit(next);
}
Tool Markup::styleTool() {
    if (tool == Tool::Hand) { auto s = selection(); return s ? s->type : Tool::Hand; }
    return tool;
}
D2D1_COLOR_F Markup::curColor() {
    if (tool == Tool::Hand) { if (auto s = selection()) return s->color; }
    return ts().color;
}
float Markup::curWidth(float k) {
    if (tool == Tool::Hand) { if (auto s = selection()) return std::round(s->width / k); }
    return ts().width;
}
void Markup::setColor(D2D1_COLOR_F c) {
    if (tool == Tool::Hand) {
        if (!selection()) return;
        selOwned_ = false;
        ownSelection();
        sel_->color = c;
        style[sel_->type].color = c;
        selOwned_ = false;
        return;
    }
    ts().color = c;
}
void Markup::setWidth(float w, float k) {
    if (tool == Tool::Hand) {
        if (!selection()) return;
        ownSelection(); // slider drags edit in place after the first change (one undo step)
        sel_->width = w * k;
        return;
    }
    ts().width = w;
}

} // namespace dv
