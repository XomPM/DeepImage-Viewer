// DeepImage Viewer — Explorer thumbnail provider (COM bridge; parsing happens in DeepImage.exe --thumbnail)
// Based on DMIND 3D Thumbnail shell_provider.cpp. MIT License.
#include "stream_thumbnail.h"
#include "shell_contract.h"
#include <propsys.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <atomic>
#include <new>

namespace {
using Microsoft::WRL::ComPtr;
HMODULE module{};
std::atomic<long> objects{};
std::atomic<long> server_locks{};

class Provider final : public IThumbnailProvider, public IInitializeWithStream, public IInitializeWithFile {
public:
    explicit Provider(const thumbnail::ShellFormat& format) : format_(format) { ++objects; }
    ~Provider() { --objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) { return E_POINTER; }
        *result = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IThumbnailProvider)) { *result = static_cast<IThumbnailProvider*>(this); }
        else if (iid == __uuidof(IInitializeWithStream) && !format_.requires_file) { *result = static_cast<IInitializeWithStream*>(this); }
        else if (iid == __uuidof(IInitializeWithFile) && format_.requires_file) { *result = static_cast<IInitializeWithFile*>(this); }
        else { return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (!remaining) { delete this; }
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IStream* stream, DWORD mode) override {
        if (stream_) { return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED); }
        if (!stream) { return E_POINTER; }
        if ((mode & 3) == STGM_WRITE) { return STG_E_ACCESSDENIED; }
        stream_ = stream;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Initialize(LPCWSTR path, DWORD mode) override {
        if (!file_.empty()) { return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED); }
        if (!path) { return E_POINTER; }
        if ((mode & 3) == STGM_WRITE) { return STG_E_ACCESSDENIED; }
        try {
            const std::filesystem::path input(path);
            if (!input.is_absolute() || _wcsicmp(input.extension().c_str(), L".gltf") != 0) { return E_INVALIDARG; }
            file_ = input;
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
          catch (...) { return E_INVALIDARG; }
    }
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT size, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) override {
        if (bitmap) { *bitmap = nullptr; }
        if (alpha) { *alpha = WTSAT_UNKNOWN; }
        if (!bitmap || !alpha) { return E_POINTER; }
        if (!stream_ && file_.empty()) { return E_UNEXPECTED; }
        try {
            std::wstring path(32768, L'\0');
            const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            if (!length || length >= path.size()) { return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME); }
            path.resize(length);
            PWSTR low_directory{};
            const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &low_directory);
            if (FAILED(status)) { return status; }
            std::filesystem::path temporary;
            try { temporary = low_directory; } catch (...) { CoTaskMemFree(low_directory); throw; }
            CoTaskMemFree(low_directory);
            if (format_.requires_file) {
                return thumbnail::thumbnail_from_gltf_file(file_, size,
                    std::filesystem::path(path).parent_path() / L"DeepImage.exe", temporary, bitmap, alpha);
            }
            return thumbnail::thumbnail_from_stream(stream_.Get(), format_.extension, size,
                std::filesystem::path(path).parent_path() / L"DeepImage.exe", temporary, bitmap, alpha);
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
          catch (...) { return E_FAIL; }
    }
private:
    std::atomic<ULONG> references_{1};
    const thumbnail::ShellFormat& format_;
    ComPtr<IStream> stream_;
    std::filesystem::path file_;
};

class Factory final : public IClassFactory {
public:
    explicit Factory(const thumbnail::ShellFormat& format) : format_(format) { ++objects; }
    ~Factory() { --objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) { return E_POINTER; }
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) { return E_NOINTERFACE; }
        *result = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (!remaining) { delete this; }
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** result) override {
        if (!result) { return E_POINTER; }
        *result = nullptr;
        if (outer) { return CLASS_E_NOAGGREGATION; }
        auto* provider = new (std::nothrow) Provider(format_);
        if (!provider) { return E_OUTOFMEMORY; }
        const auto status = provider->QueryInterface(iid, result);
        provider->Release();
        return status;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) { ++server_locks; }
        else {
            auto count = server_locks.load();
            do { if (count <= 0) { return E_UNEXPECTED; } }
            while (!server_locks.compare_exchange_weak(count, count - 1));
        }
        return S_OK;
    }
private:
    std::atomic<ULONG> references_{1};
    const thumbnail::ShellFormat& format_;
};
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { module = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return objects == 0 && server_locks == 0 ? S_OK : S_FALSE; }
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** result) {
    if (!result) { return E_POINTER; }
    *result = nullptr;
    for (const auto& format : thumbnail::kShellFormats) {
        if (format.clsid != clsid) { continue; }
        auto* factory = new (std::nothrow) Factory(format);
        if (!factory) { return E_OUTOFMEMORY; }
        const auto status = factory->QueryInterface(iid, result);
        factory->Release();
        return status;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
