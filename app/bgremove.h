// DeepImage Viewer — AI background removal (BiRefNet via ONNX Runtime, DirectML with CPU fallback)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
//
// The model (BiRefNet "general lite", MIT, ~224 MB) is downloaded once on first use into
// %LOCALAPPDATA%\DMIND\DeepImage Viewer\Models and checked by SHA-256. onnxruntime.dll and
// DirectML.dll ship next to the app and are loaded only when the feature is used.
#pragma once
#include "common.h"

#include <wincodec.h>

#include <functional>

namespace dv::bg {

// runtime DLLs present next to the exe
bool runtimeAvailable();
// model already downloaded and verified
bool modelReady();
std::wstring modelPath();
constexpr uint64_t kModelBytes = 224005088;

// downloads the model; progress 0..1; false + err key on failure. Runs on the calling (worker) thread.
bool downloadModel(const std::function<void(float)>& progress, const std::function<bool()>& cancelled, std::string& err);

// foreground mask (0..255, one byte per pixel) of `src` (32bpp PBGRA), or of the part inside `roi`
// (mask is then roi->Width x roi->Height). Worker thread. `device` is set to "GPU" or "CPU".
bool predictMask(IWICBitmap* src, const WICRect* roi, std::vector<uint8_t>& mask, std::string& err, std::wstring& device);

} // namespace dv::bg
