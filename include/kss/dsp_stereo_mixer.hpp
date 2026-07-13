#pragma once

#include "kss/dsp_voice_pipeline.hpp"

#include <array>
#include <cstdint>

namespace kss::apu::dsp {

struct DspMixerVoiceInput {
    DspVoiceStep voice{};
    std::uint16_t base_pitch{0x1000};
    std::int8_t left_volume{};
    std::int8_t right_volume{};
    bool enabled{true};
};

struct DspMixerRegisters {
    std::int8_t master_left{127};
    std::int8_t master_right{127};
    std::uint8_t noise_enable{};     // NON
    std::uint8_t echo_enable{};      // EON
    std::uint8_t noise_frequency{};  // FLG bits 0-4
    std::uint8_t pitch_modulation{}; // PMON; bit 0 is architecturally ignored.
    bool global_mute{};

    // Host inspection controls. They gate only accumulation, never DSP state,
    // noise, or the preceding-voice source used by pitch modulation.
    std::uint8_t host_mute_mask{};
    std::uint8_t host_solo_mask{};
};

struct DspStereoSample {
    std::int16_t left{};
    std::int16_t right{};
};

struct DspMixerResult {
    DspStereoSample sample{};
    DspStereoSample echo_send{};
    std::array<std::int16_t, 8> source{};
    std::array<std::uint16_t, 8> effective_pitch{};
    std::uint8_t accumulated_mask{};
    std::int16_t noise_sample{};
};

class DspEightVoiceStereoMixer {
public:
    void reset_noise() noexcept;
    [[nodiscard]] DspMixerResult mix(
        const std::array<DspMixerVoiceInput, 8>& voices,
        DspMixerRegisters registers) noexcept;

    [[nodiscard]] std::uint16_t noise_lfsr() const noexcept { return noise_lfsr_; }
    [[nodiscard]] std::uint16_t noise_counter() const noexcept { return noise_counter_; }
    [[nodiscard]] std::int16_t current_noise_sample() const noexcept;

private:
    void advance_noise(std::uint8_t frequency) noexcept;

    std::uint16_t noise_lfsr_{0x4000};
    std::uint16_t noise_counter_{};
};

} // namespace kss::apu::dsp
