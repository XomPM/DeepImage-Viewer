// DeepImage Viewer — graphics: D3D11 device, swap chain, Direct2D, DirectWrite, icons
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#include "common.h"

namespace dv {

enum Align { AL = 0, AC = 1, AR = 2 };

class Gfx {
public:
    bool init(HWND hwnd);
    bool resize();
    bool beginFrame();
    void endFrame();
    void handleDeviceLost();

    // drawing helpers (DIPs)
    ID2D1SolidColorBrush* brush(const D2D1_COLOR_F& c);
    IDWriteTextFormat* format(float size, int weight = 400, Align a = AL, bool center = true, bool wrap = false);
    ComPtr<IDWriteTextLayout> layout(const std::wstring& s, float size, int weight, float maxW = 10000, float maxH = 1000,
                                     Align a = AL, bool wrap = false);
    float textWidth(const std::wstring& s, float size, int weight = 400);
    void text(const std::wstring& s, const D2D1_RECT_F& r, float size, const D2D1_COLOR_F& c, int weight = 400,
              Align a = AL, bool wrap = false, bool vcenter = true);
    void textEllipsis(const std::wstring& s, const D2D1_RECT_F& r, float size, const D2D1_COLOR_F& c, int weight = 400, Align a = AL);
    void fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0);
    void stroke(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0, float width = 1);
    void line(float x0, float y0, float x1, float y1, const D2D1_COLOR_F& c, float w = 1);
    void circle(float cx, float cy, float r, const D2D1_COLOR_F& c, bool filled = true, float w = 1);
    void shadow(const D2D1_RECT_F& r, float radius, float strength = 1);
    void icon(const char* name, float cx, float cy, float size, const D2D1_COLOR_F& c, float strokeWidth = 1.75f);

    HWND hwnd = nullptr;
    float dpi = 96, scale = 1;
    UINT pxW = 0, pxH = 0;
    float W() const { return pxW / scale; }
    float H() const { return pxH / scale; }

    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID2D1Factory6> d2f;
    ComPtr<ID2D1Device5> d2dev;
    ComPtr<ID2D1DeviceContext5> dc;
    ComPtr<ID2D1Bitmap1> target;
    ComPtr<IDWriteFactory5> dw;
    ComPtr<IDWriteFontCollection1> fonts;
    ComPtr<IWICImagingFactory2> wic;
    std::wstring family = L"Segoe UI";
    bool warp = false;

private:
    bool createDevice();
    void createTarget();
    void loadFonts();
    std::map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brushes_;
    std::map<uint64_t, ComPtr<IDWriteTextFormat>> formats_;
    std::map<std::string, ComPtr<ID2D1SvgDocument>> icons_;
};

extern Gfx G;

} // namespace dv
