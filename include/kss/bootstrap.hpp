#pragma once

#include <filesystem>
#include <iosfwd>

namespace kss {

class NativeHostPlatform;

struct BootstrapOptions {
    std::filesystem::path rom_path;
    std::filesystem::path save_path;
    std::filesystem::path frame_output;
    NativeHostPlatform* host{};
    bool validate_only{};
};

enum class BootstrapExitCode : int {
    success = 0,
    usage_error = 2,
    rom_rejected = 3,
    translated_frontier = 4,
    save_error = 5,
    host_error = 6,
};

[[nodiscard]] BootstrapExitCode run_bootstrap(
    const BootstrapOptions& options,
    std::ostream& output,
    std::ostream& errors);

[[nodiscard]] BootstrapExitCode runtime_cli(
    int argc,
    const char* const* argv,
    std::ostream& output,
    std::ostream& errors,
    NativeHostPlatform* host = nullptr);

} // namespace kss
