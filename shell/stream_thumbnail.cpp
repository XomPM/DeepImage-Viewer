#include "stream_thumbnail.h"
#include "shell_contract.h"

#include <sddl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace thumbnail {
namespace {
using Microsoft::WRL::ComPtr;
struct HResultFailure { HRESULT code; };
void checked(HRESULT result) { if (FAILED(result)) { throw HResultFailure{result}; } }
void checked_win32(BOOL result) { if (!result) { checked(HRESULT_FROM_WIN32(GetLastError())); } }
struct CloseHandleDeleter { void operator()(HANDLE h) const { if (h && h != INVALID_HANDLE_VALUE) { CloseHandle(h); } } };
using Handle = std::unique_ptr<void, CloseHandleDeleter>;
struct FreeLocal { void operator()(void* p) const { LocalFree(p); } };
using LocalMemory = std::unique_ptr<void, FreeLocal>;
struct DeleteBitmap { void operator()(HBITMAP bitmap) const { DeleteObject(bitmap); } };
using Bitmap = std::unique_ptr<std::remove_pointer_t<HBITMAP>, DeleteBitmap>;

LocalMemory private_directory_security() {
    HANDLE token_handle{};
    checked_win32(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_handle));
    const Handle token(token_handle);
    DWORD required = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &required);
    std::vector<std::byte> buffer(required);
    checked_win32(GetTokenInformation(token.get(), TokenUser, buffer.data(), required, &required));
    LPWSTR sid_text{};
    checked_win32(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid_text));
    const LocalMemory sid(sid_text);
    // A protected DACL grants access only to this user and SYSTEM. Inherit the parent's integrity label.
    const std::wstring sddl = L"D:P(A;OICI;FA;;;" + std::wstring(sid_text) + L")(A;OICI;FA;;;SY)";
    PSECURITY_DESCRIPTOR descriptor{};
    checked_win32(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    return LocalMemory(descriptor);
}

class Workspace {
public:
    Workspace(const std::filesystem::path& root, const wchar_t* extension) {
        GUID id{};
        checked(CoCreateGuid(&id));
        wchar_t text[40]{};
        if (!StringFromGUID2(id, text, 40)) { checked(E_FAIL); }
        directory_ = root / (std::wstring(L"DeepImage-") + text);
        input_ = directory_ / (std::wstring(L"model") + extension);
        output_ = directory_ / L"thumbnail.png";
        const auto descriptor = private_directory_security();
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor.get(), FALSE};
        checked_win32(CreateDirectoryW(directory_.c_str(), &attributes));
        const auto handle = CreateFileW(directory_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            RemoveDirectoryW(directory_.c_str());
            checked(HRESULT_FROM_WIN32(error));
        }
        directory_lock_.reset(handle); // Prevent renaming/replacing this freshly created directory.
    }
    ~Workspace() {
        // Delete only the two known outputs we own; never recursively follow filesystem entries.
        DeleteFileW(input_.c_str());
        DeleteFileW(output_.c_str());
        directory_lock_.reset();
        RemoveDirectoryW(directory_.c_str());
    }
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    const std::filesystem::path& input() const { return input_; }
    const std::filesystem::path& output() const { return output_; }
private:
    std::filesystem::path directory_, input_, output_;
    Handle directory_lock_;
};

class RestoreStreamPosition {
public:
    explicit RestoreStreamPosition(IStream* stream) : stream_(stream) {
        checked(stream_->Seek({}, STREAM_SEEK_CUR, &position_));
        checked(stream_->Seek({}, STREAM_SEEK_SET, nullptr));
    }
    ~RestoreStreamPosition() {
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(position_.QuadPart);
        stream_->Seek(offset, STREAM_SEEK_SET, nullptr);
    }
private:
    IStream* stream_;
    ULARGE_INTEGER position_{};
};

Handle copy_stream(IStream* stream, const std::filesystem::path& input) {
    STATSTG stats{};
    if (SUCCEEDED(stream->Stat(&stats, STATFLAG_NONAME)) && stats.cbSize.QuadPart > kMaximumFileBytes) {
        checked(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    }
    const RestoreStreamPosition restore(stream);
    const auto raw = CreateFileW(input.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (raw == INVALID_HANDLE_VALUE) { checked(HRESULT_FROM_WIN32(GetLastError())); }
    Handle file(raw);
    std::array<std::byte, 64 * 1024> buffer;
    std::size_t total = 0;
    for (;;) {
        ULONG received = 0;
        const auto status = stream->Read(buffer.data(), static_cast<ULONG>(buffer.size()), &received);
        checked(status);
        if (received > buffer.size() || received > kMaximumFileBytes - total) {
            checked(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        }
        if (!received) { break; }
        DWORD written = 0;
        checked_win32(WriteFile(file.get(), buffer.data(), received, &written, nullptr));
        if (written != received) { checked(STG_E_WRITEFAULT); }
        total += received;
        if (status == S_FALSE) { break; }
    }
    if (!total) { checked(HRESULT_FROM_WIN32(ERROR_INVALID_DATA)); }
    return file; // Keep a handle that denies writes/deletion until the worker exits.
}

Bitmap decode_bitmap(const std::filesystem::path& path, UINT render_size, UINT requested_size) {
    if (std::filesystem::file_size(path) > 32 * 1024 * 1024) { checked(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)); }
    ComPtr<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    checked(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder));
    GUID container{};
    checked(decoder->GetContainerFormat(&container));
    if (container != GUID_ContainerFormatPng) { checked(HRESULT_FROM_WIN32(ERROR_INVALID_DATA)); }
    ComPtr<IWICBitmapFrameDecode> frame;
    checked(decoder->GetFrame(0, &frame));
    UINT width = 0, height = 0;
    checked(frame->GetSize(&width, &height));
    if (!width || !height || width > render_size || height > render_size) { checked(HRESULT_FROM_WIN32(ERROR_INVALID_DATA)); }
    ComPtr<IWICBitmapSource> source;
    ComPtr<IWICFormatConverter> converter;
    checked(factory->CreateFormatConverter(&converter));
    checked(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
        nullptr, 0, WICBitmapPaletteTypeCustom));
    checked(converter.As(&source));
    const UINT longest = std::max(width, height);
    if (longest > requested_size) {
        const UINT w = std::max(1U, width * requested_size / longest), h = std::max(1U, height * requested_size / longest);
        ComPtr<IWICBitmapScaler> scaler;
        checked(factory->CreateBitmapScaler(&scaler));
        checked(scaler->Initialize(source.Get(), w, h, WICBitmapInterpolationModeFant));
        checked(scaler.As(&source));
        width = w;
        height = h;
    }
    const UINT size_w = width, size_h = height;
    BITMAPINFO information{};
    information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    information.bmiHeader.biWidth = static_cast<LONG>(size_w);
    information.bmiHeader.biHeight = -static_cast<LONG>(size_h);
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;
    void* pixels{};
    Bitmap bitmap(CreateDIBSection(nullptr, &information, DIB_RGB_COLORS, &pixels, nullptr, 0));
    if (!bitmap) { checked(E_OUTOFMEMORY); }
    checked(source->CopyPixels(nullptr, size_w * 4, size_w * size_h * 4, static_cast<BYTE*>(pixels)));
    return bitmap;
}
}

static HRESULT thumbnail_impl(IStream* stream, const std::filesystem::path* source, const wchar_t* extension, UINT size,
    const std::filesystem::path& worker, const std::filesystem::path& temp_root,
    HBITMAP* bitmap, WTS_ALPHATYPE* alpha, const ProcessLimits& limits) noexcept {
    if (bitmap) { *bitmap = nullptr; }
    if (alpha) { *alpha = WTSAT_UNKNOWN; }
    if (!bitmap || !alpha || (!stream && !source) || !extension) { return E_POINTER; }
    if (!size) { return E_INVALIDARG; }
    try {
        const std::wstring suffix(extension);
        if (std::none_of(kShellFormats.begin(), kShellFormats.end(), [&](const auto& format) { return suffix == format.extension; })) { return E_INVALIDARG; }
        const auto render_size = std::clamp(size, 32U, 1024U);
        const Workspace workspace(temp_root, extension);
        Handle input_lock;
        if (source) {
            if (!source->is_absolute() || _wcsicmp(source->extension().c_str(), L".gltf") != 0) { return E_INVALIDARG; }
            input_lock.reset(CreateFileW(source->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
            if (input_lock.get() == INVALID_HANDLE_VALUE) { return HRESULT_FROM_WIN32(GetLastError()); }
            LARGE_INTEGER length{};
            checked_win32(GetFileSizeEx(input_lock.get(), &length));
            if (length.QuadPart <= 0 || length.QuadPart > kMaximumFileBytes) { return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE); }
        } else { input_lock = copy_stream(stream, workspace.input()); }
        const std::array<std::wstring, 6> arguments{L"--thumbnail", source ? source->wstring() : workspace.input().wstring(),
            workspace.output().wstring(), std::to_wstring(render_size), L"--ext", suffix};
        const auto result = run_limited_process(worker, arguments, limits);
        if (result.timed_out) { return HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
        if (result.exit_code != 0) { return HRESULT_FROM_WIN32(ERROR_INVALID_DATA); }
        auto image = decode_bitmap(workspace.output(), render_size, size);
        *bitmap = image.release();
        *alpha = WTSAT_ARGB;
        return S_OK;
    } catch (const HResultFailure& failure) { return failure.code; }
      catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_FAIL; }
}

HRESULT thumbnail_from_stream(IStream* stream, const wchar_t* extension, UINT size,
    const std::filesystem::path& worker, const std::filesystem::path& temp_root,
    HBITMAP* bitmap, WTS_ALPHATYPE* alpha, const ProcessLimits& limits) noexcept {
    return thumbnail_impl(stream, nullptr, extension, size, worker, temp_root, bitmap, alpha, limits);
}

HRESULT thumbnail_from_gltf_file(const std::filesystem::path& input, UINT size,
    const std::filesystem::path& worker, const std::filesystem::path& temp_root,
    HBITMAP* bitmap, WTS_ALPHATYPE* alpha, const ProcessLimits& limits) noexcept {
    return thumbnail_impl(nullptr, &input, L".gltf", size, worker, temp_root, bitmap, alpha, limits);
}
}
