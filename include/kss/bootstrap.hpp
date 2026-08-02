#pragma once

#include "kss/spc700.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>

namespace kss {

class NativeHostPlatform;

struct BootstrapOptions {
    std::filesystem::path rom_path;
    std::filesystem::path save_path;
    std::filesystem::path frame_output;
    std::filesystem::path spc_ipl_path;
    NativeHostPlatform* host{};
    bool validate_only{};
};

enum class SpcIplLoadStatus : std::uint8_t {
    loaded,
    file_not_found,
    wrong_file_size,
    read_error,
};

struct SpcIplLoadResult {
    SpcIplLoadStatus status{SpcIplLoadStatus::file_not_found};
    std::array<std::uint8_t, apu::Spc700Core::kIplSize> bytes{};
    std::uintmax_t file_size{};

    [[nodiscard]] bool ok() const noexcept {
        return status == SpcIplLoadStatus::loaded;
    }
};

// Loads an external SNES SPC700 IPL image without installing or copying it
// into the project. The only accepted file size is the hardware IPL's 64 bytes.
[[nodiscard]] SpcIplLoadResult load_spc_ipl(
    const std::filesystem::path& path) noexcept;

enum class BootstrapExitCode : int {
    success = 0,
    usage_error = 2,
    rom_rejected = 3,
    translated_frontier = 4,
    save_error = 5,
    host_error = 6,
    spc_ipl_rejected = 7,
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
