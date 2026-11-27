// DeepImage Viewer — update check against the GitHub releases (installed builds; the Store updates itself)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
//
// Once a day the app reads https://api.github.com/repos/XomPM/DeepImage-Viewer/releases/latest. When the tag is
// newer than kVersion it offers the update; one click downloads the Setup for this PC's architecture, checks it
// against the SHA-256 digest GitHub publishes for the asset, and runs it with /update: Setup closes the app,
// installs with the options of the first install and reopens the picture that was open.
#pragma once
#include "common.h"

#include <atomic>
#include <functional>

namespace dv::upd {

struct Info {
    std::wstring version; // "1.0.11"
    std::wstring page;    // release page
    std::wstring url;     // Setup download (empty: portable / not found)
    std::wstring name;    // Setup file name
    std::string sha256;   // lowercase hex ("" when GitHub gave none)
    uint64_t size = 0;
};

// Store packages update through the Store
bool supported();
// running from an installation made by Setup (portable copies only get the release page)
bool installed();
// -1 a < b, 0 equal, 1 a > b ("1.0.10" vs "1.0.9" compares numerically)
int compareVersions(const std::wstring& a, const std::wstring& b);
// worker thread: false = network / parse error
bool fetchLatest(Info& out);
// worker thread: downloads and verifies the Setup into %TEMP%\DeepImage-update; returns its path or "" (err = i18n key)
std::wstring downloadSetup(const Info& in, const std::function<void(float)>& progress, const std::function<bool()>& cancelled, std::string& err);
// starts the downloaded Setup; the app should close right after
bool runSetup(const std::wstring& setup, const std::wstring& reopen);
// leftovers of an earlier update
void cleanup();

} // namespace dv::upd
