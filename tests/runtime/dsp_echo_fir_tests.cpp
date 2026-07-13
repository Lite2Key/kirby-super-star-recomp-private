#include "kss/dsp_echo_fir.hpp"

#include <cassert>
#include <cstdint>

namespace {

using namespace kss::apu::dsp;

void store(DspEchoRam& ram, std::uint16_t address, std::int16_t value) {
    const auto bits = static_cast<std::uint16_t>(value);
    ram[address] = static_cast<std::uint8_t>(bits);
    ram[static_cast<std::uint16_t>(address + 1U)] = static_cast<std::uint8_t>(bits >> 8U);
}

std::int16_t load(const DspEchoRam& ram, std::uint16_t address) {
    const auto next = static_cast<std::uint16_t>(address + 1U);
    const auto bits = static_cast<std::uint16_t>(ram[address]
        | (static_cast<std::uint16_t>(ram[next]) << 8U));
    return static_cast<std::int16_t>(bits);
}

void test_echo_address_wraps_in_apuram_and_at_edl_length() {
    DspEchoRam ram{};
    DspEchoRegisters registers{};
    registers.source_page = 0xff;
    registers.delay = 1;
    registers.write_disable = true;
    DspEchoFir echo;

    DspEchoStepResult result{};
    for (unsigned frame = 0; frame <= 64; ++frame) result = echo.step(ram, {}, registers);
    assert(result.address == 0x0000); // 0xff00 + 0x0100 wraps at 64 KiB.

    for (unsigned frame = 65; frame < 512; ++frame) result = echo.step(ram, {}, registers);
    assert(result.address == 0x06fc);
    assert(echo.buffer_offset() == 0);
    result = echo.step(ram, {}, registers);
    assert(result.address == 0xff00 && echo.buffer_length() == 0x0800);

    echo.reset();
    registers.delay = 0;
    const auto zero_a = echo.step(ram, {}, registers);
    const auto zero_b = echo.step(ram, {}, registers);
    assert(zero_a.address == 0xff00 && zero_b.address == 0xff00);
}

void test_fir7_is_newest_and_fir0_is_oldest() {
    DspEchoRam ram{};
    DspEchoRegisters registers{};
    registers.delay = 1;
    registers.write_disable = true;
    registers.fir[7] = 64;
    DspEchoFir newest;
    store(ram, 0, 1000);
    store(ram, 2, -1000);
    const auto first = newest.step(ram, {}, registers);
    assert(first.filtered.left == 500 && first.filtered.right == -500);

    registers.fir = {};
    registers.fir[0] = 64;
    DspEchoFir oldest;
    for (std::uint16_t frame = 0; frame < 8; ++frame) {
        const auto address = static_cast<std::uint16_t>(frame * 4U);
        store(ram, address, static_cast<std::int16_t>(1000 + frame * 100));
        store(ram, static_cast<std::uint16_t>(address + 2U), 0);
        const auto step = oldest.step(ram, {}, registers);
        if (frame < 7) assert(step.filtered.left == 0);
        else assert(step.filtered.left == 500);
    }
}

void test_signed_echo_volume_wrap_and_output_saturation() {
    DspEchoRam ram{};
    DspEchoRegisters registers{};
    registers.delay = 1;
    registers.write_disable = true;
    registers.fir[7] = 64;
    registers.left_volume = -128;
    registers.right_volume = 64;
    store(ram, 0, 20000);
    store(ram, 2, 20000);
    DspEchoStepInput input{};
    input.direct = {1000, -1000};
    DspEchoFir echo;
    const auto signed_volume = echo.step(ram, input, registers);
    assert(signed_volume.filtered.left == 10000);
    assert(signed_volume.output.left == -9000 && signed_volume.output.right == 4000);

    echo.reset();
    registers.left_volume = 127;
    input.direct.left = 30000;
    const auto saturated = echo.step(ram, input, registers);
    assert(saturated.output.left == 32767);

    echo.reset();
    registers.fir[7] = -128;
    registers.left_volume = -128;
    input.direct.left = 0;
    store(ram, 0, -32768);
    const auto wrapped_volume = echo.step(ram, input, registers);
    assert(wrapped_volume.filtered.left == -32768);
    assert(wrapped_volume.output.left == -32768); // +32768 wraps before mixing.
}

void test_feedback_saturates_rounds_even_and_write_disable_preserves_ram() {
    DspEchoRam ram{};
    DspEchoRegisters registers{};
    registers.delay = 1;
    registers.fir[7] = 64;
    registers.feedback = 64;
    store(ram, 0, 20000);
    store(ram, 2, -20000);
    DspEchoStepInput input{};
    input.echo_send = {30001, -30001};
    DspEchoFir echo;
    const auto written = echo.step(ram, input, registers);
    assert(written.echo_write.left == 32766 && written.echo_write.right == -32768);
    assert(written.wrote_ram);
    assert(load(ram, 0) == 32766 && load(ram, 2) == -32768);

    echo.reset();
    registers.write_disable = true;
    store(ram, 0, 20000);
    store(ram, 2, -20000);
    const auto disabled = echo.step(ram, input, registers);
    assert(disabled.echo_write.left == 32766 && disabled.echo_write.right == -32768);
    assert(!disabled.wrote_ram);
    assert(load(ram, 0) == 20000 && load(ram, 2) == -20000);
}

void test_negative_flooring_and_fir_intermediate_wrap() {
    DspEchoRam ram{};
    DspEchoRegisters registers{};
    registers.delay = 1;
    registers.write_disable = true;
    registers.fir[7] = 32;
    store(ram, 0, -3);
    DspEchoFir rounding;
    const auto rounded = rounding.step(ram, {}, registers);
    assert(rounded.filtered.left == -2); // Arithmetic shifts floor; output is even.

    registers.fir.fill(127);
    registers.fir[7] = -128;
    DspEchoFir overflow;
    DspEchoStepResult result{};
    for (std::uint16_t frame = 0; frame < 8; ++frame) {
        store(ram, static_cast<std::uint16_t>(frame * 4U), 32000);
        result = overflow.step(ram, {}, registers);
    }
    assert(result.filtered.left == -6358);
}

} // namespace

int main() {
    test_echo_address_wraps_in_apuram_and_at_edl_length();
    test_fir7_is_newest_and_fir0_is_oldest();
    test_signed_echo_volume_wrap_and_output_saturation();
    test_feedback_saturates_rounds_even_and_write_disable_preserves_ram();
    test_negative_flooring_and_fir_intermediate_wrap();
}
