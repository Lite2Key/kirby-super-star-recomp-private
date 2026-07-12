#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace kss {

inline constexpr std::size_t kExpectedRomSize = 4U * 1024U * 1024U;
inline constexpr std::string_view kExpectedRomSha256 =
    "4e095fbbdec4a16b075d7140385ff68b259870ca9e3357f076dfff7f3d1c4a62";

using Sha256Digest = std::array<std::uint8_t, 32>;

enum class RomValidationStatus : std::uint8_t {
    valid,
    cannot_open,
    read_error,
    wrong_size,
    wrong_sha256,
};

struct RomValidationResult {
    RomValidationStatus status{RomValidationStatus::cannot_open};
    std::uintmax_t actual_size{};
    std::string actual_sha256;
    std::string message;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return status == RomValidationStatus::valid;
    }
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> bytes);
[[nodiscard]] RomValidationResult validate_rom(const std::filesystem::path& path);

} // namespace kss
