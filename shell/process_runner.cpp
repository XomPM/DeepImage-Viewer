#include "process_runner.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

namespace thumbnail {
namespace {

struct CloseHandleDeleter {
    void operator()(HANDLE handle) const { if (handle && handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); } }
};
using Handle = std::unique_ptr<void, CloseHandleDeleter>;

void check_win32(bool succeeded, const char* operation) {
    if (!succeeded) {
        throw std::runtime_error(std::string(operation) + " failed (Windows error " + std::to_string(GetLastError()) + ").");
    }
}

std::wstring quote_argument(const std::wstring& argument) {
    if (argument.find(L'\0') != std::wstring::npos) { throw std::runtime_error("NUL in process argument."); }
    std::wstring result = L"\"";
    unsigned backslashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') { ++backslashes; continue; }
        result.append(character == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        result += character;
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    return result + L'"';
}

class HandleInheritance {
public:
    explicit HandleInheritance(std::array<HANDLE, 2>& handles) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        storage_.resize(bytes);
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        check_win32(InitializeProcThreadAttributeList(list_, 1, 0, &bytes), "Initialize handle list");
        if (!UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       handles.data(), sizeof(handles), nullptr, nullptr)) {
            const auto error = GetLastError();
            DeleteProcThreadAttributeList(list_);
            SetLastError(error);
            check_win32(false, "Restrict inherited handles");
        }
    }
    ~HandleInheritance() { DeleteProcThreadAttributeList(list_); }
    HandleInheritance(const HandleInheritance&) = delete;
    HandleInheritance& operator=(const HandleInheritance&) = delete;
    LPPROC_THREAD_ATTRIBUTE_LIST get() const { return list_; }
private:
    std::vector<std::byte> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
};

void drain_diagnostics(HANDLE pipe, std::string& diagnostic) {
    constexpr std::size_t maximum_diagnostic = 16 * 1024;
    std::array<char, 4096> buffer;
    // Bound each drain so a continuously noisy worker cannot defeat the deadline.
    for (unsigned chunk = 0; chunk < 16; ++chunk) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) { return; }
        DWORD received = 0;
        if (!ReadFile(pipe, buffer.data(), std::min(available, static_cast<DWORD>(buffer.size())), &received, nullptr)) { return; }
        diagnostic.append(buffer.data(), std::min(static_cast<std::size_t>(received), maximum_diagnostic - diagnostic.size()));
    }
}

} // namespace

std::filesystem::path executable_path() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    check_win32(length > 0 && length < buffer.size(), "Locate executable");
    buffer.resize(length);
    return buffer;
}

ProcessResult run_limited_process(const std::filesystem::path& executable,
                                 std::span<const std::wstring> arguments, const ProcessLimits& limits) {
    if (!executable.is_absolute() || limits.timeout.count() <= 0 || limits.memory_bytes < 16 * 1024 * 1024) {
        throw std::runtime_error("Invalid worker executable or resource limits.");
    }
    std::wstring command = quote_argument(executable.wstring());
    for (const auto& argument : arguments) { command += L" " + quote_argument(argument); }
    if (command.size() >= 32767) { throw std::runtime_error("Worker command line is too long."); }

    const Handle job(CreateJobObjectW(nullptr, nullptr));
    check_win32(static_cast<bool>(job), "Create worker job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION settings{};
    settings.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY;
    settings.BasicLimitInformation.ActiveProcessLimit = 1;
    settings.ProcessMemoryLimit = static_cast<SIZE_T>(limits.memory_bytes);
    settings.JobMemoryLimit = static_cast<SIZE_T>(limits.memory_bytes);
    check_win32(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &settings, sizeof(settings)), "Set worker limits");

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read_pipe{}, write_pipe{};
    check_win32(CreatePipe(&read_pipe, &write_pipe, &security, 0), "Create diagnostic pipe");
    const Handle reader(read_pipe);
    Handle writer(write_pipe);
    check_win32(SetHandleInformation(reader.get(), HANDLE_FLAG_INHERIT, 0), "Protect diagnostic reader");
    const Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &security, OPEN_EXISTING, 0, nullptr));
    check_win32(input.get() != INVALID_HANDLE_VALUE, "Open worker stdin");
    std::array<HANDLE, 2> inherited{writer.get(), input.get()};
    HandleInheritance inheritance(inherited);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = writer.get();
    startup.StartupInfo.hStdError = writer.get();
    startup.lpAttributeList = inheritance.get();
    PROCESS_INFORMATION information{};
    const auto started = std::chrono::steady_clock::now();
    check_win32(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
        executable.parent_path().c_str(), &startup.StartupInfo, &information), "Start worker");
    const Handle process(information.hProcess);
    const Handle thread(information.hThread);
    writer.reset();
    if (!AssignProcessToJobObject(job.get(), process.get())) {
        const auto error = GetLastError();
        TerminateProcess(process.get(), 1);
        WaitForSingleObject(process.get(), 1000);
        SetLastError(error);
        check_win32(false, "Assign worker job");
    }
    check_win32(ResumeThread(thread.get()) != static_cast<DWORD>(-1), "Resume worker");

    ProcessResult result;
    for (;;) {
        drain_diagnostics(reader.get(), result.diagnostic);
        const auto wait = WaitForSingleObject(process.get(), 20);
        if (wait == WAIT_OBJECT_0) { break; }
        check_win32(wait != WAIT_FAILED, "Wait for worker");
        if (std::chrono::steady_clock::now() - started >= limits.timeout) {
            result.timed_out = true;
            check_win32(TerminateJobObject(job.get(), ERROR_TIMEOUT), "Stop timed-out worker");
            check_win32(WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0, "Wait for worker termination");
            break;
        }
    }
    drain_diagnostics(reader.get(), result.diagnostic);
    DWORD exit_code = 0;
    check_win32(GetExitCodeProcess(process.get(), &exit_code), "Read worker exit status");
    result.exit_code = exit_code;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION usage{};
    check_win32(QueryInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &usage, sizeof(usage), nullptr), "Read worker memory usage");
    result.peak_committed_bytes = usage.PeakJobMemoryUsed;
    return result;
}

} // namespace thumbnail
