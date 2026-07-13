#include "kss/dsp_core.hpp"

#include <algorithm>
#include <bit>

namespace kss::apu::dsp {
namespace {

constexpr std::int8_t signed_register(std::uint8_t value) noexcept {
    return std::bit_cast<std::int8_t>(value);
}

constexpr std::int32_t floor_shift(std::int32_t value, unsigned shift) noexcept {
    if (value >= 0) return value >> shift;
    const auto magnitude = static_cast<std::uint32_t>(-value);
    return -static_cast<std::int32_t>(
        (magnitude + (std::uint32_t{1} << shift) - 1U) >> shift);
}

constexpr std::int16_t wrap16(std::int32_t value) noexcept {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

constexpr std::int16_t envelope_noise(
    std::int16_t noise, std::uint16_t envelope) noexcept {
    return static_cast<std::int16_t>(
        floor_shift(static_cast<std::int32_t>(noise) * envelope, 11) & ~1);
}

std::uint16_t read16(const DspEchoRam& ram, std::uint16_t address) noexcept {
    const auto high_address = static_cast<std::uint16_t>(address + 1U);
    return static_cast<std::uint16_t>(ram[address]
        | (static_cast<std::uint16_t>(ram[high_address]) << 8U));
}

std::uint16_t voice_pitch(const DspRegisterFile& registers, std::size_t voice) noexcept {
    const auto base = voice * 0x10U;
    return static_cast<std::uint16_t>((registers[base + 2U]
        | (static_cast<std::uint16_t>(registers[base + 3U]) << 8U)) & 0x3fffU);
}

DspEnvelopeRegisters voice_envelope(
    const DspRegisterFile& registers, std::size_t voice) noexcept {
    const auto base = voice * 0x10U;
    return {registers[base + 5U], registers[base + 6U], registers[base + 7U]};
}

} // namespace

void DspCore::reset() noexcept {
    voices_ = {};
    active_.fill(false);
    key_delay_.fill(0);
    start_address_.fill(0);
    loop_address_.fill(0);
    mixer_ = {};
    mixer_.reset_noise();
    echo_.reset();
    pending_key_on_ = 0;
    sample_clock_ = 0;
}

bool DspCore::write_register(DspRegisterFile& registers,
    std::uint8_t address, std::uint8_t value) noexcept {
    if (address >= registers.size()) return false;
    if (address == 0x7cU) {
        registers[address] = 0;
        return true;
    }
    registers[address] = value;
    if (address == 0x4cU) {
        pending_key_on_ = value;
    }
    return true;
}

bool DspCore::voice_active(std::size_t voice) const noexcept {
    return voice < active_.size() && active_[voice];
}

std::uint8_t DspCore::voice_key_delay(std::size_t voice) const noexcept {
    return voice < key_delay_.size() ? key_delay_[voice] : 0;
}

std::uint16_t DspCore::voice_start_address(std::size_t voice) const noexcept {
    return voice < start_address_.size() ? start_address_[voice] : 0;
}

std::uint16_t DspCore::voice_loop_address(std::size_t voice) const noexcept {
    return voice < loop_address_.size() ? loop_address_[voice] : 0;
}

DspClockResult DspCore::clock_sample(
    DspEchoRam& apuram, DspRegisterFile& registers) noexcept {
    DspClockResult result{};
    result.sample_clock = ++sample_clock_;

    if ((registers[0x6c] & 0x80U) != 0) {
        active_.fill(false);
        key_delay_.fill(0);
        pending_key_on_ = 0;
        mixer_.reset_noise();
        echo_.reset();
        for (std::size_t voice = 0; voice < voices_.size(); ++voice) {
            registers[voice * 0x10U + 8U] = 0;
            registers[voice * 0x10U + 9U] = 0;
        }
        result.status = DspClockStatus::reset_held;
        return result;
    }

    const auto key_on = pending_key_on_;
    pending_key_on_ = 0;
    const auto key_off = registers[0x5c];
    result.keyed_on = key_on;
    result.keyed_off = static_cast<std::uint8_t>(key_off & ~key_on);
    for (std::size_t voice = 0; voice < voices_.size(); ++voice) {
        const auto mask = static_cast<std::uint8_t>(1U << voice);
        if ((key_on & mask) != 0) {
            const auto directory = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(registers[0x5d]) << 8U)
                + static_cast<std::uint16_t>(registers[voice * 0x10U + 4U] * 4U));
            start_address_[voice] = read16(apuram, directory);
            loop_address_[voice] = read16(
                apuram, static_cast<std::uint16_t>(directory + 2U));
            DspVoiceConfig config{};
            config.start_address = start_address_[voice];
            config.loop_address = loop_address_[voice];
            config.pitch = voice_pitch(registers, voice);
            config.envelope = voice_envelope(registers, voice);
            voices_[voice].key_on(config);
            active_[voice] = true;
            key_delay_[voice] = kKeyOnDelaySamples;
            registers[0x7c] = static_cast<std::uint8_t>(registers[0x7c] & ~mask);
        } else if ((key_off & mask) != 0 && active_[voice]) {
            voices_[voice].key_off();
        }
    }

    std::array<DspMixerVoiceInput, 8> mixer_voices{};
    const auto noise = mixer_.current_noise_sample();
    std::int16_t preceding_source = 0;
    for (std::size_t voice = 0; voice < voices_.size(); ++voice) {
        const auto mask = static_cast<std::uint8_t>(1U << voice);
        auto pitch = static_cast<std::int32_t>(voice_pitch(registers, voice));
        if (voice != 0 && (registers[0x2d] & mask) != 0) {
            pitch += floor_shift(floor_shift(preceding_source, 5) * pitch, 10);
        }
        pitch = std::clamp(pitch, 0, 0x3fff);

        DspVoiceStep voice_step{};
        voice_step.status = DspVoiceStatus::running;
        if (active_[voice] && key_delay_[voice] != 0) {
            --key_delay_[voice];
        } else if (active_[voice]) {
            voice_step = voices_[voice].step(apuram,
                static_cast<std::uint16_t>(pitch), voice_envelope(registers, voice));
            if (voice_step.block_end || voice_step.looped
                || voice_step.status == DspVoiceStatus::source_ended) {
                registers[0x7c] = static_cast<std::uint8_t>(registers[0x7c] | mask);
            }
            if (voices_[voice].envelope().mode() == DspEnvelopeMode::release
                && voices_[voice].envelope().level() == 0) {
                active_[voice] = false;
            }
        }

        const auto base = voice * 0x10U;
        registers[base + 8U] = static_cast<std::uint8_t>(voice_step.envelope >> 4U);
        registers[base + 9U] = static_cast<std::uint8_t>(
            std::bit_cast<std::uint16_t>(voice_step.output) >> 8U);
        mixer_voices[voice].voice = voice_step;
        mixer_voices[voice].base_pitch = voice_pitch(registers, voice);
        mixer_voices[voice].left_volume = signed_register(registers[base]);
        mixer_voices[voice].right_volume = signed_register(registers[base + 1U]);
        mixer_voices[voice].enabled = active_[voice] || key_delay_[voice] != 0;

        preceding_source = (registers[0x3d] & mask) != 0
            ? envelope_noise(noise, voice_step.envelope) : voice_step.output;
        if (active_[voice]) result.active_voices =
            static_cast<std::uint8_t>(result.active_voices | mask);
    }

    DspMixerRegisters mixer_registers{};
    mixer_registers.master_left = signed_register(registers[0x0c]);
    mixer_registers.master_right = signed_register(registers[0x1c]);
    mixer_registers.noise_enable = registers[0x3d];
    mixer_registers.echo_enable = registers[0x4d];
    mixer_registers.noise_frequency = static_cast<std::uint8_t>(registers[0x6c] & 0x1fU);
    mixer_registers.pitch_modulation = registers[0x2d];
    mixer_registers.global_mute = (registers[0x6c] & 0x40U) != 0;
    result.mixer = mixer_.mix(mixer_voices, mixer_registers);

    DspEchoRegisters echo_registers{};
    echo_registers.left_volume = signed_register(registers[0x2c]);
    echo_registers.right_volume = signed_register(registers[0x3c]);
    echo_registers.feedback = signed_register(registers[0x0d]);
    echo_registers.source_page = registers[0x6d];
    echo_registers.delay = static_cast<std::uint8_t>(registers[0x7d] & 0x0fU);
    echo_registers.write_disable = (registers[0x6c] & 0x20U) != 0;
    for (std::size_t tap = 0; tap < echo_registers.fir.size(); ++tap) {
        echo_registers.fir[tap] = signed_register(registers[tap * 0x10U + 0x0fU]);
    }
    DspEchoStepInput echo_input{};
    echo_input.direct = {result.mixer.sample.left, result.mixer.sample.right};
    echo_input.echo_send = {
        result.mixer.echo_send.left, result.mixer.echo_send.right};
    result.echo = echo_.step(apuram, echo_input, echo_registers);
    result.sample = {result.echo.output.left, result.echo.output.right};
    if ((registers[0x6c] & 0x40U) != 0) result.sample = {};
    return result;
}

} // namespace kss::apu::dsp
