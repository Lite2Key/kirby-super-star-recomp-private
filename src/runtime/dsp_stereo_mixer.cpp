// Voice accumulation, PMON, and noise rules are informed by the ares S-DSP:
// Copyright (c) 2004-2025 ares team, Near et al; ISC license.
#include "kss/dsp_stereo_mixer.hpp"

#include <algorithm>
#include <bit>

namespace kss::apu::dsp {
namespace {

constexpr std::array<std::uint16_t, 32> kCounterRate{
    0,2048,1536,1280,1024,768,640,512,384,320,256,192,160,128,96,80,
    64,48,40,32,24,20,16,12,10,8,6,5,4,3,2,1,
};
constexpr std::array<std::uint16_t, 32> kCounterOffset{
    0,0,1040,536,0,1040,536,0,1040,536,0,1040,536,0,1040,536,
    0,1040,536,0,1040,536,0,1040,536,0,1040,536,0,1040,0,0,
};

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

constexpr std::int16_t apply_envelope(std::int16_t sample, std::uint16_t envelope) noexcept {
    const auto scaled = floor_shift(static_cast<std::int32_t>(sample) * envelope, 11);
    return static_cast<std::int16_t>(scaled & ~1);
}

} // namespace

void DspEightVoiceStereoMixer::reset_noise() noexcept {
    noise_lfsr_ = 0x4000;
    noise_counter_ = 0;
}

std::int16_t DspEightVoiceStereoMixer::current_noise_sample() const noexcept {
    return wrap16(static_cast<std::int32_t>(noise_lfsr_) << 1U);
}

void DspEightVoiceStereoMixer::advance_noise(std::uint8_t frequency) noexcept {
    if (noise_counter_ == 0) noise_counter_ = 30720;
    --noise_counter_;
    frequency &= 0x1fU;
    if (frequency == 0
        || (noise_counter_ + kCounterOffset[frequency]) % kCounterRate[frequency] != 0) {
        return;
    }
    const auto feedback = static_cast<std::uint16_t>(
        ((noise_lfsr_ << 13U) ^ (noise_lfsr_ << 14U)) & 0x4000U);
    noise_lfsr_ = static_cast<std::uint16_t>(feedback | (noise_lfsr_ >> 1U));
}

DspMixerResult DspEightVoiceStereoMixer::mix(
    const std::array<DspMixerVoiceInput, 8>& voices,
    DspMixerRegisters r) noexcept {
    DspMixerResult result{};
    result.noise_sample = current_noise_sample();
    std::int32_t left = 0;
    std::int32_t right = 0;
    std::int32_t echo_left = 0;
    std::int32_t echo_right = 0;
    std::int16_t preceding = 0;
    const auto pmon = static_cast<std::uint8_t>(r.pitch_modulation & 0xfeU);

    for (std::size_t index = 0; index < voices.size(); ++index) {
        const auto bit = static_cast<std::uint8_t>(1U << index);
        const auto base_pitch = static_cast<std::uint16_t>(voices[index].base_pitch & 0x3fffU);
        std::int32_t pitch = base_pitch;
        if ((pmon & bit) != 0) {
            pitch += floor_shift(
                floor_shift(preceding, 5) * static_cast<std::int32_t>(base_pitch), 10);
        }
        result.effective_pitch[index] = static_cast<std::uint16_t>(
            std::clamp(pitch, 0, 0x3fff));

        auto source = voices[index].voice.output;
        if ((r.noise_enable & bit) != 0) {
            source = apply_envelope(result.noise_sample, voices[index].voice.envelope);
        }
        result.source[index] = source;
        preceding = source;

        const auto soloed = r.host_solo_mask == 0 || (r.host_solo_mask & bit) != 0;
        const auto audible = voices[index].enabled && soloed && (r.host_mute_mask & bit) == 0;
        if (!audible) continue;
        result.accumulated_mask = static_cast<std::uint8_t>(result.accumulated_mask | bit);
        const auto voice_left = floor_shift(
            static_cast<std::int32_t>(source) * voices[index].left_volume, 7);
        const auto voice_right = floor_shift(
            static_cast<std::int32_t>(source) * voices[index].right_volume, 7);
        left = clamp16(left + voice_left);
        right = clamp16(right + voice_right);
        if ((r.echo_enable & bit) != 0) {
            echo_left = clamp16(echo_left + voice_left);
            echo_right = clamp16(echo_right + voice_right);
        }
    }

    result.sample.left = wrap16(floor_shift(left * r.master_left, 7));
    result.sample.right = wrap16(floor_shift(right * r.master_right, 7));
    result.echo_send.left = clamp16(echo_left);
    result.echo_send.right = clamp16(echo_right);
    if (r.global_mute) result.sample = {};
    advance_noise(r.noise_frequency);
    return result;
}

} // namespace kss::apu::dsp
