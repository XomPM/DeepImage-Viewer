#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace thumbnail {

struct ProcessLimits {
    std::chrono::milliseconds timeout{15000};
    std::uint64_t memory_bytes{1024ULL * 1024 * 1024};
};

struct ProcessResult {
    std::uint32_t exit_code{};
    bool timed_out{};
    std::uint64_t peak_committed_bytes{};
    std::string diagnostic;
};

// Resource isolation, not a security sandbox. No shell or PATH lookup is used.
ProcessResult run_limited_process(const std::filesystem::path& executable,
                                 std::span<const std::wstring> arguments,
                                 const ProcessLimits& limits = {});
std::filesystem::path executable_path();

} // namespace thumbnail
