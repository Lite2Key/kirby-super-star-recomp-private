#pragma once

#include "kss/dsp_echo_fir.hpp"
#include "kss/dsp_stereo_mixer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kss::apu::dsp {

using DspRegisterFile = std::array<std::uint8_t, 0x80>;

enum class DspClockStatus : std::uint8_t {
    rendered,
    reset_held,
};

struct DspClockResult {
    DspClockStatus status{DspClockStatus::rendered};
    DspStereoSample sample{};
    DspMixerResult mixer{};
    DspEchoStepResult echo{};
    std::uint8_t keyed_on{};
    std::uint8_t keyed_off{};
    std::uint8_t active_voices{};
    std::uint64_t sample_clock{};
};

// Sample-boundary S-DSP bridge. Register writes are atomic between calls to
// clock_sample(); no host audio callback or wall-clock scheduling is owned here.
class DspCore {
public:
    static constexpr std::uint8_t kKeyOnDelaySamples = 5;

    void reset() noexcept;
    [[nodiscard]] bool write_register(DspRegisterFile& registers,
        std::uint8_t address, std::uint8_t value) noexcept;
    [[nodiscard]] DspClockResult clock_sample(
        DspEchoRam& apuram, DspRegisterFile& registers) noexcept;

    [[nodiscard]] std::uint64_t sample_clock() const noexcept { return sample_clock_; }
    [[nodiscard]] bool voice_active(std::size_t voice) const noexcept;
    [[nodiscard]] std::uint8_t voice_key_delay(std::size_t voice) const noexcept;
    [[nodiscard]] std::uint16_t voice_start_address(std::size_t voice) const noexcept;
    [[nodiscard]] std::uint16_t voice_loop_address(std::size_t voice) const noexcept;

private:
    std::array<DspVoicePipeline, 8> voices_{};
    std::array<bool, 8> active_{};
    std::array<std::uint8_t, 8> key_delay_{};
    std::array<std::uint16_t, 8> start_address_{};
    std::array<std::uint16_t, 8> loop_address_{};
    DspEightVoiceStereoMixer mixer_{};
    DspEchoFir echo_{};
    std::uint8_t pending_key_on_{};
    std::uint64_t sample_clock_{};
};

} // namespace kss::apu::dsp
