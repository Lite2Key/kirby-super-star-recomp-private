// Echo/FIR rules are informed by the ares S-DSP implementation:
// Copyright (c) 2004-2025 ares team, Near et al; ISC license.
#include "kss/dsp_echo_fir.hpp"

#include <algorithm>
#include <bit>

namespace kss::apu::dsp {
namespace {

constexpr std::int32_t floor_shift(std::int32_t value, unsigned shift) noexcept {
    if (value >= 0) return value >> shift;
    const auto magnitude = static_cast<std::uint32_t>(-value);
    return -static_cast<std::int32_t>(
        (magnitude + (std::uint32_t{1} << shift) - 1U) >> shift);
}

constexpr std::int16_t clamp16(std::int32_t value) noexcept {
    return static_cast<std::int16_t>(std::clamp(value, -32768, 32767));
}

constexpr std::int16_t wrap16(std::int32_t value) noexcept {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

constexpr std::int16_t clear_low_bit(std::int16_t value) noexcept {
    const auto bits = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(value) & std::uint16_t{0xfffe});
    return std::bit_cast<std::int16_t>(bits);
}

std::int16_t read16(const DspEchoRam& ram, std::uint16_t address) noexcept {
    const auto next = static_cast<std::uint16_t>(address + 1U);
    const auto bits = static_cast<std::uint16_t>(ram[address]
        | (static_cast<std::uint16_t>(ram[next]) << 8U));
    return std::bit_cast<std::int16_t>(bits);
}

void write16(DspEchoRam& ram, std::uint16_t address, std::int16_t value) noexcept {
    const auto bits = std::bit_cast<std::uint16_t>(value);
    ram[address] = static_cast<std::uint8_t>(bits);
    ram[static_cast<std::uint16_t>(address + 1U)] = static_cast<std::uint8_t>(bits >> 8U);
}

constexpr std::int16_t scale_and_wrap(
    std::int16_t sample, std::int8_t volume) noexcept {
    return wrap16(floor_shift(
        static_cast<std::int32_t>(sample) * volume, 7));
}

constexpr std::int16_t feedback_sample(
    std::int16_t send, std::int16_t filtered, std::int8_t feedback) noexcept {
    const auto feedback_term = wrap16(floor_shift(
        static_cast<std::int32_t>(filtered) * feedback, 7));
    return clear_low_bit(clamp16(static_cast<std::int32_t>(send) + feedback_term));
}

} // namespace

void DspEchoFir::reset() noexcept {
    history_ = {};
    buffer_offset_ = 0;
    buffer_length_ = 0;
    history_cursor_ = 0;
}

std::int16_t DspEchoFir::filter_channel(std::size_t channel,
    const std::array<std::int8_t, 8>& coefficients) const noexcept {
    std::int32_t first_seven = 0;
    for (std::size_t tap = 0; tap < 7; ++tap) {
        // FIR0 addresses the oldest retained sample; FIR7 addresses the sample
        // read during this step, matching the S-DSP's rotating history order.
        const auto index = static_cast<std::uint8_t>((history_cursor_ + tap + 1U) & 7U);
        first_seven += floor_shift(
            static_cast<std::int32_t>(history_[channel][index]) * coefficients[tap], 6);
    }
    const auto newest = history_[channel][history_cursor_];
    const auto tap_seven = floor_shift(
        static_cast<std::int32_t>(newest) * coefficients[7], 6);

    // Hardware truncates the tap 0-6 subtotal and tap 7 independently to 16
    // bits, then saturates their sum and forces an even result.
    const auto combined = static_cast<std::int32_t>(wrap16(first_seven))
        + wrap16(tap_seven);
    return clear_low_bit(clamp16(combined));
}

DspEchoStepResult DspEchoFir::step(
    DspEchoRam& apuram,
    DspEchoStepInput input,
    const DspEchoRegisters& r) noexcept {
    DspEchoStepResult result{};
    if (buffer_offset_ == 0) {
        buffer_length_ = static_cast<std::uint16_t>((r.delay & 0x0fU) << 11U);
    }

    result.address = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(r.source_page) << 8U) + buffer_offset_);
    history_cursor_ = static_cast<std::uint8_t>((history_cursor_ + 1U) & 7U);
    history_[0][history_cursor_] = static_cast<std::int16_t>(
        floor_shift(read16(apuram, result.address), 1));
    history_[1][history_cursor_] = static_cast<std::int16_t>(floor_shift(read16(
        apuram, static_cast<std::uint16_t>(result.address + 2U)), 1));

    result.filtered.left = filter_channel(0, r.fir);
    result.filtered.right = filter_channel(1, r.fir);

    const auto echo_left = scale_and_wrap(result.filtered.left, r.left_volume);
    const auto echo_right = scale_and_wrap(result.filtered.right, r.right_volume);
    result.output.left = clamp16(static_cast<std::int32_t>(input.direct.left) + echo_left);
    result.output.right = clamp16(static_cast<std::int32_t>(input.direct.right) + echo_right);

    result.echo_write.left = feedback_sample(
        input.echo_send.left, result.filtered.left, r.feedback);
    result.echo_write.right = feedback_sample(
        input.echo_send.right, result.filtered.right, r.feedback);
    if (!r.write_disable) {
        write16(apuram, result.address, result.echo_write.left);
        write16(apuram, static_cast<std::uint16_t>(result.address + 2U),
            result.echo_write.right);
        result.wrote_ram = true;
    }

    buffer_offset_ = static_cast<std::uint16_t>(buffer_offset_ + 4U);
    if (buffer_length_ == 0 || buffer_offset_ >= buffer_length_) buffer_offset_ = 0;
    return result;
}

} // namespace kss::apu::dsp
