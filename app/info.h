// DeepImage Viewer — EXIF (Windows property system) + histogram
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#include "common.h"

namespace dv {

struct InfoRow { const char* key; std::wstring value; };

struct ExifInfo {
    std::vector<InfoRow> rows;
    bool loaded = false;
};
ExifInfo readExif(const std::wstring& path);

struct Histogram {
    float r[256]{}, g[256]{}, b[256]{}, l[256]{};
    bool valid = false;
};
Histogram computeHistogram(IWICBitmapSource* src);
void drawHistogram(const Histogram& h, const D2D1_RECT_F& r);

std::wstring formatFileTime(const FILETIME& ft);

} // namespace dv
