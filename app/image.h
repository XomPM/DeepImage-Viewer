// DeepImage Viewer — image document (decode, GPU tiles, render-to-pixels)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#include "common.h"

namespace dv {

struct DecodedImage {
    std::vector<ComPtr<IWICBitmap>> frames; // 32bpp PBGRA, upright (EXIF orientation applied)
    std::vector<UINT> delays;               // ms per frame (animation)
    UINT width = 0, height = 0;
    std::wstring format;                    // "JPEG", "PNG", ...
    UINT bitDepth = 0;
    double dpiX = 0, dpiY = 0;
    int orientation = 1;                    // original EXIF orientation
    bool fromSvg = false;
    GUID container{};
};

// Thread-safe (creates its own factories). Returns false with an error key ("load.failed"/"load.codec").
bool decodeImageFile(const std::wstring& path, DecodedImage& out, std::string& errorKey, UINT maxSvgEdge = 4096);

struct Tile { D2D1_RECT_F rect; ComPtr<ID2D1Bitmap1> bmp; };

class ImageDoc {
public:
    std::wstring path;
    DecodedImage img;
    std::vector<std::vector<Tile>> gpu; // per frame
    int frame = 0;
    DWORD nextFrameAt = 0;

    bool loaded() const { return img.width > 0; }
    UINT W() const { return img.width; }
    UINT H() const { return img.height; }
    void clear();
    void upload(ID2D1DeviceContext* dc);               // (re)create GPU tiles for all frames
    void replace(ComPtr<IWICBitmap> bmp);              // after an edit: single upright frame
    void draw(ID2D1DeviceContext* dc, float scale, ID2D1Effect* chainHead = nullptr, ID2D1Effect* chainTail = nullptr,
              float opacity = 1.f) const;
    bool tick();                                       // advance animation; true if frame changed
    UINT frameCount() const { return (UINT)img.frames.size(); }
};

// Render arbitrary content at pixel size w x h on the GPU (tiled if needed) and read it back.
// draw() is called with the device context transform positioned for the current tile.
ComPtr<IWICBitmap> renderToWic(UINT w, UINT h, const std::function<void(ID2D1DeviceContext*)>& draw, bool opaqueWhite = false);

} // namespace dv
