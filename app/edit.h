// DeepImage Viewer — image editing (adjust, crop, resize, save)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#include "common.h"
#include "image.h"
#include "imageview.h"

namespace dv {

struct Adjust {
    float exposure = 0, contrast = 0, highlights = 0, shadows = 0, saturation = 0, temperature = 0, tint = 0;
    bool identity() const {
        return !exposure && !contrast && !highlights && !shadows && !saturation && !temperature && !tint;
    }
};

class Effects {
public:
    // (Re)build the effect chain for the parameters; returns false when no effect is needed.
    bool build(const Adjust& a);
    ID2D1Effect* head() const { return head_; }
    ID2D1Effect* tail() const { return tail_; }
private:
    ComPtr<ID2D1Effect> exposure_, contrast_, hs_, temp_, sat_;
    ID2D1Effect* head_ = nullptr;
    ID2D1Effect* tail_ = nullptr;
};

struct EditSnapshot {
    ComPtr<IWICBitmap> image;
    std::vector<std::shared_ptr<Shape>> shapes;
};

// Pixel operations (CPU, exact)
ComPtr<IWICBitmap> bakeOrientation(IWICBitmap* src, int rot, float fx, float fy);
ComPtr<IWICBitmap> cropBitmap(IWICBitmap* src, UINT x, UINT y, UINT w, UINT h);
ComPtr<IWICBitmap> resizeBitmap(IWICBitmap* src, UINT w, UINT h);

// Keep markup aligned with pixel edits
void transformShapes(std::vector<std::shared_ptr<Shape>>& shapes, const std::function<D2D1_POINT_2F(D2D1_POINT_2F)>& f, float widthScale);

struct EncoderInfo { GUID container; std::wstring name; std::wstring exts; };   // exts: "*.jpg;*.jpeg"
std::vector<EncoderInfo> availableEncoders();
bool saveBitmap(const std::wstring& path, const GUID& container, IWICBitmapSource* pixels, float jpegQuality,
                const std::wstring& metadataSource, bool keepMetadata);
bool copyToClipboard(HWND hwnd, IWICBitmapSource* pixels);

} // namespace dv
