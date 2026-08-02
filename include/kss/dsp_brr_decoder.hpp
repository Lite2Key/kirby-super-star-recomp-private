#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace kss::apu::dsp {

struct BrrHistory {
    std::int16_t previous1{}; // Most recently decoded hardware sample.
    std::int16_t previous2{};

    friend constexpr bool operator==(const BrrHistory&, const BrrHistory&) = default;
};

enum class BrrDecodeStatus : std::uint8_t {
    decoded,
    invalid_block_size,
};

struct BrrBlockResult {
    BrrDecodeStatus status{BrrDecodeStatus::invalid_block_size};
    std::array<std::int16_t, 16> samples{};
    BrrHistory history{};
    std::uint8_t range{};
    std::uint8_t filter{};
    bool end{};
    bool loop{};
};

// Decode one complete nine-byte SNES BRR block. The caller owns directory,
// loop-address, voice, envelope, interpolation, and mixing behavior.
class BrrBlockDecoder {
public:
    static constexpr std::size_t kBlockSize = 9;
    static constexpr std::size_t kSamplesPerBlock = 16;

    [[nodiscard]] static BrrBlockResult decode(
        std::span<const std::uint8_t> block,
        BrrHistory history = {}) noexcept;
};

} // namespace kss::apu::dsp
