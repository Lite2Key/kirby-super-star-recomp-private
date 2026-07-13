#pragma once

#include "kss/dsp_brr_decoder.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace kss::apu::dsp {

enum class BrrStreamStatus : std::uint8_t {
    decoded,
    decoded_end,
    invalid_apuram,
};

struct BrrStreamBlock {
    BrrStreamStatus status{BrrStreamStatus::invalid_apuram};
    BrrBlockResult decoded{};
    std::uint16_t address{};
    std::uint16_t next_address{};
    bool looped{};
};

class DspBrrStream {
public:
    void reset(std::uint16_t start_address, std::uint16_t loop_address) noexcept;
    [[nodiscard]] BrrStreamBlock next(std::span<const std::uint8_t> apuram) noexcept;
    [[nodiscard]] std::uint16_t next_address() const noexcept { return next_address_; }
    [[nodiscard]] BrrHistory history() const noexcept { return history_; }

private:
    std::uint16_t next_address_{};
    std::uint16_t loop_address_{};
    BrrHistory history_{};
};

enum class DspEnvelopeMode : std::uint8_t {
    attack,
    decay,
    sustain,
    release,
};

struct DspEnvelopeRegisters {
    std::uint8_t adsr0{};
    std::uint8_t adsr1{};
    std::uint8_t gain{};
};

class DspEnvelope {
public:
    void key_on() noexcept;
    void key_off() noexcept;
    void step(DspEnvelopeRegisters registers) noexcept;

    [[nodiscard]] std::uint16_t level() const noexcept { return level_; }
    [[nodiscard]] DspEnvelopeMode mode() const noexcept { return mode_; }
    [[nodiscard]] std::uint16_t counter() const noexcept { return counter_; }

private:
    [[nodiscard]] bool counter_poll(std::uint8_t rate) const noexcept;
    void counter_tick() noexcept;

    DspEnvelopeMode mode_{DspEnvelopeMode::release};
    std::uint16_t level_{};
    std::int32_t hidden_level_{};
    std::uint16_t counter_{30720};
};

class DspGaussianInterpolator {
public:
    // Samples are oldest to newest around the interpolation point. Fraction
    // is a 12-bit position between the two center samples.
    [[nodiscard]] static std::int16_t interpolate(
        const std::array<std::int16_t, 4>& samples,
        std::uint16_t fraction) noexcept;
};

enum class DspVoiceStatus : std::uint8_t {
    running,
    source_ended,
    invalid_apuram,
};

struct DspVoiceConfig {
    std::uint16_t start_address{};
    std::uint16_t loop_address{};
    std::uint16_t pitch{0x1000};
    DspEnvelopeRegisters envelope{};
};

struct DspVoiceStep {
    DspVoiceStatus status{DspVoiceStatus::invalid_apuram};
    std::int16_t interpolated{};
    std::int16_t output{};
    std::uint16_t envelope{};
    bool block_end{};
    bool looped{};
};

class DspVoicePipeline {
public:
    void key_on(DspVoiceConfig config) noexcept;
    void key_off() noexcept { envelope_.key_off(); }
    [[nodiscard]] DspVoiceStep step(std::span<const std::uint8_t> apuram) noexcept;
    [[nodiscard]] DspVoiceStep step(std::span<const std::uint8_t> apuram,
        std::uint16_t pitch, DspEnvelopeRegisters envelope) noexcept;

    [[nodiscard]] const DspEnvelope& envelope() const noexcept { return envelope_; }
    [[nodiscard]] std::uint16_t phase() const noexcept { return phase_; }
    [[nodiscard]] std::uint16_t source_address() const noexcept {
        return stream_.next_address();
    }

private:
    [[nodiscard]] bool fetch_sample(
        std::span<const std::uint8_t> apuram,
        std::int16_t& sample,
        bool& block_end,
        bool& looped) noexcept;
    [[nodiscard]] bool prime(std::span<const std::uint8_t> apuram,
        bool& block_end, bool& looped) noexcept;

    DspVoiceConfig config_{};
    DspBrrStream stream_{};
    DspEnvelope envelope_{};
    BrrStreamBlock block_{};
    std::array<std::int16_t, 4> window_{};
    std::uint16_t phase_{};
    std::uint8_t block_sample_{};
    bool primed_{};
    bool source_ended_{};
    bool invalid_apuram_{};
};

} // namespace kss::apu::dsp
