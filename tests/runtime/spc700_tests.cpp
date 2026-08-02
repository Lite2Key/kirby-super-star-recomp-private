#include "kss/spc700.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#ifdef _WIN32
#include <crtdbg.h>
#endif
#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace {

using kss::apu::Spc700Core;
using kss::apu::SpcStepStatus;

std::array<std::uint8_t, Spc700Core::kIplSize> synthetic_ipl(std::uint16_t vector = 0x0200) {
    std::array<std::uint8_t, Spc700Core::kIplSize> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(index ^ 0x5aU);
    }
    bytes[62] = static_cast<std::uint8_t>(vector);
    bytes[63] = static_cast<std::uint8_t>(vector >> 8U);
    return bytes;
}

Spc700Core core_at(std::uint16_t pc = 0x0200) {
    Spc700Core core;
    const auto ipl = synthetic_ipl(pc);
    assert(core.load_ipl(ipl));
    assert(core.reset());
    return core;
}

void put(Spc700Core& core, std::uint16_t address,
    std::initializer_list<std::uint8_t> bytes) {
    for (const auto value : bytes) core.ram()[address++] = value;
}

void test_runtime_provisioned_ipl_and_overlay() {
    Spc700Core core;
    std::array<std::uint8_t, 63> short_ipl{};
    assert(!core.load_ipl(short_ipl));
    assert(!core.reset());
    assert(core.step().status == SpcStepStatus::missing_ipl);

    const auto ipl = synthetic_ipl(0xffc0);
    assert(core.load_ipl(ipl));
    assert(core.reset() && core.registers().pc == 0xffc0);
    assert(core.registers().sp == 0xef && core.registers().ps == 0x02);
    core.ram()[0xffc0] = 0xa5;
    assert(core.read8(0xffc0) == ipl[0]);
    core.write8(0xffc0, 0x3c);
    assert(core.read8(0xffc0) == ipl[0]);
    core.write8(0x00f1, 0x00); // Disable overlay without embedding Nintendo IPL bytes.
    assert(!core.ipl_enabled() && core.read8(0xffc0) == 0x3c);
}

void test_ipl_bootstrap_required_before_trace_entry() {
    auto core = core_at();
    put(core, 0x0200, {0xcd, 0xef, 0xbd, 0xe8, 0x00, 0xc6, 0x1d});
    assert(core.step().status == SpcStepStatus::executed); // MOV X,#imm
    assert(core.registers().x == 0xef && core.registers().cycles == 2);
    assert(core.step().status == SpcStepStatus::executed); // MOV SP,X
    assert(core.registers().sp == 0xef && core.registers().cycles == 4);
    assert(core.step().status == SpcStepStatus::executed); // MOV A,#imm
    assert(core.registers().a == 0 && (core.registers().ps & 0x02) != 0);
    assert(core.step().status == SpcStepStatus::executed); // MOV (X),A
    assert(core.ram()[0x00ef] == 0 && core.registers().cycles == 10);
    assert(core.step().status == SpcStepStatus::executed); // DEC X
    assert(core.registers().x == 0xee && core.registers().cycles == 12);
}

void test_ports_control_and_direct_page_selection() {
    auto core = core_at();
    core.cpu_write_port(0, 0x12);
    core.cpu_write_port(1, 0x34);
    assert(core.read8(0x00f4) == 0x12 && core.read8(0x00f5) == 0x34);
    core.write8(0x00f4, 0x56);
    assert(core.cpu_read_port(0) == 0x56);
    core.write8(0x00f1, 0x90); // Clear input 0/1, retain IPL overlay.
    assert(core.read8(0x00f4) == 0 && core.read8(0x00f5) == 0 && core.ipl_enabled());

    core.registers().ps = 0x20;
    core.registers().a = 0x7a;
    put(core, 0x0200, {0xc4, 0x10}); // MOV $10,A in direct page one.
    const auto result = core.step();
    assert(result.status == SpcStepStatus::executed && result.instruction_cycles == 4);
    assert(core.ram()[0x0110] == 0x7a && core.ram()[0x0010] == 0);
}

void test_observed_load_store_transfer_and_word_instructions() {
    auto core = core_at();
    core.ram()[0x0010] = 0x34;
    core.ram()[0x0011] = 0x12;
    core.registers().y = 0x80;
    put(core, 0x0200, {
        0xe4,0x10,       // MOV A,dp
        0x5d,            // MOV X,A
        0xcb,0x12,       // MOV dp,Y
        0xeb,0x12,       // MOV Y,dp
        0xfc,            // INC Y
        0xba,0x10,       // MOVW YA,dp
        0xda,0x20,       // MOVW dp,YA
        0x8f,0x99,0x22,  // MOV dp,#imm
    });
    assert(core.step().instruction_cycles == 3 && core.registers().a == 0x34);
    assert(core.step().instruction_cycles == 2 && core.registers().x == 0x34);
    assert(core.step().instruction_cycles == 4 && core.ram()[0x12] == 0x80);
    assert(core.step().instruction_cycles == 3 && core.registers().y == 0x80);
    assert(core.step().instruction_cycles == 2 && core.registers().y == 0x81);
    assert(core.step().instruction_cycles == 5);
    assert(core.registers().a == 0x34 && core.registers().y == 0x12);
    assert(core.step().instruction_cycles == 5);
    assert(core.ram()[0x20] == 0x34 && core.ram()[0x21] == 0x12);
    assert(core.step().instruction_cycles == 5 && core.ram()[0x22] == 0x99);
}

void test_observed_comparisons_branches_and_indirect_store() {
    auto core = core_at();
    core.ram()[0x10] = 5;
    core.ram()[0x11] = 7;
    core.ram()[0x20] = 0x00;
    core.ram()[0x21] = 0x30;
    core.registers().y = 7;
    core.registers().a = 0x66;
    put(core, 0x0200, {
        0x78,0x05,0x10, // CMP dp,#imm: equal
        0x7e,0x11,      // CMP Y,dp: equal
        0xd0,0x02,      // BNE not taken
        0xdd,           // MOV A,Y
        0x2f,0x01,      // BRA skips DEC X
        0x1d,
        0xd7,0x20,      // MOV [dp]+Y,A
    });
    assert(core.step().instruction_cycles == 5 && (core.registers().ps & 0x03) == 0x03);
    assert(core.step().instruction_cycles == 3 && (core.registers().ps & 0x02) != 0);
    assert(core.step().instruction_cycles == 2 && core.registers().pc == 0x0207);
    assert(core.step().instruction_cycles == 2 && core.registers().a == 7);
    assert(core.step().instruction_cycles == 4 && core.registers().pc == 0x020b);
    assert(core.step().instruction_cycles == 7 && core.ram()[0x3007] == 7);

    auto taken = core_at();
    taken.registers().ps = 0;
    put(taken, 0x0200, {0xd0,0xfe});
    assert(taken.step().instruction_cycles == 4 && taken.registers().pc == 0x0200);
}

void test_fail_closed_opcode_and_io_boundaries() {
    auto io = core_at();
    io.registers().a = 0x00;
    put(io, 0x0200, {0xc5,0xf0,0x00}); // Unsupported TEST RAM write-protect mode.
    assert(io.step().status == SpcStepStatus::unsupported_io && io.stopped());
    assert(io.registers().cycles == 0);

    auto ignored = core_at(); ignored.registers().ps |= 0x20U; ignored.registers().a = 0;
    put(ignored, 0x0200, {0xc5,0xf0,0x00});
    assert(ignored.step().status == SpcStepStatus::executed); // TEST ignored while P=1.
}

void test_dsp_auxiliary_and_control_io() {
    auto core = core_at();
    put(core, 0x0200, {
        0x8f,0x0c,0xf2, 0x8f,0x7f,0xf3, 0xe4,0xf3,
        0x8f,0x55,0xf8, 0xe4,0xf8,
    });
    assert(core.step().instruction_cycles == 5 && core.dsp_address() == 0x0c);
    assert(core.step().instruction_cycles == 5 && core.dsp_registers()[0x0c] == 0x7f);
    assert(core.step().instruction_cycles == 3 && core.registers().a == 0x7f);
    assert(core.step().instruction_cycles == 5 && core.auxiliary_io()[0] == 0x55);
    assert(core.step().instruction_cycles == 3 && core.registers().a == 0x55);

    core.write8(0x00f2, 0x8c);
    assert(core.read8(0x00f3) == 0x7f);
    core.write8(0x00f3, 0x22); // Bit-7 DSP addresses are read-only mirrors.
    assert(core.dsp_registers()[0x0c] == 0x7f);

    core.cpu_write_port(0, 1); core.cpu_write_port(1, 2);
    core.cpu_write_port(2, 3); core.cpu_write_port(3, 4);
    core.write8(0x00f1, 0x30);
    assert(core.read8(0x00f4) == 0 && core.read8(0x00f5) == 0);
    assert(core.read8(0x00f6) == 0 && core.read8(0x00f7) == 0);
}

void test_instruction_cycle_driven_timers() {
    auto timer2 = core_at();
    timer2.write8(0x00fc, 1);
    timer2.write8(0x00f1, 0x04);
    put(timer2, 0x0200, {0,0,0,0,0,0,0,0,0});
    for (unsigned step = 0; step < 7; ++step) assert(timer2.step().instruction_cycles == 2);
    assert(timer2.timers()[2].divider == 14 && timer2.read8(0x00ff) == 0);
    assert(timer2.step().instruction_cycles == 2);
    assert(timer2.timers()[2].divider == 0 && timer2.read8(0x00ff) == 1);
    assert(timer2.read8(0x00ff) == 0);

    auto timer0 = core_at();
    timer0.write8(0x00fa, 1); timer0.write8(0x00f1, 0x01);
    for (std::uint16_t address = 0x0200; address < 0x0240; ++address) timer0.ram()[address] = 0;
    for (unsigned step = 0; step < 64; ++step) assert(timer0.step().instruction_cycles == 2);
    assert(timer0.registers().cycles == 128 && timer0.read8(0x00fd) == 1);

    auto zero_target = core_at();
    zero_target.write8(0x00fc, 0); zero_target.write8(0x00f1, 0x04);
    zero_target.ram()[0x0200] = 0;
    for (unsigned step = 0; step < 2048; ++step) {
        zero_target.registers().pc = 0x0200;
        assert(zero_target.step().instruction_cycles == 2);
    }
    assert(zero_target.read8(0x00ff) == 1);

    auto gated = core_at();
    gated.write8(0x00fc, 1); gated.write8(0x00f1, 0x04);
    gated.write8(0x00f0, 0x0b); gated.ram()[0x0200] = 0;
    for (unsigned step = 0; step < 8; ++step) {
        gated.registers().pc = 0x0200; assert(gated.step().instruction_cycles == 2);
    }
    assert(gated.read8(0x00ff) == 0);
    gated.write8(0x00f0, 0x0a);
    for (unsigned step = 0; step < 8; ++step) {
        gated.registers().pc = 0x0200; assert(gated.step().instruction_cycles == 2);
    }
    assert(gated.read8(0x00ff) == 1);
}

void test_complete_dispatch_classification() {
    std::size_t executed = 0;
    std::size_t sleeping = 0;
    std::size_t stopped = 0;
    for (unsigned opcode = 0; opcode <= std::numeric_limits<std::uint8_t>::max(); ++opcode) {
        auto core = core_at();
        put(core, 0x0200, {static_cast<std::uint8_t>(opcode),0,0,0});
        const auto result = core.step();
        if (opcode == 0xef) {
            assert(result.status == SpcStepStatus::sleeping);
            ++sleeping;
        } else if (opcode == 0xff) {
            assert(result.status == SpcStepStatus::stopped);
            ++stopped;
        } else {
            assert(result.status == SpcStepStatus::executed);
            ++executed;
        }
        assert(result.status != SpcStepStatus::unsupported_opcode);
    }
    assert(executed == 254 && sleeping == 1 && stopped == 1);
}

void test_bit_carry_flags_and_explicit_halts() {
    struct Vector { std::uint8_t opcode; std::uint8_t cycles; };
    constexpr Vector vectors[]{
        {0x0a,5},{0x2a,5},{0x4a,4},{0x6a,4},{0x8a,5},{0xaa,4},
        {0xca,6},{0xea,5},{0x0e,6},{0x4e,6},{0x20,2},{0x40,2},
    };
    for (const auto vector : vectors) {
        auto core = core_at(); put(core, 0x0200, {vector.opcode,0,0});
        const auto result = core.step();
        assert(result.status == SpcStepStatus::executed
            && result.instruction_cycles == vector.cycles);
    }

    // Encoded address $0123, bit 5. Exercise carry load, inversion, and store.
    auto bits = core_at(); bits.ram()[0x0123] = 0x20; bits.registers().ps = 0;
    put(bits, 0x0200, {
        0xaa,0x23,0xa1, // MOV1 C,$0123.5
        0x8a,0x23,0xa1, // EOR1 C,$0123.5
        0xca,0x23,0xa1, // MOV1 $0123.5,C
        0xea,0x23,0xa1, // NOT1 $0123.5
    });
    assert(bits.step().instruction_cycles == 4 && (bits.registers().ps & 1U) != 0);
    assert(bits.step().instruction_cycles == 5 && (bits.registers().ps & 1U) == 0);
    assert(bits.step().instruction_cycles == 6 && bits.ram()[0x0123] == 0);
    assert(bits.step().instruction_cycles == 5 && bits.ram()[0x0123] == 0x20);

    auto test_bits = core_at(); test_bits.registers().a = 0x0f; test_bits.ram()[0x1234] = 0x03;
    put(test_bits, 0x0200, {0x0e,0x34,0x12,0x4e,0x34,0x12,0x20,0x40});
    assert(test_bits.step().instruction_cycles == 6 && test_bits.ram()[0x1234] == 0x0f);
    assert(test_bits.step().instruction_cycles == 6 && test_bits.ram()[0x1234] == 0);
    assert(test_bits.step().instruction_cycles == 2 && (test_bits.registers().ps & 0x20U) == 0);
    assert(test_bits.step().instruction_cycles == 2 && (test_bits.registers().ps & 0x20U) != 0);

    auto sleep = core_at(); put(sleep, 0x0200, {0xef});
    const auto sleep_result = sleep.step();
    assert(sleep_result.status == SpcStepStatus::sleeping
        && sleep_result.instruction_cycles == 3 && sleep.registers().cycles == 3);
    assert(sleep.sleeping() && !sleep.stopped());
    assert(sleep.step().status == SpcStepStatus::sleeping && sleep.registers().cycles == 3);

    auto stop = core_at(); put(stop, 0x0200, {0xff});
    const auto stop_result = stop.step();
    assert(stop_result.status == SpcStepStatus::stopped
        && stop_result.instruction_cycles == 3 && stop.registers().cycles == 3);
    assert(stop.stopped() && !stop.sleeping());
    assert(stop.step().status == SpcStepStatus::stopped && stop.registers().cycles == 3);
}

std::uint8_t expected_alu(std::uint8_t family, std::uint8_t lhs,
    std::uint8_t rhs, bool carry, std::uint8_t& flags) {
    auto set_nz = [&](std::uint8_t value) {
        flags = static_cast<std::uint8_t>(flags & ~0x82U);
        if (value == 0) flags |= 0x02;
        if ((value & 0x80U) != 0) flags |= 0x80;
    };
    if (family == 0x00) { lhs = static_cast<std::uint8_t>(lhs | rhs); set_nz(lhs); return lhs; }
    if (family == 0x20) { lhs = static_cast<std::uint8_t>(lhs & rhs); set_nz(lhs); return lhs; }
    if (family == 0x40) { lhs = static_cast<std::uint8_t>(lhs ^ rhs); set_nz(lhs); return lhs; }
    if (family == 0x60) {
        const auto result = static_cast<std::uint8_t>(lhs - rhs);
        flags = static_cast<std::uint8_t>(flags & ~0x83U);
        if (lhs >= rhs) flags |= 0x01;
        if (result == 0) flags |= 0x02;
        if ((result & 0x80U) != 0) flags |= 0x80;
        return lhs;
    }
    const auto operand = family == 0xa0 ? static_cast<std::uint8_t>(~rhs) : rhs;
    const auto wide = static_cast<unsigned>(lhs) + operand + (carry ? 1U : 0U);
    const auto result = static_cast<std::uint8_t>(wide);
    flags = static_cast<std::uint8_t>(flags & ~0xcbU);
    if (wide > 0xffU) flags |= 0x01;
    if (result == 0) flags |= 0x02;
    if (((lhs ^ operand ^ result) & 0x10U) != 0) flags |= 0x08;
    if (((~(lhs ^ operand) & (lhs ^ result)) & 0x80U) != 0) flags |= 0x40;
    if ((result & 0x80U) != 0) flags |= 0x80;
    return result;
}

void test_exhaustive_immediate_alu_truth_tables() {
    auto core = core_at();
    constexpr std::array<std::uint8_t, 6> families{0x00,0x20,0x40,0x60,0x80,0xa0};
    for (const auto family : families) {
        for (unsigned lhs = 0; lhs <= std::numeric_limits<std::uint8_t>::max(); ++lhs) {
            for (unsigned rhs = 0; rhs <= std::numeric_limits<std::uint8_t>::max(); ++rhs) {
                const auto carry_cases = (family == 0x80 || family == 0xa0) ? 2U : 1U;
                for (unsigned carry = 0; carry < carry_cases; ++carry) {
                    core.registers().pc = 0x0200;
                    core.registers().a = static_cast<std::uint8_t>(lhs);
                    core.registers().ps = static_cast<std::uint8_t>(0x34U | carry);
                    put(core, 0x0200, {static_cast<std::uint8_t>(family | 0x08U),
                        static_cast<std::uint8_t>(rhs)});
                    auto expected_flags = core.registers().ps;
                    const auto expected = expected_alu(family, static_cast<std::uint8_t>(lhs),
                        static_cast<std::uint8_t>(rhs), carry != 0, expected_flags);
                    const auto result = core.step();
                    assert(result.status == SpcStepStatus::executed && result.instruction_cycles == 2);
                    assert(core.registers().a == expected && core.registers().ps == expected_flags);
                }
            }
        }
    }
}

void test_every_alu_addressing_opcode() {
    constexpr std::array<std::uint8_t, 6> families{0x00,0x20,0x40,0x60,0x80,0xa0};
    constexpr std::array<std::uint8_t, 12> modes{
        0x04,0x05,0x06,0x07,0x08,0x09,0x14,0x15,0x16,0x17,0x18,0x19};
    constexpr std::array<std::uint8_t, 12> cycles{3,4,3,6,2,6,4,5,5,6,5,5};
    for (const auto family : families) for (std::size_t index = 0; index < modes.size(); ++index) {
        auto core = core_at();
        core.registers().a = 0x35; core.registers().x = 0x10; core.registers().y = 0x20;
        core.registers().ps = 0x01;
        const auto opcode = static_cast<std::uint8_t>(family | modes[index]);
        switch (modes[index]) {
        case 0x04: core.ram()[0x30]=0x12; put(core,0x0200,{opcode,0x30}); break;
        case 0x05: core.ram()[0x1234]=0x12; put(core,0x0200,{opcode,0x34,0x12}); break;
        case 0x06: core.ram()[0x10]=0x12; put(core,0x0200,{opcode}); break;
        case 0x07: core.ram()[0x40]=0x34; core.ram()[0x41]=0x12; core.ram()[0x1234]=0x12; put(core,0x0200,{opcode,0x30}); break;
        case 0x08: put(core,0x0200,{opcode,0x12}); break;
        case 0x09: core.ram()[0x30]=0x12; core.ram()[0x31]=0x35; put(core,0x0200,{opcode,0x30,0x31}); break;
        case 0x14: core.ram()[0x40]=0x12; put(core,0x0200,{opcode,0x30}); break;
        case 0x15: core.ram()[0x1244]=0x12; put(core,0x0200,{opcode,0x34,0x12}); break;
        case 0x16: core.ram()[0x1254]=0x12; put(core,0x0200,{opcode,0x34,0x12}); break;
        case 0x17: core.ram()[0x30]=0x34; core.ram()[0x31]=0x12; core.ram()[0x1254]=0x12; put(core,0x0200,{opcode,0x30}); break;
        case 0x18: core.ram()[0x31]=0x35; put(core,0x0200,{opcode,0x12,0x31}); break;
        case 0x19: core.ram()[0x10]=0x35; core.ram()[0x20]=0x12; put(core,0x0200,{opcode}); break;
        default: assert(false);
        }
        const auto before_target = modes[index] == 0x09 || modes[index] == 0x18 ? core.ram()[0x31] : core.ram()[0x10];
        const auto result = core.step();
        assert(result.status == SpcStepStatus::executed && result.instruction_cycles == cycles[index]);
        if (family == 0x60 && (modes[index] == 0x09 || modes[index] == 0x18 || modes[index] == 0x19)) {
            const auto after_target = modes[index] == 0x09 || modes[index] == 0x18 ? core.ram()[0x31] : core.ram()[0x10];
            assert(after_target == before_target);
        }
    }
}

void test_stack_calls_returns_and_vectors() {
    auto call = core_at(); call.registers().sp = 0x00;
    put(call,0x0200,{0x3f,0x34,0x12}); put(call,0x1234,{0x6f});
    assert(call.step().instruction_cycles == 8 && call.registers().pc == 0x1234);
    assert(call.registers().sp == 0xfe && call.ram()[0x0100] == 0x02 && call.ram()[0x01ff] == 0x03);
    assert(call.step().instruction_cycles == 5 && call.registers().pc == 0x0203 && call.registers().sp == 0x00);

    auto stack = core_at(); stack.registers().sp = 0xff; stack.registers().a=1; stack.registers().x=2; stack.registers().y=3;
    put(stack,0x0200,{0x2d,0x4d,0x6d,0xae,0xce,0xee});
    for (int i=0;i<6;++i) assert(stack.step().instruction_cycles == 4);
    assert(stack.registers().a == 3 && stack.registers().x == 2 && stack.registers().y == 1 && stack.registers().sp == 0xff);

    for (unsigned vector=0; vector<16; ++vector) {
        auto tcall = core_at(); tcall.write8(0x00f1,0x00);
        const auto table = static_cast<std::uint16_t>(0xffdeU-vector*2U);
        tcall.ram()[table]=static_cast<std::uint8_t>(0x40U+vector); tcall.ram()[table+1]=0x12;
        put(tcall,0x0200,{static_cast<std::uint8_t>(0x01U+(vector<<4U))});
        assert(tcall.step().instruction_cycles == 8);
        assert(tcall.registers().pc == static_cast<std::uint16_t>(0x1240U+vector));
    }

    auto brk = core_at(); brk.write8(0x00f1,0x00); brk.ram()[0xffde]=0x00; brk.ram()[0xffdf]=0x30;
    brk.registers().ps=0xff; put(brk,0x0200,{0x0f}); put(brk,0x3000,{0x7f});
    assert(brk.step().instruction_cycles == 8 && brk.registers().pc == 0x3000);
    assert((brk.registers().ps & 0x14U) == 0x10U);
    assert(brk.step().instruction_cycles == 6 && brk.registers().pc == 0x0201 && brk.registers().ps == 0xff);

    auto pcall = core_at(); put(pcall,0x0200,{0x4f,0x80}); put(pcall,0xff80,{0x6f});
    assert(pcall.step().instruction_cycles == 6 && pcall.registers().pc == 0xff80);
    assert(pcall.step().instruction_cycles == 5 && pcall.registers().pc == 0x0202);

    auto jumps = core_at(); jumps.registers().x=1;
    put(jumps,0x0200,{0x1f,0xff,0x2f}); jumps.ram()[0x3000]=0x34; jumps.ram()[0x3001]=0x12;
    put(jumps,0x1234,{0x5f,0x78,0x56});
    assert(jumps.step().instruction_cycles == 6 && jumps.registers().pc == 0x1234);
    assert(jumps.step().instruction_cycles == 3 && jumps.registers().pc == 0x5678);
}

void test_direct_page_pointer_wrap_and_flag_controls() {
    auto pointer = core_at(); pointer.registers().ps=0x20; pointer.registers().a=0x10;
    pointer.ram()[0x01ff]=0x34; pointer.ram()[0x0100]=0x12; pointer.ram()[0x1234]=0x01;
    put(pointer,0x0200,{0x07,0xff});
    assert(pointer.step().instruction_cycles == 6 && pointer.registers().a == 0x11);

    auto flags = core_at(); flags.registers().ps=0x48;
    put(flags,0x0200,{0x80,0xed,0x60,0xa0,0xc0,0xe0,0x00});
    assert(flags.step().instruction_cycles == 2 && (flags.registers().ps & 1U) != 0);
    assert(flags.step().instruction_cycles == 3 && (flags.registers().ps & 1U) == 0);
    assert(flags.step().instruction_cycles == 2 && (flags.registers().ps & 1U) == 0);
    assert(flags.step().instruction_cycles == 3 && (flags.registers().ps & 4U) != 0);
    assert(flags.step().instruction_cycles == 3 && (flags.registers().ps & 4U) == 0);
    assert(flags.step().instruction_cycles == 2 && (flags.registers().ps & 0x48U) == 0);
    assert(flags.step().instruction_cycles == 2);
}

void test_word_multiply_divide_and_decimal_boundaries() {
    auto word = core_at(); word.registers().a=0xff; word.registers().y=0x7f;
    word.ram()[0x20]=1; word.ram()[0x21]=0; put(word,0x0200,{0x7a,0x20,0x5a,0x20,0x9a,0x20});
    assert(word.step().instruction_cycles == 5 && word.registers().a == 0 && word.registers().y == 0x80);
    assert(word.step().instruction_cycles == 4 && (word.registers().ps & 0x81U) == 0x01U);
    assert(word.step().instruction_cycles == 5 && word.registers().a == 0xff && word.registers().y == 0x7f);

    auto mul = core_at(); mul.registers().a=0xff; mul.registers().y=0xff; put(mul,0x0200,{0xcf});
    assert(mul.step().instruction_cycles == 9 && mul.registers().a == 1 && mul.registers().y == 0xfe);

    auto div = core_at(); div.registers().a=0x34; div.registers().y=0x12; div.registers().x=0; put(div,0x0200,{0x9e});
    assert(div.step().instruction_cycles == 12); // Defined S-SMP overflow path; never host-divides by zero.

    auto decimal = core_at(); decimal.registers().a=0x9a; decimal.registers().ps=0; put(decimal,0x0200,{0xdf,0xbe,0x9f});
    assert(decimal.step().instruction_cycles == 3 && decimal.registers().a == 0x00 && (decimal.registers().ps & 3U) == 3U);
    decimal.registers().ps=0; assert(decimal.step().instruction_cycles == 3 && decimal.registers().a == 0x9a);
    assert(decimal.step().instruction_cycles == 5 && decimal.registers().a == 0xa9);
}

void test_expanded_family_inventory_and_exact_cycles() {
    struct Vector { std::uint8_t opcode; std::uint8_t cycles; };
    constexpr Vector moves[]{
        Vector{0xe5,4}, {0xe6,3}, {0xe7,6}, {0xf4,4}, {0xf5,5}, {0xf6,5}, {0xf7,6},
        {0xbf,4}, {0xe9,4}, {0xf8,3}, {0xf9,4}, {0x9d,2}, {0x7d,2}, {0x8d,2},
        {0xec,4}, {0xfb,4}, {0xfd,2}, {0xc5,5}, {0xc7,7}, {0xd4,5}, {0xd5,6},
        {0xd6,6}, {0xaf,4}, {0xc9,5}, {0xd8,4}, {0xd9,5}, {0xcc,5}, {0xdb,5},
        {0xfa,5},
    };
    for (const auto vector : moves) {
        auto core = core_at(); put(core, 0x0200, {vector.opcode,0,0,0});
        const auto result = core.step();
        assert(result.status == SpcStepStatus::executed
            && result.instruction_cycles == vector.cycles);
    }

    constexpr Vector shifts[]{
        Vector{0x0b,4},{0x0c,5},{0x1b,5},{0x1c,2},
        {0x2b,4},{0x2c,5},{0x3b,5},{0x3c,2},
        {0x4b,4},{0x4c,5},{0x5b,5},{0x5c,2},
        {0x6b,4},{0x6c,5},{0x7b,5},{0x7c,2},
    };
    for (const auto vector : shifts) {
        auto core = core_at(); put(core, 0x0200, {vector.opcode,0,0});
        assert(core.step().instruction_cycles == vector.cycles);
    }

    constexpr Vector inc_dec_compare[]{
        Vector{0x1a,6},{0x3a,6},{0x8b,4},{0x8c,5},{0x9b,5},{0x9c,2},
        {0xab,4},{0xac,5},{0xbb,5},{0xbc,2},{0x3d,2},{0xdc,2},
        {0xc8,2},{0x3e,3},{0x1e,4},{0xad,2},{0x5e,4},
    };
    for (const auto vector : inc_dec_compare) {
        auto core = core_at(); put(core, 0x0200, {vector.opcode,0,0});
        assert(core.step().instruction_cycles == vector.cycles);
    }

    constexpr Vector conditional_branches[]{
        Vector{0x10,4},{0x30,2},{0x50,4},{0x70,2},
        {0x90,4},{0xb0,2},{0xf0,4},{0x2e,5},{0xde,6},{0x6e,7},{0xfe,6},
    };
    for (const auto vector : conditional_branches) {
        auto core = core_at(); put(core, 0x0200, {vector.opcode,0,0});
        const auto result = core.step();
        if (result.instruction_cycles != vector.cycles) {
            std::fprintf(stderr, "SPC branch %02X cycles=%u expected=%u\n",
                static_cast<unsigned>(vector.opcode),
                static_cast<unsigned>(result.instruction_cycles),
                static_cast<unsigned>(vector.cycles));
        }
        assert(result.instruction_cycles == vector.cycles);
    }

    for (unsigned bit = 0; bit < 8; ++bit) {
        for (const bool clear : {false, true}) {
            const auto opcode = static_cast<std::uint8_t>(0x02U + bit * 0x20U
                + (clear ? 0x10U : 0U));
            auto core = core_at(); put(core, 0x0200, {opcode,0});
            assert(core.step().instruction_cycles == 4);
        }
        for (const bool branch_on_clear : {false, true}) {
            const auto opcode = static_cast<std::uint8_t>(0x03U + bit * 0x20U
                + (branch_on_clear ? 0x10U : 0U));
            auto core = core_at(); put(core, 0x0200, {opcode,0,0});
            const auto result = core.step();
            assert(result.instruction_cycles == (branch_on_clear ? 7 : 5));
        }
    }
}

void test_expanded_family_semantics() {
    auto bit = core_at(); bit.ram()[0x20] = 0;
    put(bit, 0x0200, {0x42,0x20, 0x43,0x20,0x00, 0x52,0x20});
    assert(bit.step().instruction_cycles == 4 && bit.ram()[0x20] == 0x04);
    assert(bit.step().instruction_cycles == 7 && bit.registers().pc == 0x0205);
    assert(bit.step().instruction_cycles == 4 && bit.ram()[0x20] == 0);

    auto shifts = core_at(); shifts.registers().a = 0x81; shifts.registers().ps = 0;
    put(shifts, 0x0200, {0x1c,0x7c,0x3c,0x5c});
    assert(shifts.step().instruction_cycles == 2 && shifts.registers().a == 0x02
        && (shifts.registers().ps & 1U) != 0);
    assert(shifts.step().instruction_cycles == 2 && shifts.registers().a == 0x81);
    assert(shifts.step().instruction_cycles == 2 && shifts.registers().a == 0x02);
    assert(shifts.step().instruction_cycles == 2 && shifts.registers().a == 0x01
        && (shifts.registers().ps & 1U) == 0);

    auto move = core_at(); move.registers().a = 0xa5; move.registers().x = 0x10;
    put(move, 0x0200, {0xaf,0xbf,0x7d,0xfa,0x10,0x11});
    assert(move.step().instruction_cycles == 4 && move.ram()[0x10] == 0xa5
        && move.registers().x == 0x11);
    assert(move.step().instruction_cycles == 4 && move.registers().a == 0
        && move.registers().x == 0x12);
    assert(move.step().instruction_cycles == 2 && move.registers().a == 0x12);
    move.ram()[0x10] = 0x66;
    assert(move.step().instruction_cycles == 5 && move.ram()[0x11] == 0x66);
}

} // namespace

int main() {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    test_runtime_provisioned_ipl_and_overlay();
    test_ipl_bootstrap_required_before_trace_entry();
    test_ports_control_and_direct_page_selection();
    test_observed_load_store_transfer_and_word_instructions();
    test_observed_comparisons_branches_and_indirect_store();
    test_exhaustive_immediate_alu_truth_tables();
    test_every_alu_addressing_opcode();
    test_stack_calls_returns_and_vectors();
    test_direct_page_pointer_wrap_and_flag_controls();
    test_word_multiply_divide_and_decimal_boundaries();
    test_expanded_family_inventory_and_exact_cycles();
    test_expanded_family_semantics();
    test_bit_carry_flags_and_explicit_halts();
    test_complete_dispatch_classification();
    test_dsp_auxiliary_and_control_io();
    test_instruction_cycle_driven_timers();
    test_fail_closed_opcode_and_io_boundaries();
}
