// DeepImage Viewer — browsing images inside archives (ZIP/CBZ, TAR/CBT)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
//
// An archive is shown like a folder. Each viewable entry gets a stable path under
// %TEMP%\DeepImage Viewer\arc\<archive-hash>\<n>\<name>; the file is written there on first
// use (materialize), so every decoder, thumbnailer and the 3D loader keep working with plain paths.
#pragma once
#include "common.h"

namespace dv::arc {

struct Item {
    std::wstring path;  // virtual (temp) path
    std::wstring name;  // entry file name (no folders)
    std::wstring inner; // full name inside the archive, '/' separated (sorting / display)
    uint64_t size = 0;
    FILETIME mtime{};
};

bool isArchiveExt(const std::wstring& ext);
const std::vector<std::wstring>& archiveExts();
// lists the viewable entries (images and 3D models), natural order; false when unreadable
bool list(const std::wstring& archive, std::vector<Item>& out);
// cheap check used when stepping through folders
bool hasViewable(const std::wstring& archive);
// writes the entry behind a virtual path to disk if needed; no-op for ordinary files
bool materialize(const std::wstring& path);
// archive that owns a virtual path ("" for ordinary files)
std::wstring archiveOf(const std::wstring& path);
// removes this session's extracted files (exit) and stale ones from older sessions (start)
void cleanup(bool atExit);

} // namespace dv::arc
