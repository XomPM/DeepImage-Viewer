// DeepImage Viewer — background-removal editor (see bgedit.h)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#include "bgedit.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dv {
namespace {

inline uint8_t u8(float v) { return (uint8_t)(v <= 0 ? 0 : v >= 255 ? 255 : (int)(v + .5f)); }

// van Herk / Gil-Werman running min or max over a window of 2r+1 (edges replicated), O(n)
void minmaxLine(const uint8_t* in, uint8_t* out, int n, int r, bool isMax, std::vector<uint8_t>& a, std::vector<uint8_t>& g, std::vector<uint8_t>& h) {
    const int L = n + 2 * r, k = 2 * r + 1;
    a.resize(L); g.resize(L); h.resize(L);
    for (int i = 0; i < L; ++i) a[i] = in[std::clamp(i - r, 0, n - 1)];
    auto op = [isMax](uint8_t x, uint8_t y) { return isMax ? std::max(x, y) : std::min(x, y); };
    for (int i = 0; i < L; ++i) g[i] = (i % k == 0) ? a[i] : op(g[i - 1], a[i]);
    for (int i = L - 1; i >= 0; --i) h[i] = (i % k == k - 1 || i == L - 1) ? a[i] : op(h[i + 1], a[i]);
    for (int j = 0; j < n; ++j) out[j] = op(h[j], g[j + k - 1]);
}

// applies a line operation to every column, gathering 32 columns at a time (cache friendly)
template <class F> void forColumns(std::vector<uint8_t>& m, int W, int H, F lineOp) {
    constexpr int B = 32;
    std::vector<uint8_t> lines((size_t)B * H), res(H);
    for (int x0 = 0; x0 < W; x0 += B) {
        const int n = std::min(B, W - x0);
        for (int y = 0; y < H; ++y) {
            const uint8_t* row = &m[(size_t)y * W + x0];
            for (int b = 0; b < n; ++b) lines[(size_t)b * H + y] = row[b];
        }
        for (int b = 0; b < n; ++b) {
            lineOp(&lines[(size_t)b * H], res.data(), H);
            std::memcpy(&lines[(size_t)b * H], res.data(), H);
        }
        for (int y = 0; y < H; ++y) {
            uint8_t* row = &m[(size_t)y * W + x0];
            for (int b = 0; b < n; ++b) row[b] = lines[(size_t)b * H + y];
        }
    }
}

void minmax2d(std::vector<uint8_t>& m, int W, int H, int r, bool isMax) {
    if (r <= 0) return;
    std::vector<uint8_t> a, g, h, line(std::max(W, H)), res(std::max(W, H));
    for (int y = 0; y < H; ++y) {
        uint8_t* row = &m[(size_t)y * W];
        minmaxLine(row, res.data(), W, r, isMax, a, g, h);
        std::memcpy(row, res.data(), W);
    }
    forColumns(m, W, H, [&](const uint8_t* in, uint8_t* out, int n) { minmaxLine(in, out, n, r, isMax, a, g, h); });
}

// running box average over 2r+1 (edges replicated)
void boxLine(const uint8_t* in, uint8_t* out, int n, int r) {
    const int k = 2 * r + 1;
    int sum = 0;
    for (int i = -r; i <= r; ++i) sum += in[std::clamp(i, 0, n - 1)];
    for (int j = 0; j < n; ++j) {
        out[j] = (uint8_t)((sum + k / 2) / k);
        sum += in[std::min(j + r + 1, n - 1)] - in[std::max(j - r, 0)];
    }
}

void box2d(std::vector<uint8_t>& m, int W, int H, int r) {
    if (r <= 0) return;
    std::vector<uint8_t> line(std::max(W, H)), res(std::max(W, H));
    for (int y = 0; y < H; ++y) {
        uint8_t* row = &m[(size_t)y * W];
        boxLine(row, res.data(), W, r);
        std::memcpy(row, res.data(), W);
    }
    forColumns(m, W, H, [&](const uint8_t* in, uint8_t* out, int n) { boxLine(in, out, n, r); });
}

// float box blur of interleaved channels on a small grid
void boxGrid(std::vector<float>& v, int w, int h, int ch, int r) {
    if (r <= 0) return;
    std::vector<float> line((size_t)std::max(w, h) * ch), res(line.size());
    auto pass = [&](int n) {
        const float inv = 1.f / (2 * r + 1);
        for (int c = 0; c < ch; ++c) {
            float sum = 0;
            for (int i = -r; i <= r; ++i) sum += line[(size_t)std::clamp(i, 0, n - 1) * ch + c];
            for (int j = 0; j < n; ++j) {
                res[(size_t)j * ch + c] = sum * inv;
                sum += line[(size_t)std::min(j + r + 1, n - 1) * ch + c] - line[(size_t)std::max(j - r, 0) * ch + c];
            }
        }
    };
    for (int y = 0; y < h; ++y) {
        std::memcpy(line.data(), &v[(size_t)y * w * ch], sizeof(float) * w * ch);
        pass(w);
        std::memcpy(&v[(size_t)y * w * ch], res.data(), sizeof(float) * w * ch);
    }
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) for (int c = 0; c < ch; ++c) line[(size_t)y * ch + c] = v[((size_t)y * w + x) * ch + c];
        pass(h);
        for (int y = 0; y < h; ++y) for (int c = 0; c < ch; ++c) v[((size_t)y * w + x) * ch + c] = res[(size_t)y * ch + c];
    }
}

// background colour estimate: blur of the picture weighted by "how much background" (255 - mask),
// so the subject's colours do not leak into it
BgGrid backgroundGrid(const std::vector<uint8_t>& straight, const std::vector<uint8_t>& m, int W, int H, int k, int radius) {
    BgGrid g;
    g.k = k;
    g.w = (W + k - 1) / k;
    g.h = (H + k - 1) / k;
    std::vector<float> acc((size_t)g.w * g.h * 4, 0.f);
    for (int y = 0; y < H; ++y) {
        float* rowAcc = &acc[(size_t)(y / k) * g.w * 4];
        const uint8_t* s = &straight[(size_t)y * W * 3];
        const uint8_t* mm = &m[(size_t)y * W];
        for (int x = 0; x < W; ++x) {
            float w = (256 - mm[x]) * (1.f / 256);
            float* a = rowAcc + (x / k) * 4;
            a[0] += s[x * 3] * w; a[1] += s[x * 3 + 1] * w; a[2] += s[x * 3 + 2] * w; a[3] += w;
        }
    }
    for (int pass = 0; pass < 3; ++pass) boxGrid(acc, g.w, g.h, 4, radius);
    g.c.resize((size_t)g.w * g.h * 3);
    for (size_t i = 0; i < (size_t)g.w * g.h; ++i) {
        float w = std::max(acc[i * 4 + 3], 1e-6f);
        for (int c = 0; c < 3; ++c) g.c[i * 3 + c] = acc[i * 4 + c] / w;
    }
    return g;
}

int gridStep(int W, int H, int cells) { return std::max(1, (std::max(W, H) + cells - 1) / cells); }

} // namespace

void BgGrid::sample(int x, int y, float out[3]) const {
    float gx = std::clamp((x + .5f) / k - .5f, 0.f, (float)(w - 1)), gy = std::clamp((y + .5f) / k - .5f, 0.f, (float)(h - 1));
    int x0 = (int)gx, y0 = (int)gy, x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    float fx = gx - x0, fy = gy - y0;
    const float* a = &c[((size_t)y0 * w + x0) * 3];
    const float* b = &c[((size_t)y0 * w + x1) * 3];
    const float* d = &c[((size_t)y1 * w + x0) * 3];
    const float* e = &c[((size_t)y1 * w + x1) * 3];
    for (int i = 0; i < 3; ++i) out[i] = (a[i] * (1 - fx) + b[i] * fx) * (1 - fy) + (d[i] * (1 - fx) + e[i] * fx) * fy;
}

std::unique_ptr<BgStage> bgComputeStage(const BgSource& src, const std::vector<uint8_t>& ai, const BgParams& p) {
    const int W = src.W, H = src.H;
    const size_t N = (size_t)W * H;
    auto st = std::make_unique<BgStage>();
    // 1. strength: a gamma curve (identity at 0)
    {
        uint8_t lut[256];
        const float gamma = std::pow(2.f, p.strength / 50.f);
        for (int i = 0; i < 256; ++i) lut[i] = u8(255.f * std::pow(i / 255.f, gamma));
        st->mask.resize(N);
        for (size_t i = 0; i < N; ++i) st->mask[i] = lut[ai[i]];
    }
    auto& m = st->mask;
    // 2. objects: connected parts of the confident mask (4-connected)
    {
        st->labels.assign(N, 0);
        std::vector<uint32_t> stack;
        uint32_t next = 0;
        for (size_t i0 = 0; i0 < N; ++i0) {
            if (m[i0] < 128 || st->labels[i0]) continue;
            const uint32_t id = ++next;
            st->labels[i0] = id;
            stack.assign(1, (uint32_t)i0);
            while (!stack.empty()) {
                uint32_t i = stack.back();
                stack.pop_back();
                int x = (int)(i % W), y = (int)(i / W);
                auto visit = [&](size_t j) { if (m[j] >= 128 && !st->labels[j]) { st->labels[j] = id; stack.push_back((uint32_t)j); } };
                if (x > 0) visit(i - 1);
                if (x + 1 < W) visit(i + 1);
                if (y > 0) visit(i - W);
                if (y + 1 < H) visit(i + W);
            }
        }
        if (!p.objects.empty()) {
            std::vector<uint32_t> chosen;
            for (auto [x, y] : p.objects) {
                const int r = 12;
                uint32_t best = 0;
                int bd = 1 << 30;
                for (int yy = std::max(0, y - r); yy <= std::min(H - 1, y + r); ++yy)
                    for (int xx = std::max(0, x - r); xx <= std::min(W - 1, x + r); ++xx) {
                        uint32_t l = st->labels[(size_t)yy * W + xx];
                        int d = (xx - x) * (xx - x) + (yy - y) * (yy - y);
                        if (l && d < bd) { bd = d; best = l; }
                    }
                if (best) chosen.push_back(best);
            }
            if (!chosen.empty()) {
                std::vector<uint8_t> sel(N, 0);
                std::sort(chosen.begin(), chosen.end());
                for (size_t i = 0; i < N; ++i)
                    if (st->labels[i] && std::binary_search(chosen.begin(), chosen.end(), st->labels[i])) sel[i] = 255;
                // keep the soft rim around the chosen objects
                minmax2d(sel, W, H, std::max(4, std::max(W, H) / 250), true);
                for (size_t i = 0; i < N; ++i) m[i] = std::min(m[i], sel[i]);
            }
        }
    }
    // 3. edge shift
    if (p.edge) minmax2d(m, W, H, std::abs(p.edge), p.edge > 0);
    // 4. feather: two box passes ≈ a soft Gaussian of about that radius
    if (p.feather > 0) {
        int r = std::max(1, (p.feather + 1) / 2);
        box2d(m, W, H, r);
        box2d(m, W, H, r);
    }
    // 5. straight colours, then decontaminated soft edges: F = (I - (1-a)·B) / a
    std::vector<uint8_t> straight(N * 3);
    for (size_t i = 0; i < N; ++i) {
        const uint8_t* s = &src.px[i * 4];
        const int A = s[3];
        for (int c = 0; c < 3; ++c) straight[i * 3 + c] = A == 255 ? s[c] : A ? (uint8_t)std::min(255, (s[c] * 255 + A / 2) / A) : 0;
    }
    st->fg = straight;
    if (p.decontam) {
        BgGrid bg = backgroundGrid(straight, m, W, H, gridStep(W, H, 384), 3);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = (size_t)y * W + x;
                const int a8 = m[i];
                if (a8 < 6 || a8 > 249) continue;
                const float a = a8 / 255.f;
                float B[3];
                bg.sample(x, y, B);
                for (int c = 0; c < 3; ++c) st->fg[i * 3 + c] = u8((straight[i * 3 + c] - (1 - a) * B[c]) / a);
            }
    }
    // 6. blurred background (bgMode 2): the picture with the subject filled in from around it
    if (p.bgMode == 2) {
        BgGrid g = backgroundGrid(straight, m, W, H, gridStep(W, H, 480), 1 + p.blur * 24 / 100);
        st->blurBg.resize(N * 3);
        float B[3];
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                g.sample(x, y, B);
                uint8_t* o = &st->blurBg[((size_t)y * W + x) * 3];
                o[0] = u8(B[0]); o[1] = u8(B[1]); o[2] = u8(B[2]);
            }
    }
    return st;
}

void BgEdit::compose(int x0, int y0, int x1, int y1) {
    if (!src || !stage) return;
    const int W = src->W, H = src->H;
    x0 = std::clamp(x0, 0, W); x1 = std::clamp(x1, 0, W);
    y0 = std::clamp(y0, 0, H); y1 = std::clamp(y1, 0, H);
    out.resize((size_t)W * H * 4);
    const bool hasKeep = !keep.empty(), hasErase = !erase.empty();
    const int cr = (p.bgColor >> 16) & 255, cg = (p.bgColor >> 8) & 255, cb = p.bgColor & 255;
    const bool blurOk = p.bgMode == 2 && stage->blurBg.size() == (size_t)W * H * 3;
    auto div255 = [](int v) { return (v + 128 + ((v + 128) >> 8)) >> 8; };
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const size_t i = (size_t)y * W + x;
            int a = stage->mask[i];
            if (hasErase) a = div255(a * (255 - erase[i]));
            if (hasKeep) a = a + div255((255 - a) * keep[i]);
            const uint8_t* s = &src->px[i * 4];
            if (s[3] != 255) a = div255(a * s[3]);
            const uint8_t* f = &stage->fg[i * 3];
            uint8_t* o = &out[i * 4];
            if (showMask) {
                // original picture; the parts that will go are tinted red
                const int A = s[3];
                const int tint[3] = {70, 60, 255}; // BGR
                for (int c = 0; c < 3; ++c) {
                    int I = A ? std::min(255, s[c] * 255 / A) : 0;
                    o[c] = (uint8_t)((I * a + ((I * 4 + tint[c] * 6) / 10) * (255 - a)) / 255);
                }
                o[3] = 255;
                continue;
            }
            if (p.bgMode == 0) {
                for (int c = 0; c < 3; ++c) o[c] = (uint8_t)div255(f[c] * a);
                o[3] = (uint8_t)a;
            } else {
                const uint8_t col[3] = {(uint8_t)cb, (uint8_t)cg, (uint8_t)cr};
                const uint8_t* B = blurOk ? &stage->blurBg[i * 3] : col;
                for (int c = 0; c < 3; ++c) o[c] = (uint8_t)div255(f[c] * a + B[c] * (255 - a));
                o[3] = 255;
            }
        }
}

BgEdit::Rect BgEdit::stroke(float ax, float ay, float bx, float by, float radius, bool keepBrush) {
    const int W = this->W(), H = this->H();
    Rect r;
    if (!W || radius <= 0) return r;
    if (keep.empty()) keep.assign((size_t)W * H, 0);
    if (erase.empty()) erase.assign((size_t)W * H, 0);
    auto& on = keepBrush ? keep : erase;
    auto& off = keepBrush ? erase : keep;
    const float len = std::hypot(bx - ax, by - ay), step = std::max(.5f, radius * .25f);
    const int n = std::max(1, (int)std::ceil(len / step));
    const float soft = std::max(1.f, radius * .35f);
    r.x0 = W; r.y0 = H; r.x1 = 0; r.y1 = 0;
    for (int k = 0; k <= n; ++k) {
        const float t = (float)k / n, cx = ax + (bx - ax) * t, cy = ay + (by - ay) * t;
        const int X0 = std::max(0, (int)std::floor(cx - radius)), X1 = std::min(W, (int)std::ceil(cx + radius) + 1);
        const int Y0 = std::max(0, (int)std::floor(cy - radius)), Y1 = std::min(H, (int)std::ceil(cy + radius) + 1);
        if (X0 >= X1 || Y0 >= Y1) continue;
        r.x0 = std::min(r.x0, X0); r.y0 = std::min(r.y0, Y0); r.x1 = std::max(r.x1, X1); r.y1 = std::max(r.y1, Y1);
        for (int y = Y0; y < Y1; ++y)
            for (int x = X0; x < X1; ++x) {
                const float d = std::hypot(x + .5f - cx, y + .5f - cy);
                const float v = std::clamp((radius - d) / soft, 0.f, 1.f);
                if (v <= 0) continue;
                const int v8 = (int)(v * 255 + .5f);
                const size_t i = (size_t)y * W + x;
                if (on[i] < v8) on[i] = (uint8_t)v8;
                off[i] = (uint8_t)(off[i] * (255 - v8) / 255);
            }
    }
    return r;
}

uint32_t BgEdit::labelAt(int x, int y, int radius) const {
    if (!stage || stage->labels.empty()) return 0;
    const int W = this->W(), H = this->H();
    uint32_t best = 0;
    int bd = 1 << 30;
    for (int yy = std::max(0, y - radius); yy <= std::min(H - 1, y + radius); ++yy)
        for (int xx = std::max(0, x - radius); xx <= std::min(W - 1, x + radius); ++xx) {
            uint32_t l = stage->labels[(size_t)yy * W + xx];
            int d = (xx - x) * (xx - x) + (yy - y) * (yy - y);
            if (l && d < bd) { bd = d; best = l; }
        }
    return best;
}

void BgEdit::beginStroke() {
    const size_t N = (size_t)W() * H();
    if (keep.empty()) keep.assign(N, 0);
    if (erase.empty()) erase.assign(N, 0);
    keep0_ = keep;
    erase0_ = erase;
}

void BgEdit::endStroke(const Rect& r) {
    if (r.empty() || keep0_.empty()) return;
    Undo u;
    u.kind = 0;
    u.r = r;
    const int W = this->W(), w = r.x1 - r.x0;
    for (int y = r.y0; y < r.y1; ++y) {
        u.keep.insert(u.keep.end(), &keep0_[(size_t)y * W + r.x0], &keep0_[(size_t)y * W + r.x0] + w);
        u.erase.insert(u.erase.end(), &erase0_[(size_t)y * W + r.x0], &erase0_[(size_t)y * W + r.x0] + w);
    }
    keep0_.clear(); keep0_.shrink_to_fit();
    erase0_.clear(); erase0_.shrink_to_fit();
    pushUndo(std::move(u));
}

} // namespace dv
