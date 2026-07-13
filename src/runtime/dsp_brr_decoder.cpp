// Filter structure and hardware scaling are informed by the ares S-DSP core:
// Copyright (c) 2004-2025 ares team, Near et al; ISC license.
#include "kss/dsp_brr_decoder.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace kss::apu::dsp {
namespace {

constexpr std::int32_t arithmetic_shift_right(
    std::int32_t value, unsigned shift) noexcept {
    if (value >= 0) return value >> shift;
    const auto magnitude = static_cast<std::uint32_t>(-value);
    const auto rounded = (magnitude + (std::uint32_t{1} << shift) - 1U) >> shift;
    return -static_cast<std::int32_t>(rounded);
}

constexpr std::int16_t double_and_wrap(std::int32_t value) noexcept {
    const auto bits = static_cast<std::uint16_t>(static_cast<std::uint32_t>(value * 2) & 0xffffU);
    return std::bit_cast<std::int16_t>(bits);
}

constexpr std::int32_t decode_nibble(std::uint8_t nibble, std::uint8_t range) noexcept {
    const auto signed_nibble = nibble < 8U
        ? static_cast<std::int32_t>(nibble)
        : static_cast<std::int32_t>(nibble) - 16;
    if (range <= 12U) {
        return arithmetic_shift_right(signed_nibble * (std::int32_t{1} << range), 1);
    }
    return signed_nibble < 0 ? -2048 : 0;
}

constexpr std::int32_t apply_filter(
    std::int32_t sample,
    std::uint8_t filter,
    BrrHistory history) noexcept {
    const auto p1 = static_cast<std::int32_t>(history.previous1);
    const auto p2 = arithmetic_shift_right(history.previous2, 1);
    switch (filter) {
    case 0: break;
    case 1:
        sample += arithmetic_shift_right(p1, 1);
        sample += arithmetic_shift_right(-p1, 5);
        break;
    case 2:
        sample += p1;
        sample -= p2;
        sample += arithmetic_shift_right(p2, 4);
        sample += arithmetic_shift_right(p1 * -3, 6);
        break;
    case 3:
        sample += p1;
        sample -= p2;
        sample += arithmetic_shift_right(p1 * -13, 7);
        sample += arithmetic_shift_right(p2 * 3, 4);
        break;
    }
    return sample;
}

} // namespace

BrrBlockResult BrrBlockDecoder::decode(
    std::span<const std::uint8_t> block,
    BrrHistory history) noexcept {
    BrrBlockResult result{};
    result.history = history;
    if (block.size() != kBlockSize) return result;

    const auto header = block[0];
    result.status = BrrDecodeStatus::decoded;
    result.range = static_cast<std::uint8_t>(header >> 4U);
    result.filter = static_cast<std::uint8_t>((header >> 2U) & 0x03U);
    result.end = (header & 0x01U) != 0;
    result.loop = (header & 0x02U) != 0;

    std::size_t output = 0;
    for (std::size_t byte_index = 1; byte_index < kBlockSize; ++byte_index) {
        const std::array nibbles{
            static_cast<std::uint8_t>(block[byte_index] >> 4U),
            static_cast<std::uint8_t>(block[byte_index] & 0x0fU),
        };
        for (const auto nibble : nibbles) {
            auto sample = decode_nibble(nibble, result.range);
            sample = apply_filter(sample, result.filter, result.history);
            sample = std::clamp(sample,
                static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
                static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max()));
            const auto hardware_sample = double_and_wrap(sample);
            result.samples[output++] = hardware_sample;
            result.history.previous2 = result.history.previous1;
            result.history.previous1 = hardware_sample;
        }
    }
    return result;
}

} // namespace kss::apu::dsp
