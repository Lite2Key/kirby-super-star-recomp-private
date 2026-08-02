#include "kss/dsp_voice_pipeline.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {

using namespace kss::apu::dsp;

void put_block(std::vector<std::uint8_t>& ram, std::uint16_t address,
    std::uint8_t header, std::uint8_t packed) {
    ram[address] = header;
    for (unsigned index = 1; index < 9; ++index) {
        ram[static_cast<std::uint16_t>(address + index)] = packed;
    }
}

void test_brr_stream_sequencing_end_loop_and_wrap() {
    std::vector<std::uint8_t> ram(65536);
    put_block(ram, 0x0100, 0x10, 0x11);
    put_block(ram, 0x0109, 0x11, 0x22);
    DspBrrStream stream; stream.reset(0x0100, 0x0300);
    const auto first = stream.next(ram);
    assert(first.status == BrrStreamStatus::decoded && first.address == 0x0100);
    assert(first.next_address == 0x0109 && !first.looped);
    assert(first.decoded.samples[0] == 2 && first.decoded.samples.size() == 16);
    const auto end = stream.next(ram);
    assert(end.status == BrrStreamStatus::decoded_end && end.address == 0x0109);
    assert(end.next_address == 0x0112 && end.decoded.end && !end.decoded.loop);

    put_block(ram, 0x0200, 0x13, 0x33);
    DspBrrStream looping; looping.reset(0x0200, 0x0300);
    const auto loop = looping.next(ram);
    assert(loop.status == BrrStreamStatus::decoded && loop.looped);
    assert(loop.next_address == 0x0300 && looping.next_address() == 0x0300);

    ram[0xfffc] = 0x10;
    for (unsigned index = 1; index < 9; ++index) {
        ram[static_cast<std::uint16_t>(0xfffcU + index)] = 0x44;
    }
    DspBrrStream wrapping; wrapping.reset(0xfffc, 0);
    const auto wrapped = wrapping.next(ram);
    assert(wrapped.status == BrrStreamStatus::decoded);
    assert(wrapped.next_address == 0x0005 && wrapped.decoded.samples[0] == 8);

    std::array<std::uint8_t, 32> short_ram{};
    assert(stream.next(short_ram).status == BrrStreamStatus::invalid_apuram);
}

void test_verified_gaussian_coefficients_and_exact_outputs() {
    constexpr std::array<std::int16_t, 4> samples{-12000,-4000,8000,16000};
    struct Vector { std::uint16_t phase; std::int16_t output; };
    constexpr Vector vectors[]{
        {0x000,-3258},{0x400,-690},{0x800,2024},{0xc00,4736},{0xfff,7256},
    };
    for (const auto vector : vectors) {
        assert(DspGaussianInterpolator::interpolate(samples, vector.phase)
            == vector.output);
    }
    constexpr std::array<std::int16_t, 4> maximum{32767,32767,32767,32767};
    const auto saturated = DspGaussianInterpolator::interpolate(maximum, 0x800);
    assert(saturated <= 32766 && (saturated & 1) == 0);
}

void test_adsr_gain_release_and_rate_counter() {
    DspEnvelope envelope;
    envelope.key_on();
    envelope.step({0x8f,0x00,0});
    assert(envelope.level() == 0x400 && envelope.mode() == DspEnvelopeMode::attack);
    envelope.step({0x8f,0x00,0});
    assert(envelope.level() == 0x7ff && envelope.mode() == DspEnvelopeMode::decay);
    envelope.key_off();
    envelope.step({0x8f,0,0});
    assert(envelope.level() == 0x7f7 && envelope.mode() == DspEnvelopeMode::release);

    DspEnvelope gain;
    gain.key_on();
    gain.step({0,0,0x60}); // Direct gain.
    assert(gain.level() == 0x600);
    gain.step({0,0,0x9f}); // Linear decrease, rate 31.
    assert(gain.level() == 0x5e0);
    gain.step({0,0,0xbf}); // Exponential decrease, rate 31.
    assert(gain.level() == 0x5da);
    gain.step({0,0,0xdf}); // Linear increase, rate 31.
    assert(gain.level() == 0x5fa);
    gain.step({0,0,0x60});
    gain.step({0,0,0xff}); // Bent increase above $600 uses +8.
    assert(gain.level() == 0x608);

    DspEnvelope rate30;
    rate30.key_on();
    rate30.step({0,0,0xde});
    assert(rate30.level() == 0x20);
    rate30.step({0,0,0xde});
    assert(rate30.level() == 0x20);
    rate30.step({0,0,0xde});
    assert(rate30.level() == 0x40);
}

void test_voice_pipeline_end_loop_envelope_and_invalid_memory() {
    std::vector<std::uint8_t> ram(65536);
    put_block(ram, 0x0100, 0x13, 0x44); // End+loop, constant decoded sample 8.
    DspVoicePipeline loop;
    loop.key_on({0x0100,0x0100,0x1000,{0,0,0x7f}});
    const auto first = loop.step(ram);
    assert(first.status == DspVoiceStatus::running && first.envelope == 0);
    const auto second = loop.step(ram);
    assert(second.envelope == 0x7f0 && second.output != 0);
    bool saw_loop = false;
    for (unsigned step = 2; step < 16; ++step) {
        const auto sample = loop.step(ram);
        saw_loop = saw_loop || sample.looped;
        assert(sample.status == DspVoiceStatus::running);
    }
    assert(saw_loop && loop.source_address() == 0x0100);

    put_block(ram, 0x0200, 0x11, 0x44); // End without loop.
    DspVoicePipeline ending;
    ending.key_on({0x0200,0x0300,0x1000,{0,0,0x7f}});
    bool ended = false;
    for (unsigned step = 0; step < 20; ++step) {
        ended = ended || ending.step(ram).status == DspVoiceStatus::source_ended;
    }
    assert(ended && ending.envelope().mode() == DspEnvelopeMode::release);

    std::array<std::uint8_t, 64> invalid{};
    DspVoicePipeline bad;
    bad.key_on({});
    assert(bad.step(invalid).status == DspVoiceStatus::invalid_apuram);
}

} // namespace

int main() {
    test_brr_stream_sequencing_end_loop_and_wrap();
    test_verified_gaussian_coefficients_and_exact_outputs();
    test_adsr_gain_release_and_rate_counter();
    test_voice_pipeline_end_loop_envelope_and_invalid_memory();
}
