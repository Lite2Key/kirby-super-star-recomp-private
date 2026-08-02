#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kss::apu::dsp {

using DspEchoRam = std::array<std::uint8_t, 65536>;

struct DspEchoStereoSample {
    std::int16_t left{};
    std::int16_t right{};
};

struct DspEchoRegisters {
    std::int8_t left_volume{};   // EVOLL
    std::int8_t right_volume{};  // EVOLR
    std::int8_t feedback{};      // EFB
    std::array<std::int8_t, 8> fir{};
    std::uint8_t source_page{};  // ESA
    std::uint8_t delay{};        // EDL bits 0-3
    bool write_disable{};        // FLG bit 5
};

struct DspEchoStepInput {
    // The direct path is expected to have already had master volume applied.
    DspEchoStereoSample direct{};
    // Saturated sum of voices selected by EON, before echo feedback.
    DspEchoStereoSample echo_send{};
};

struct DspEchoStepResult {
    DspEchoStereoSample output{};
    DspEchoStereoSample filtered{};
    DspEchoStereoSample echo_write{};
    std::uint16_t address{};
    bool wrote_ram{};
};

// One step corresponds to one 32 kHz S-DSP stereo sample. This class owns only
// echo history and buffer position; register timing and voice accumulation stay
// outside it so it can be integrated with the SPC/DSP register model later.
class DspEchoFir {
public:
    void reset() noexcept;

    [[nodiscard]] DspEchoStepResult step(
        DspEchoRam& apuram,
        DspEchoStepInput input,
        const DspEchoRegisters& registers) noexcept;

    [[nodiscard]] std::uint16_t buffer_offset() const noexcept { return buffer_offset_; }
    [[nodiscard]] std::uint16_t buffer_length() const noexcept { return buffer_length_; }
    [[nodiscard]] std::uint8_t history_cursor() const noexcept { return history_cursor_; }

private:
    [[nodiscard]] std::int16_t filter_channel(std::size_t channel,
        const std::array<std::int8_t, 8>& coefficients) const noexcept;

    std::array<std::array<std::int16_t, 8>, 2> history_{};
    std::uint16_t buffer_offset_{};
    std::uint16_t buffer_length_{};
    std::uint8_t history_cursor_{};
};

} // namespace kss::apu::dsp
