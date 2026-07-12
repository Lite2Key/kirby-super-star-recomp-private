#pragma once

#include <filesystem>
#include <iosfwd>

namespace kss {

struct BootstrapOptions {
    std::filesystem::path rom_path;
    bool validate_only{};
};

enum class BootstrapExitCode : int {
    success = 0,
    usage_error = 2,
    rom_rejected = 3,
};

[[nodiscard]] BootstrapExitCode run_bootstrap(
    const BootstrapOptions& options,
    std::ostream& output,
    std::ostream& errors);

[[nodiscard]] BootstrapExitCode runtime_cli(
    int argc,
    const char* const* argv,
    std::ostream& output,
    std::ostream& errors);

} // namespace kss
