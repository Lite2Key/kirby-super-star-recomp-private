#include "kss/spc700.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

using kss::apu::Spc700Core;
using kss::apu::dsp::DspClockStatus;

std::array<std::uint8_t, Spc700Core::kIplSize> test_ipl() {
    std::array<std::uint8_t, Spc700Core::kIplSize> ipl{};
    ipl[0] = 0x00; // NOP
    ipl[62] = 0xc0;
    ipl[63] = 0xff;
    return ipl;
}

void initialize(Spc700Core& core) {
    const auto ipl = test_ipl();
    assert(core.load_ipl(ipl));
    assert(core.reset());
}

void dsp_write(Spc700Core& core, std::uint8_t address, std::uint8_t value) {
    core.write8(0x00f2, address);
    core.write8(0x00f3, value);
}

std::uint8_t dsp_read(Spc700Core& core, std::uint8_t address) {
    core.write8(0x00f2, address);
    return core.read8(0x00f3);
}

void store16(Spc700Core& core, std::uint16_t address, std::uint16_t value) {
    core.ram()[address] = static_cast<std::uint8_t>(value);
    core.ram()[static_cast<std::uint16_t>(address + 1U)] =
        static_cast<std::uint8_t>(value >> 8U);
}

void seed_voice(Spc700Core& core, std::size_t voice, std::uint8_t source,
    std::uint16_t start, std::uint16_t loop, std::uint8_t header = 0) {
    constexpr std::uint8_t directory_page = 0x20;
    const auto voice_base = static_cast<std::uint8_t>(voice * 0x10U);
    const auto directory = static_cast<std::uint16_t>(
        (directory_page << 8U) + source * 4U);
    store16(core, directory, start);
    store16(core, static_cast<std::uint16_t>(directory + 2U), loop);
    core.ram()[start] = header;
    for (std::uint16_t index = 1; index < 9; ++index) {
        core.ram()[static_cast<std::uint16_t>(start + index)] = 0x77;
    }
    dsp_write(core, 0x5d, directory_page);
    dsp_write(core, static_cast<std::uint8_t>(voice_base + 0x00U), 0x7f);
    dsp_write(core, static_cast<std::uint8_t>(voice_base + 0x01U), 0x7f);
    dsp_write(core, static_cast<std::uint8_t>(voice_base + 0x02U), 0x00);
    dsp_write(core, static_cast<std::uint8_t>(voice_base + 0x03U), 0x10);
    dsp_write(core, static_cast<std::uint8_t>(voice_base + 0x04U), source);
    dsp_write(core, static_cast<std::uint8_t>(voice_base + 0x05U), 0x00);
    dsp_write(core, static_cast<std::uint8_t>(voice_base + 0x07U), 0x7f);
    dsp_write(core, 0x0c, 0x7f);
    dsp_write(core, 0x1c, 0x7f);
}

void test_explicit_clock_kon_directory_and_visible_voice_registers() {
    Spc700Core core;
    initialize(core);
    seed_voice(core, 0, 3, 0x3000, 0x3100);
    dsp_write(core, 0x4c, 0x01);

    assert(core.dsp_core().sample_clock() == 0);
    assert(!core.dsp_core().voice_active(0));
    assert(core.step().instruction_cycles == 2); // SPC execution is not a DSP clock.
    assert(core.dsp_core().sample_clock() == 0);

    auto sample = core.clock_dsp_sample();
    assert(sample.status == DspClockStatus::rendered && sample.keyed_on == 0x01);
    assert(core.dsp_core().voice_active(0));
    assert(core.dsp_core().voice_start_address(0) == 0x3000);
    assert(core.dsp_core().voice_loop_address(0) == 0x3100);
    assert(core.dsp_core().voice_key_delay(0) == 4);
    for (unsigned count = 1; count < 5; ++count) {
        sample = core.clock_dsp_sample();
    }
    assert(core.dsp_core().voice_key_delay(0) == 0);
    assert(dsp_read(core, 0x08) == 0 && dsp_read(core, 0x09) == 0);

    (void)core.clock_dsp_sample(); // First active pipeline clock raises direct GAIN.
    sample = core.clock_dsp_sample();
    assert(dsp_read(core, 0x08) == 0x7f);
    assert(dsp_read(core, 0x09) == static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(sample.mixer.source[0]) >> 8U));
    assert(sample.active_voices == 0x01 && sample.sample_clock == 7);
}

void test_endx_clear_and_level_sensitive_koff_release() {
    Spc700Core core;
    initialize(core);
    seed_voice(core, 0, 4, 0x3200, 0x3300, 0x03); // END+LOOP BRR block.
    dsp_write(core, 0x4c, 0x01);
    for (unsigned count = 0; count < 6; ++count) (void)core.clock_dsp_sample();
    assert((dsp_read(core, 0x7c) & 0x01U) != 0);

    dsp_write(core, 0x7c, 0xff);
    assert(dsp_read(core, 0x7c) == 0); // Any ENDX write clears every bit.
    (void)core.clock_dsp_sample();
    assert(dsp_read(core, 0x08) == 0x7f);

    dsp_write(core, 0x5c, 0x01);
    const auto keyoff = core.clock_dsp_sample();
    assert(keyoff.keyed_off == 0x01);
    for (unsigned count = 0; count < 300 && core.dsp_core().voice_active(0); ++count) {
        (void)core.clock_dsp_sample();
    }
    assert(!core.dsp_core().voice_active(0));
    assert(dsp_read(core, 0x08) == 0);
}

void test_global_mixer_echo_write_disable_mute_and_reset() {
    Spc700Core core;
    initialize(core);
    seed_voice(core, 0, 5, 0x3400, 0x3500, 0xc0); // High-range BRR test signal.
    dsp_write(core, 0x4d, 0x01); // EON voice 0
    dsp_write(core, 0x2c, 0x7f); // EVOLL
    dsp_write(core, 0x3c, 0x7f); // EVOLR
    dsp_write(core, 0x0d, 0x40); // EFB
    dsp_write(core, 0x6d, 0x40); // ESA=$4000
    dsp_write(core, 0x7d, 0x00); // EDL=0 reuses one stereo frame.
    dsp_write(core, 0x7f, 0x40); // FIR7=1.0
    dsp_write(core, 0x4c, 0x01);

    kss::apu::dsp::DspClockResult sample{};
    bool saw_echo_send = false;
    for (unsigned count = 0; count < 24 && sample.echo.echo_write.left == 0; ++count) {
        sample = core.clock_dsp_sample();
        saw_echo_send = saw_echo_send || sample.mixer.echo_send.left != 0;
    }
    assert(saw_echo_send && sample.echo.wrote_ram);
    assert(sample.echo.address == 0x4000);
    assert(sample.echo.echo_write.left != 0);
    sample = core.clock_dsp_sample();
    assert(sample.echo.filtered.left != 0);

    const auto before_disable = std::array<std::uint8_t, 4>{
        core.ram()[0x4000], core.ram()[0x4001], core.ram()[0x4002], core.ram()[0x4003]};
    dsp_write(core, 0x6c, 0x20); // Echo write disable.
    sample = core.clock_dsp_sample();
    assert(!sample.echo.wrote_ram);
    assert((std::array<std::uint8_t, 4>{
        core.ram()[0x4000], core.ram()[0x4001], core.ram()[0x4002], core.ram()[0x4003]})
        == before_disable);

    dsp_write(core, 0x6c, 0x40); // Global mute; echo state and writes still advance.
    sample = core.clock_dsp_sample();
    assert(sample.sample.left == 0 && sample.sample.right == 0);
    assert(sample.echo.wrote_ram);

    dsp_write(core, 0x6c, 0x80); // Soft reset is an explicit held state.
    sample = core.clock_dsp_sample();
    assert(sample.status == DspClockStatus::reset_held);
    assert(sample.sample.left == 0 && sample.active_voices == 0);
    assert(!core.dsp_core().voice_active(0) && dsp_read(core, 0x08) == 0);
}

} // namespace

int main() {
    test_explicit_clock_kon_directory_and_visible_voice_registers();
    test_endx_clear_and_level_sensitive_koff_release();
    test_global_mixer_echo_write_disable_mute_and_reset();
}
