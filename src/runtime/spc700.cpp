// Instruction shapes and flag rules are adapted from the ares SPC700 core:
// Copyright (c) 2004-2025 ares team, Near et al; ISC license.
// This file is a focused dependency-free rewrite for the observed KSS slice.
#include "kss/spc700.hpp"

#include <algorithm>

namespace kss::apu {

bool Spc700Core::load_ipl(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() != kIplSize) return false;
    std::copy(bytes.begin(), bytes.end(), ipl_.begin());
    ipl_loaded_ = true;
    return true;
}

bool Spc700Core::reset() noexcept {
    registers_ = {};
    cpu_to_spc_.fill(0);
    spc_to_cpu_.fill(0);
    dsp_registers_.fill(0);
    dsp_core_.reset();
    auxiliary_io_.fill(0);
    timers_ = {};
    dsp_address_ = 0;
    timers_disabled_ = false;
    timers_enabled_ = true;
    ipl_enabled_ = true;
    stopped_ = false;
    sleeping_ = false;
    io_fault_ = false;
    if (!ipl_loaded_) {
        stopped_ = true;
        return false;
    }
    registers_.sp = 0xef;
    registers_.ps = 0x02;
    registers_.pc = static_cast<std::uint16_t>(
        ipl_[62] | (static_cast<std::uint16_t>(ipl_[63]) << 8U));
    return true;
}

std::uint8_t Spc700Core::read8(std::uint16_t address) noexcept {
    if (address >= 0xffc0U && ipl_enabled_) return ipl_[address & 0x003fU];
    if (address >= 0x00f0U && address <= 0x00ffU) {
        switch (address) {
        case 0x00f0: case 0x00f1: return 0; // TEST/CONTROL are write-only.
        case 0x00f2: return dsp_address_;
        case 0x00f3: return dsp_registers_[dsp_address_ & 0x7fU];
        case 0x00f4: case 0x00f5: case 0x00f6: case 0x00f7:
            return cpu_to_spc_[address - 0x00f4U];
        case 0x00f8: case 0x00f9: return auxiliary_io_[address - 0x00f8U];
        case 0x00fa: case 0x00fb: case 0x00fc: return 0; // Timer targets are write-only.
        case 0x00fd: case 0x00fe: case 0x00ff: {
            auto& output = timers_[address - 0x00fdU].output;
            const auto value = output;
            output = 0;
            return value;
        }
        default: break;
        }
    }
    return ram_[address];
}

void Spc700Core::write8(std::uint16_t address, std::uint8_t value) noexcept {
    // SPC writes always reach underlying APURAM, including under the IPL overlay.
    ram_[address] = value;
    switch (address) {
    case 0x00f0: write_test(value); return;
    case 0x00f1: write_control(value); return;
    case 0x00f2: dsp_address_ = value; return;
    case 0x00f3:
        // $80-$FF select read-only mirrors of the 128 DSP registers.
        if ((dsp_address_ & 0x80U) == 0) {
            (void)dsp_core_.write_register(dsp_registers_, dsp_address_, value);
        }
        return;
    case 0x00f4: case 0x00f5: case 0x00f6: case 0x00f7:
        spc_to_cpu_[address - 0x00f4U] = value;
        if (port_write_sink_) {
            port_write_sink_(port_write_context_, registers_.cycles,
                static_cast<std::uint8_t>(address - 0x00f4U), value);
        }
        return;
    case 0x00f8: case 0x00f9: auxiliary_io_[address - 0x00f8U] = value; return;
    case 0x00fa: case 0x00fb: case 0x00fc:
        timers_[address - 0x00faU].target = value; return;
    case 0x00fd: case 0x00fe: case 0x00ff:
        return; // Timer outputs are read-only; writes only reach underlying APURAM.
    default: return;
    }
}

dsp::DspClockResult Spc700Core::clock_dsp_sample() noexcept {
    return dsp_core_.clock_sample(ram_, dsp_registers_);
}

void Spc700Core::write_test(std::uint8_t value) noexcept {
    // TEST writes are ignored while the direct-page flag is set. RAM disable,
    // RAM write-protect, and non-default wait states are outside this runtime.
    if ((registers_.ps & kDirectPage) != 0) return;
    if ((value & 0xf0U) != 0 || (value & 0x02U) == 0 || (value & 0x04U) != 0) {
        io_fault_ = true;
        return;
    }
    timers_disabled_ = (value & 0x01U) != 0;
    timers_enabled_ = (value & 0x08U) != 0;
}

void Spc700Core::write_control(std::uint8_t value) noexcept {
    for (std::size_t index = 0; index < timers_.size(); ++index) {
        const auto enable = (value & (1U << index)) != 0;
        if (enable && !timers_[index].enabled) {
            timers_[index].stage2 = 0;
            timers_[index].output = 0;
        }
        timers_[index].enabled = enable;
    }
    if ((value & 0x10U) != 0) cpu_to_spc_[0] = cpu_to_spc_[1] = 0;
    if ((value & 0x20U) != 0) cpu_to_spc_[2] = cpu_to_spc_[3] = 0;
    ipl_enabled_ = (value & 0x80U) != 0;
}

void Spc700Core::cpu_write_port(std::uint8_t port, std::uint8_t value) noexcept {
    if (port < cpu_to_spc_.size()) cpu_to_spc_[port] = value;
}

std::uint8_t Spc700Core::cpu_read_port(std::uint8_t port) const noexcept {
    return port < spc_to_cpu_.size() ? spc_to_cpu_[port] : 0;
}

std::uint8_t Spc700Core::fetch8() noexcept {
    const auto value = read8(registers_.pc);
    ++registers_.pc;
    return value;
}

std::uint16_t Spc700Core::direct_address(std::uint8_t offset) const noexcept {
    return static_cast<std::uint16_t>(((registers_.ps & kDirectPage) != 0 ? 0x0100U : 0U) | offset);
}

std::uint8_t Spc700Core::read_direct(std::uint8_t offset) noexcept {
    return read8(direct_address(offset));
}

std::uint16_t Spc700Core::read_direct16(std::uint8_t offset) noexcept {
    const auto low = read_direct(offset);
    const auto high = read_direct(static_cast<std::uint8_t>(offset + 1U));
    return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
}

void Spc700Core::write_direct(std::uint8_t offset, std::uint8_t value) noexcept {
    write8(direct_address(offset), value);
}

void Spc700Core::push(std::uint8_t value) noexcept {
    write8(static_cast<std::uint16_t>(0x0100U | registers_.sp), value);
    --registers_.sp;
}

std::uint8_t Spc700Core::pull() noexcept {
    ++registers_.sp;
    return read8(static_cast<std::uint16_t>(0x0100U | registers_.sp));
}

std::uint8_t Spc700Core::alu(
    std::uint8_t family, std::uint8_t lhs, std::uint8_t rhs) noexcept {
    switch (family) {
    case 0x00: lhs = static_cast<std::uint8_t>(lhs | rhs); set_nz8(lhs); return lhs;
    case 0x20: lhs = static_cast<std::uint8_t>(lhs & rhs); set_nz8(lhs); return lhs;
    case 0x40: lhs = static_cast<std::uint8_t>(lhs ^ rhs); set_nz8(lhs); return lhs;
    case 0x60: compare8(lhs, rhs); return lhs;
    case 0x80:
    case 0xa0: {
        const auto operand = family == 0xa0 ? static_cast<std::uint8_t>(~rhs) : rhs;
        const auto carry = (registers_.ps & kCarry) != 0 ? 1U : 0U;
        const auto result = static_cast<unsigned>(lhs) + operand + carry;
        const auto byte = static_cast<std::uint8_t>(result);
        registers_.ps = static_cast<std::uint8_t>(registers_.ps &
            ~(kCarry | kZero | kHalfCarry | kOverflow | kNegative));
        if (result > 0xffU) registers_.ps |= kCarry;
        if (byte == 0) registers_.ps |= kZero;
        if (((lhs ^ operand ^ byte) & 0x10U) != 0) registers_.ps |= kHalfCarry;
        if (((~(lhs ^ operand) & (lhs ^ byte)) & 0x80U) != 0) registers_.ps |= kOverflow;
        if ((byte & 0x80U) != 0) registers_.ps |= kNegative;
        return byte;
    }
    default: return lhs;
    }
}

SpcStepResult Spc700Core::step_alu_family(std::uint8_t opcode) noexcept {
    const auto family = static_cast<std::uint8_t>(opcode & 0xe0U);
    const auto mode = static_cast<std::uint8_t>(opcode & 0x1fU);
    (void)fetch8();
    auto apply_a = [&](std::uint8_t rhs, std::uint8_t bytes, std::uint8_t cycles) {
        registers_.a = alu(family, registers_.a, rhs);
        return finish(opcode, bytes, cycles);
    };
    switch (mode) {
    case 0x04: return apply_a(read_direct(fetch8()), 2, 3);
    case 0x05: {
        const auto low = fetch8(); const auto high = fetch8();
        return apply_a(read8(static_cast<std::uint16_t>(low | (high << 8U))), 3, 4);
    }
    case 0x06: return apply_a(read_direct(registers_.x), 1, 3);
    case 0x07: {
        const auto pointer = static_cast<std::uint8_t>(fetch8() + registers_.x);
        return apply_a(read8(read_direct16(pointer)), 2, 6);
    }
    case 0x08: return apply_a(fetch8(), 2, 2);
    case 0x09: {
        const auto source = fetch8(); const auto rhs = read_direct(source);
        const auto target = fetch8(); const auto lhs = read_direct(target);
        const auto result = alu(family, lhs, rhs);
        if (family != 0x60) write_direct(target, result);
        return finish(opcode, 3, 6);
    }
    case 0x14: {
        const auto address = static_cast<std::uint8_t>(fetch8() + registers_.x);
        return apply_a(read_direct(address), 2, 4);
    }
    case 0x15: case 0x16: {
        const auto low = fetch8(); const auto high = fetch8();
        const auto index = mode == 0x15 ? registers_.x : registers_.y;
        const auto address = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(low | (high << 8U)) + index);
        return apply_a(read8(address), 3, 5);
    }
    case 0x17: {
        const auto pointer = fetch8();
        return apply_a(read8(static_cast<std::uint16_t>(read_direct16(pointer) + registers_.y)), 2, 6);
    }
    case 0x18: {
        const auto immediate = fetch8(); const auto target = fetch8();
        const auto result = alu(family, read_direct(target), immediate);
        if (family != 0x60) write_direct(target, result);
        return finish(opcode, 3, 5);
    }
    case 0x19: {
        const auto rhs = read_direct(registers_.y); const auto lhs = read_direct(registers_.x);
        const auto result = alu(family, lhs, rhs);
        if (family != 0x60) write_direct(registers_.x, result);
        return finish(opcode, 1, 5);
    }
    default: break;
    }
    stopped_ = true;
    return {SpcStepStatus::unsupported_opcode, opcode, 0, 0};
}

void Spc700Core::set_nz8(std::uint8_t value) noexcept {
    registers_.ps = static_cast<std::uint8_t>(registers_.ps & ~(kNegative | kZero));
    if (value == 0) registers_.ps |= kZero;
    if ((value & 0x80U) != 0) registers_.ps |= kNegative;
}

void Spc700Core::set_nz16(std::uint16_t value) noexcept {
    registers_.ps = static_cast<std::uint8_t>(registers_.ps & ~(kNegative | kZero));
    if (value == 0) registers_.ps |= kZero;
    if ((value & 0x8000U) != 0) registers_.ps |= kNegative;
}

void Spc700Core::compare8(std::uint8_t lhs, std::uint8_t rhs) noexcept {
    const auto result = static_cast<std::uint8_t>(lhs - rhs);
    registers_.ps = static_cast<std::uint8_t>(registers_.ps & ~(kCarry | kNegative | kZero));
    if (lhs >= rhs) registers_.ps |= kCarry;
    if (result == 0) registers_.ps |= kZero;
    if ((result & 0x80U) != 0) registers_.ps |= kNegative;
}

void Spc700Core::advance_timers(std::uint8_t cycles) noexcept {
    constexpr std::array<std::uint32_t, 3> periods{128, 128, 16};
    for (std::size_t index = 0; index < timers_.size(); ++index) {
        auto& timer = timers_[index];
        timer.divider += cycles;
        while (timer.divider >= periods[index]) {
            timer.divider -= periods[index];
            if (!timers_enabled_ || timers_disabled_ || !timer.enabled) continue;
            ++timer.stage2;
            if (timer.stage2 != timer.target) continue;
            timer.stage2 = 0;
            timer.output = static_cast<std::uint8_t>((timer.output + 1U) & 0x0fU);
        }
    }
}

SpcStepResult Spc700Core::finish(
    std::uint8_t opcode, std::uint8_t bytes, std::uint8_t cycles) noexcept {
    if (io_fault_) {
        stopped_ = true;
        return {SpcStepStatus::unsupported_io, opcode, bytes, 0};
    }
    advance_timers(cycles);
    registers_.cycles += cycles;
    return {SpcStepStatus::executed, opcode, bytes, cycles};
}

SpcStepResult Spc700Core::step() noexcept {
    if (!ipl_loaded_) return {SpcStepStatus::missing_ipl, 0, 0, 0};
    if (sleeping_) return {SpcStepStatus::sleeping, 0, 0, 0};
    if (stopped_) return {SpcStepStatus::stopped, 0, 0, 0};
    io_fault_ = false;
    const auto opcode = read8(registers_.pc);
    const auto family = static_cast<std::uint8_t>(opcode & 0xe0U);
    const auto mode = static_cast<std::uint8_t>(opcode & 0x1fU);
    const bool alu_family = (family == 0x00 || family == 0x20 || family == 0x40 ||
        family == 0x60 || family == 0x80 || family == 0xa0) &&
        (mode == 0x04 || mode == 0x05 || mode == 0x06 || mode == 0x07 ||
         mode == 0x08 || mode == 0x09 || mode == 0x14 || mode == 0x15 ||
         mode == 0x16 || mode == 0x17 || mode == 0x18 || mode == 0x19);
    if (alu_family) return step_alu_family(opcode);
    switch (opcode) {
    case 0x0a: case 0x0e: case 0x20: case 0x2a: case 0x40: case 0x4a: case 0x4e:
    case 0x6a: case 0x8a: case 0xaa: case 0xca: case 0xea: case 0xef: case 0xff:
    case 0x02: case 0x03: case 0x0b: case 0x0c: case 0x10: case 0x12: case 0x13:
    case 0x1a: case 0x1b: case 0x1c: case 0x1e: case 0x22: case 0x23: case 0x2b: case 0x2c:
    case 0x2e: case 0x30: case 0x32: case 0x33: case 0x3a: case 0x3b: case 0x3c:
    case 0x3d: case 0x3e: case 0x42: case 0x43: case 0x4b: case 0x4c: case 0x50: case 0x52:
    case 0x53: case 0x5b: case 0x5c: case 0x5e: case 0x62: case 0x63: case 0x6b:
    case 0x6c: case 0x6e: case 0x70: case 0x72: case 0x73: case 0x7b: case 0x7c: case 0x7d:
    case 0x82: case 0x83: case 0x8b: case 0x8c: case 0x8d: case 0x90: case 0x92:
    case 0x93: case 0x9b: case 0x9c: case 0x9d: case 0xa2: case 0xa3: case 0xab: case 0xad:
    case 0xac: case 0xb0: case 0xb2: case 0xb3: case 0xbb: case 0xbc: case 0xbf:
    case 0xc2: case 0xc3: case 0xc5: case 0xc7: case 0xc8: case 0xc9: case 0xcc:
    case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: case 0xd8: case 0xd9:
    case 0xdb: case 0xdc: case 0xde: case 0xe2: case 0xe3: case 0xe5: case 0xe6:
    case 0xe7: case 0xe9: case 0xec: case 0xf0: case 0xf2: case 0xf3: case 0xf4:
    case 0xf5: case 0xf6: case 0xf7: case 0xf8: case 0xf9: case 0xfa: case 0xfb:
    case 0xfd: case 0xfe: case 0xaf:
    case 0x1d: case 0x2f: case 0x5d: case 0x78: case 0x7e: case 0x8f:
    case 0xba: case 0xc4: case 0xc6: case 0xcb: case 0xd0: case 0xd7:
    case 0xda: case 0xdd: case 0xe4: case 0xeb: case 0xfc:
    case 0xcd: case 0xbd: case 0xe8:
    case 0x00: case 0x0d: case 0x0f: case 0x1f: case 0x2d: case 0x3f:
    case 0x4d: case 0x4f: case 0x5a: case 0x5f: case 0x60: case 0x6d:
    case 0x6f: case 0x7a: case 0x7f: case 0x80: case 0x8e: case 0x9a:
    case 0x9e: case 0x9f: case 0xa0: case 0xae: case 0xbe: case 0xc0:
    case 0xce: case 0xcf: case 0xdf: case 0xe0: case 0xed: case 0xee:
    case 0x01: case 0x11: case 0x21: case 0x31: case 0x41: case 0x51:
    case 0x61: case 0x71: case 0x81: case 0x91: case 0xa1: case 0xb1:
    case 0xc1: case 0xd1: case 0xe1: case 0xf1:
        break;
    default:
        stopped_ = true;
        return {SpcStepStatus::unsupported_opcode, opcode, 0, 0};
    }
    (void)fetch8();

    // SET1/CLR1 dp.bit and BBS/BBC dp.bit,rel are regular opcode grids.
    if ((opcode & 0x0fU) == 0x02U) {
        const auto address = fetch8();
        const auto mask = static_cast<std::uint8_t>(1U << ((opcode >> 5U) & 7U));
        const auto before = read_direct(address);
        const auto after = (opcode & 0x10U) == 0
            ? static_cast<std::uint8_t>(before | mask)
            : static_cast<std::uint8_t>(before & static_cast<std::uint8_t>(~mask));
        write_direct(address, after);
        return finish(opcode, 2, 4);
    }
    if ((opcode & 0x0fU) == 0x03U) {
        const auto address = fetch8();
        const auto displacement = static_cast<std::int8_t>(fetch8());
        const auto mask = static_cast<std::uint8_t>(1U << ((opcode >> 5U) & 7U));
        const auto bit_set = (read_direct(address) & mask) != 0;
        const auto take = (opcode & 0x10U) == 0 ? bit_set : !bit_set;
        if (take) registers_.pc = static_cast<std::uint16_t>(registers_.pc + displacement);
        return finish(opcode, 3, static_cast<std::uint8_t>(take ? 7 : 5));
    }

    auto relative_branch = [&](bool take, std::uint8_t not_taken,
                               std::uint8_t taken) noexcept {
        const auto displacement = static_cast<std::int8_t>(fetch8());
        if (take) registers_.pc = static_cast<std::uint16_t>(registers_.pc + displacement);
        return finish(opcode, 2, take ? taken : not_taken);
    };
    auto shift = [&](std::uint8_t value, unsigned operation) noexcept {
        const auto old_carry = (registers_.ps & kCarry) != 0;
        bool new_carry = false;
        switch (operation) {
        case 0: new_carry = (value & 0x80U) != 0; value = static_cast<std::uint8_t>(value << 1U); break;
        case 1: new_carry = (value & 0x80U) != 0; value = static_cast<std::uint8_t>(
            (value << 1U) | (old_carry ? 1U : 0U)); break;
        case 2: new_carry = (value & 0x01U) != 0; value = static_cast<std::uint8_t>(value >> 1U); break;
        default: new_carry = (value & 0x01U) != 0; value = static_cast<std::uint8_t>((value >> 1U) | (old_carry ? 0x80U : 0U)); break;
        }
        registers_.ps = static_cast<std::uint8_t>(registers_.ps & ~kCarry);
        if (new_carry) registers_.ps |= kCarry;
        set_nz8(value);
        return value;
    };
    auto fetch_bit_address = [&](std::uint8_t& mask) noexcept {
        const auto low = fetch8();
        const auto high = fetch8();
        const auto encoded = static_cast<std::uint16_t>(low | (high << 8U));
        mask = static_cast<std::uint8_t>(1U << (encoded >> 13U));
        return static_cast<std::uint16_t>(encoded & 0x1fffU);
    };

    switch (opcode) {
    case 0x01: case 0x11: case 0x21: case 0x31: case 0x41: case 0x51:
    case 0x61: case 0x71: case 0x81: case 0x91: case 0xa1: case 0xb1:
    case 0xc1: case 0xd1: case 0xe1: case 0xf1: { // TCALL n
        push(static_cast<std::uint8_t>(registers_.pc >> 8U));
        push(static_cast<std::uint8_t>(registers_.pc));
        const auto vector = static_cast<std::uint16_t>((opcode >> 4U) & 0x0fU);
        const auto address = static_cast<std::uint16_t>(0xffdeU - vector * 2U);
        registers_.pc = static_cast<std::uint16_t>(read8(address) |
            (read8(static_cast<std::uint16_t>(address + 1U)) << 8U));
        return finish(opcode, 1, 8);
    }
    case 0x00: return finish(opcode, 1, 2); // NOP
    case 0x20: registers_.ps &= static_cast<std::uint8_t>(~kDirectPage); return finish(opcode, 1, 2); // CLRP
    case 0x40: registers_.ps |= kDirectPage; return finish(opcode, 1, 2); // SETP
    case 0x0a: case 0x2a: case 0x4a: case 0x6a:
    case 0x8a: case 0xaa: { // OR1/AND1/EOR1/MOV1 C,mem.bit
        std::uint8_t mask = 0;
        const auto address = fetch_bit_address(mask);
        auto bit = (read8(address) & mask) != 0;
        if (opcode == 0x2a || opcode == 0x6a) bit = !bit;
        const auto carry = (registers_.ps & kCarry) != 0;
        bool result = bit;
        if (opcode == 0x0a || opcode == 0x2a) result = carry || bit;
        else if (opcode == 0x4a || opcode == 0x6a) result = carry && bit;
        else if (opcode == 0x8a) result = carry != bit;
        registers_.ps = static_cast<std::uint8_t>(registers_.ps & ~kCarry);
        if (result) registers_.ps |= kCarry;
        const auto cycles = static_cast<std::uint8_t>(
            opcode == 0x4a || opcode == 0x6a || opcode == 0xaa ? 4 : 5);
        return finish(opcode, 3, cycles);
    }
    case 0xca: case 0xea: { // MOV1 mem.bit,C / NOT1 mem.bit
        std::uint8_t mask = 0;
        const auto address = fetch_bit_address(mask);
        const auto before = read8(address);
        std::uint8_t after = 0;
        if (opcode == 0xea) {
            after = static_cast<std::uint8_t>(before ^ mask);
        } else if ((registers_.ps & kCarry) != 0) {
            after = static_cast<std::uint8_t>(before | mask);
        } else {
            after = static_cast<std::uint8_t>(before & static_cast<std::uint8_t>(~mask));
        }
        write8(address, after);
        return finish(opcode, 3, static_cast<std::uint8_t>(opcode == 0xca ? 6 : 5));
    }
    case 0x0e: case 0x4e: { // TSET1/TCLR1 abs
        const auto low = fetch8(); const auto high = fetch8();
        const auto address = static_cast<std::uint16_t>(low | (high << 8U));
        const auto before = read8(address);
        const auto difference = static_cast<std::uint8_t>(registers_.a - before);
        set_nz8(difference);
        const auto after = opcode == 0x0e
            ? static_cast<std::uint8_t>(before | registers_.a)
            : static_cast<std::uint8_t>(before & static_cast<std::uint8_t>(~registers_.a));
        write8(address, after);
        return finish(opcode, 3, 6);
    }
    case 0xef: // SLEEP
        sleeping_ = true;
        advance_timers(3);
        registers_.cycles += 3;
        return {SpcStepStatus::sleeping, opcode, 1, 3};
    case 0xff: // STOP
        stopped_ = true;
        advance_timers(3);
        registers_.cycles += 3;
        return {SpcStepStatus::stopped, opcode, 1, 3};
    case 0x10: return relative_branch((registers_.ps & kNegative) == 0, 2, 4); // BPL
    case 0x30: return relative_branch((registers_.ps & kNegative) != 0, 2, 4); // BMI
    case 0x50: return relative_branch((registers_.ps & kOverflow) == 0, 2, 4); // BVC
    case 0x70: return relative_branch((registers_.ps & kOverflow) != 0, 2, 4); // BVS
    case 0x90: return relative_branch((registers_.ps & kCarry) == 0, 2, 4); // BCC
    case 0xb0: return relative_branch((registers_.ps & kCarry) != 0, 2, 4); // BCS
    case 0xf0: return relative_branch((registers_.ps & kZero) != 0, 2, 4); // BEQ
    case 0x2e: { // CBNE dp,rel
        const auto value = read_direct(fetch8());
        const auto displacement = static_cast<std::int8_t>(fetch8());
        const auto take = registers_.a != value;
        if (take) registers_.pc = static_cast<std::uint16_t>(registers_.pc + displacement);
        return finish(opcode, 3, static_cast<std::uint8_t>(take ? 7 : 5));
    }
    case 0xde: { // CBNE dp+X,rel
        const auto address = static_cast<std::uint8_t>(fetch8() + registers_.x);
        const auto value = read_direct(address);
        const auto displacement = static_cast<std::int8_t>(fetch8());
        const auto take = registers_.a != value;
        if (take) registers_.pc = static_cast<std::uint16_t>(registers_.pc + displacement);
        return finish(opcode, 3, static_cast<std::uint8_t>(take ? 8 : 6));
    }
    case 0x6e: { // DBNZ dp,rel
        const auto address = fetch8();
        auto value = static_cast<std::uint8_t>(read_direct(address) - 1U);
        write_direct(address, value);
        const auto displacement = static_cast<std::int8_t>(fetch8());
        if (value != 0) registers_.pc = static_cast<std::uint16_t>(registers_.pc + displacement);
        return finish(opcode, 3, static_cast<std::uint8_t>(value != 0 ? 7 : 5));
    }
    case 0xfe: { // DBNZ Y,rel
        --registers_.y;
        return relative_branch(registers_.y != 0, 4, 6);
    }
    case 0x0d: push(registers_.ps); return finish(opcode, 1, 4); // PUSH PSW
    case 0x2d: push(registers_.a); return finish(opcode, 1, 4);  // PUSH A
    case 0x4d: push(registers_.x); return finish(opcode, 1, 4);  // PUSH X
    case 0x6d: push(registers_.y); return finish(opcode, 1, 4);  // PUSH Y
    case 0x8e: registers_.ps = pull(); return finish(opcode, 1, 4); // POP PSW
    case 0xae: registers_.a = pull(); return finish(opcode, 1, 4);  // POP A
    case 0xce: registers_.x = pull(); return finish(opcode, 1, 4);  // POP X
    case 0xee: registers_.y = pull(); return finish(opcode, 1, 4);  // POP Y
    case 0x0f: { // BRK
        push(static_cast<std::uint8_t>(registers_.pc >> 8U));
        push(static_cast<std::uint8_t>(registers_.pc));
        push(registers_.ps);
        registers_.pc = static_cast<std::uint16_t>(read8(0xffde) | (read8(0xffdf) << 8U));
        registers_.ps = static_cast<std::uint8_t>((registers_.ps & ~kInterrupt) | kBreak);
        return finish(opcode, 1, 8);
    }
    case 0x3f: { // CALL abs
        const auto low = fetch8(); const auto high = fetch8();
        push(static_cast<std::uint8_t>(registers_.pc >> 8U));
        push(static_cast<std::uint8_t>(registers_.pc));
        registers_.pc = static_cast<std::uint16_t>(low | (high << 8U));
        return finish(opcode, 3, 8);
    }
    case 0x4f: { // PCALL page
        const auto target = fetch8();
        push(static_cast<std::uint8_t>(registers_.pc >> 8U));
        push(static_cast<std::uint8_t>(registers_.pc));
        registers_.pc = static_cast<std::uint16_t>(0xff00U | target);
        return finish(opcode, 2, 6);
    }
    case 0x6f: { // RET
        const auto low = pull(); const auto high = pull();
        registers_.pc = static_cast<std::uint16_t>(low | (high << 8U));
        return finish(opcode, 1, 5);
    }
    case 0x7f: { // RETI
        registers_.ps = pull(); const auto low = pull(); const auto high = pull();
        registers_.pc = static_cast<std::uint16_t>(low | (high << 8U));
        return finish(opcode, 1, 6);
    }
    case 0x1f: { // JMP [abs+X]
        const auto low = fetch8(); const auto high = fetch8();
        const auto pointer = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(low | (high << 8U)) + registers_.x);
        registers_.pc = static_cast<std::uint16_t>(read8(pointer) |
            (read8(static_cast<std::uint16_t>(pointer + 1U)) << 8U));
        return finish(opcode, 3, 6);
    }
    case 0x5f: { // JMP abs
        const auto low = fetch8(); const auto high = fetch8();
        registers_.pc = static_cast<std::uint16_t>(low | (high << 8U));
        return finish(opcode, 3, 3);
    }
    case 0x60: registers_.ps &= static_cast<std::uint8_t>(~kCarry); return finish(opcode, 1, 2);
    case 0x80: registers_.ps |= kCarry; return finish(opcode, 1, 2);
    case 0xa0: registers_.ps |= kInterrupt; return finish(opcode, 1, 3);
    case 0xc0: registers_.ps &= static_cast<std::uint8_t>(~kInterrupt); return finish(opcode, 1, 3);
    case 0xe0: registers_.ps &= static_cast<std::uint8_t>(~(kHalfCarry | kOverflow)); return finish(opcode, 1, 2);
    case 0xed: registers_.ps ^= kCarry; return finish(opcode, 1, 3);
    case 0x5a: { // CMPW YA,dp
        const auto rhs = read_direct16(fetch8());
        const auto lhs = static_cast<std::uint16_t>(registers_.a | (registers_.y << 8U));
        const auto result = static_cast<std::uint16_t>(lhs - rhs);
        registers_.ps = static_cast<std::uint8_t>(registers_.ps & ~(kCarry | kZero | kNegative));
        if (lhs >= rhs) registers_.ps |= kCarry;
        if (result == 0) registers_.ps |= kZero;
        if ((result & 0x8000U) != 0) registers_.ps |= kNegative;
        return finish(opcode, 2, 4);
    }
    case 0x7a: case 0x9a: { // ADDW/SUBW YA,dp
        const auto rhs = read_direct16(fetch8());
        const auto family16 = opcode == 0x7a ? 0x80U : 0xa0U;
        if (opcode == 0x9a) registers_.ps |= kCarry;
        else registers_.ps &= static_cast<std::uint8_t>(~kCarry);
        registers_.a = alu(static_cast<std::uint8_t>(family16), registers_.a,
            static_cast<std::uint8_t>(rhs));
        registers_.y = alu(static_cast<std::uint8_t>(family16), registers_.y,
            static_cast<std::uint8_t>(rhs >> 8U));
        const auto result = static_cast<std::uint16_t>(registers_.a | (registers_.y << 8U));
        registers_.ps &= static_cast<std::uint8_t>(~kZero);
        if (result == 0) registers_.ps |= kZero;
        return finish(opcode, 2, 5);
    }
    case 0x9e: { // DIV YA,X
        const auto ya = static_cast<std::uint16_t>(registers_.a | (registers_.y << 8U));
        const auto x = static_cast<unsigned>(registers_.x);
        const auto y = static_cast<unsigned>(registers_.y);
        registers_.ps = static_cast<std::uint8_t>(registers_.ps & ~(kHalfCarry | kOverflow));
        if ((y & 15U) >= (x & 15U)) registers_.ps |= kHalfCarry;
        if (y >= x) registers_.ps |= kOverflow;
        if (y < x * 2U) {
            registers_.a = static_cast<std::uint8_t>(ya / x);
            registers_.y = static_cast<std::uint8_t>(ya % x);
        } else {
            const auto delta = static_cast<int>(ya) - static_cast<int>(x * 512U);
            registers_.a = static_cast<std::uint8_t>(255 - delta / static_cast<int>(256U - x));
            registers_.y = static_cast<std::uint8_t>(x + delta % static_cast<int>(256U - x));
        }
        set_nz8(registers_.a); return finish(opcode, 1, 12);
    }
    case 0x9f:
        registers_.a = static_cast<std::uint8_t>((registers_.a >> 4U) | (registers_.a << 4U));
        set_nz8(registers_.a); return finish(opcode, 1, 5);
    case 0xcf: { // MUL YA
        const auto product = static_cast<std::uint16_t>(registers_.y * registers_.a);
        registers_.a = static_cast<std::uint8_t>(product);
        registers_.y = static_cast<std::uint8_t>(product >> 8U);
        set_nz8(registers_.y); return finish(opcode, 1, 9);
    }
    case 0xdf: // DAA
        if ((registers_.ps & kCarry) != 0 || registers_.a > 0x99U) {
            registers_.a = static_cast<std::uint8_t>(registers_.a + 0x60U); registers_.ps |= kCarry;
        }
        if ((registers_.ps & kHalfCarry) != 0 || (registers_.a & 0x0fU) > 9U)
            registers_.a = static_cast<std::uint8_t>(registers_.a + 6U);
        set_nz8(registers_.a); return finish(opcode, 1, 3);
    case 0xbe: // DAS
        if ((registers_.ps & kCarry) == 0 || registers_.a > 0x99U) {
            registers_.a = static_cast<std::uint8_t>(registers_.a - 0x60U); registers_.ps &= static_cast<std::uint8_t>(~kCarry);
        }
        if ((registers_.ps & kHalfCarry) == 0 || (registers_.a & 0x0fU) > 9U)
            registers_.a = static_cast<std::uint8_t>(registers_.a - 6U);
        set_nz8(registers_.a); return finish(opcode, 1, 3);

    // ASL/ROL/LSR/ROR: dp, abs, dp+X, and accumulator forms.
    case 0x0b: case 0x2b: case 0x4b: case 0x6b: {
        const auto address = fetch8();
        const auto operation = static_cast<unsigned>(opcode >> 5U);
        const auto value = shift(read_direct(address), operation);
        write_direct(address, value);
        return finish(opcode, 2, 4);
    }
    case 0x0c: case 0x2c: case 0x4c: case 0x6c: {
        const auto low = fetch8(); const auto high = fetch8();
        const auto address = static_cast<std::uint16_t>(low | (high << 8U));
        const auto operation = static_cast<unsigned>(opcode >> 5U);
        const auto value = shift(read8(address), operation);
        write8(address, value);
        return finish(opcode, 3, 5);
    }
    case 0x1b: case 0x3b: case 0x5b: case 0x7b: {
        const auto address = static_cast<std::uint8_t>(fetch8() + registers_.x);
        const auto operation = static_cast<unsigned>((opcode - 0x10U) >> 5U);
        const auto value = shift(read_direct(address), operation);
        write_direct(address, value);
        return finish(opcode, 2, 5);
    }
    case 0x1c: case 0x3c: case 0x5c: case 0x7c: {
        const auto operation = static_cast<unsigned>((opcode - 0x10U) >> 5U);
        registers_.a = shift(registers_.a, operation);
        return finish(opcode, 1, 2);
    }

    case 0x1a: case 0x3a: { // DECW/INCW dp
        const auto address = fetch8();
        auto value = read_direct16(address);
        value = opcode == 0x1a ? static_cast<std::uint16_t>(value - 1U)
                               : static_cast<std::uint16_t>(value + 1U);
        write_direct(address, static_cast<std::uint8_t>(value));
        write_direct(static_cast<std::uint8_t>(address + 1U),
            static_cast<std::uint8_t>(value >> 8U));
        set_nz16(value);
        return finish(opcode, 2, 6);
    }
    case 0x8b: case 0xab: { // DEC/INC dp
        const auto address = fetch8();
        auto value = read_direct(address);
        value = opcode == 0x8b ? static_cast<std::uint8_t>(value - 1U)
                               : static_cast<std::uint8_t>(value + 1U);
        write_direct(address, value); set_nz8(value);
        return finish(opcode, 2, 4);
    }
    case 0x8c: case 0xac: { // DEC/INC abs
        const auto low = fetch8(); const auto high = fetch8();
        const auto address = static_cast<std::uint16_t>(low | (high << 8U));
        auto value = read8(address);
        value = opcode == 0x8c ? static_cast<std::uint8_t>(value - 1U)
                               : static_cast<std::uint8_t>(value + 1U);
        write8(address, value); set_nz8(value);
        return finish(opcode, 3, 5);
    }
    case 0x9b: case 0xbb: { // DEC/INC dp+X
        const auto address = static_cast<std::uint8_t>(fetch8() + registers_.x);
        auto value = read_direct(address);
        value = opcode == 0x9b ? static_cast<std::uint8_t>(value - 1U)
                               : static_cast<std::uint8_t>(value + 1U);
        write_direct(address, value); set_nz8(value);
        return finish(opcode, 2, 5);
    }
    case 0x9c: --registers_.a; set_nz8(registers_.a); return finish(opcode, 1, 2); // DEC A
    case 0xbc: ++registers_.a; set_nz8(registers_.a); return finish(opcode, 1, 2); // INC A
    case 0x3d: ++registers_.x; set_nz8(registers_.x); return finish(opcode, 1, 2); // INC X
    case 0xdc: --registers_.y; set_nz8(registers_.y); return finish(opcode, 1, 2); // DEC Y

    case 0xc8: compare8(registers_.x, fetch8()); return finish(opcode, 2, 2); // CMP X,#imm
    case 0x3e: compare8(registers_.x, read_direct(fetch8())); return finish(opcode, 2, 3); // CMP X,dp
    case 0x1e: { // CMP X,abs
        const auto low = fetch8(); const auto high = fetch8();
        compare8(registers_.x, read8(static_cast<std::uint16_t>(low | (high << 8U))));
        return finish(opcode, 3, 4);
    }
    case 0xad: compare8(registers_.y, fetch8()); return finish(opcode, 2, 2); // CMP Y,#imm
    case 0x5e: { // CMP Y,abs
        const auto low = fetch8(); const auto high = fetch8();
        compare8(registers_.y, read8(static_cast<std::uint16_t>(low | (high << 8U))));
        return finish(opcode, 3, 4);
    }

    case 0xe5: { // MOV A,abs
        const auto low = fetch8(); const auto high = fetch8();
        registers_.a = read8(static_cast<std::uint16_t>(low | (high << 8U)));
        set_nz8(registers_.a); return finish(opcode, 3, 4);
    }
    case 0xe6: registers_.a = read_direct(registers_.x); set_nz8(registers_.a); return finish(opcode, 1, 3);
    case 0xe7: {
        const auto pointer = static_cast<std::uint8_t>(fetch8() + registers_.x);
        registers_.a = read8(read_direct16(pointer)); set_nz8(registers_.a);
        return finish(opcode, 2, 6);
    }
    case 0xf4: registers_.a = read_direct(static_cast<std::uint8_t>(fetch8() + registers_.x)); set_nz8(registers_.a); return finish(opcode, 2, 4);
    case 0xf5: case 0xf6: {
        const auto low = fetch8(); const auto high = fetch8();
        const auto index = opcode == 0xf5 ? registers_.x : registers_.y;
        registers_.a = read8(static_cast<std::uint16_t>(low | (high << 8U)) + index);
        set_nz8(registers_.a); return finish(opcode, 3, 5);
    }
    case 0xf7: {
        const auto pointer = fetch8();
        registers_.a = read8(static_cast<std::uint16_t>(read_direct16(pointer) + registers_.y));
        set_nz8(registers_.a); return finish(opcode, 2, 6);
    }
    case 0xbf: registers_.a = read_direct(registers_.x++); set_nz8(registers_.a); return finish(opcode, 1, 4);
    case 0xe9: {
        const auto low = fetch8(); const auto high = fetch8();
        registers_.x = read8(static_cast<std::uint16_t>(low | (high << 8U)));
        set_nz8(registers_.x); return finish(opcode, 3, 4);
    }
    case 0xf8: registers_.x = read_direct(fetch8()); set_nz8(registers_.x); return finish(opcode, 2, 3);
    case 0xf9: registers_.x = read_direct(static_cast<std::uint8_t>(fetch8() + registers_.y)); set_nz8(registers_.x); return finish(opcode, 2, 4);
    case 0x9d: registers_.x = registers_.sp; set_nz8(registers_.x); return finish(opcode, 1, 2);
    case 0x7d: registers_.a = registers_.x; set_nz8(registers_.a); return finish(opcode, 1, 2);
    case 0x8d: registers_.y = fetch8(); set_nz8(registers_.y); return finish(opcode, 2, 2);
    case 0xec: {
        const auto low = fetch8(); const auto high = fetch8();
        registers_.y = read8(static_cast<std::uint16_t>(low | (high << 8U)));
        set_nz8(registers_.y); return finish(opcode, 3, 4);
    }
    case 0xfb: registers_.y = read_direct(static_cast<std::uint8_t>(fetch8() + registers_.x)); set_nz8(registers_.y); return finish(opcode, 2, 4);
    case 0xfd: registers_.y = registers_.a; set_nz8(registers_.y); return finish(opcode, 1, 2);

    case 0xc5: case 0xc9: case 0xcc: { // MOV abs,A/X/Y
        const auto low = fetch8(); const auto high = fetch8();
        const auto address = static_cast<std::uint16_t>(low | (high << 8U));
        const auto value = opcode == 0xc5 ? registers_.a : opcode == 0xc9 ? registers_.x : registers_.y;
        (void)read8(address); write8(address, value); return finish(opcode, 3, 5);
    }
    case 0xc7: { // MOV [dp+X],A
        const auto pointer = static_cast<std::uint8_t>(fetch8() + registers_.x);
        const auto address = read_direct16(pointer);
        (void)read8(address); write8(address, registers_.a); return finish(opcode, 2, 7);
    }
    case 0xd4: { const auto address = static_cast<std::uint8_t>(fetch8() + registers_.x); (void)read_direct(address); write_direct(address, registers_.a); return finish(opcode, 2, 5); }
    case 0xd5: case 0xd6: {
        const auto low = fetch8(); const auto high = fetch8();
        const auto index = opcode == 0xd5 ? registers_.x : registers_.y;
        const auto address = static_cast<std::uint16_t>(static_cast<std::uint16_t>(low | (high << 8U)) + index);
        (void)read8(address); write8(address, registers_.a); return finish(opcode, 3, 6);
    }
    case 0xaf: (void)read_direct(registers_.x); write_direct(registers_.x++, registers_.a); return finish(opcode, 1, 4);
    case 0xd8: { const auto address = fetch8(); (void)read_direct(address); write_direct(address, registers_.x); return finish(opcode, 2, 4); }
    case 0xd9: { const auto address = static_cast<std::uint8_t>(fetch8() + registers_.y); (void)read_direct(address); write_direct(address, registers_.x); return finish(opcode, 2, 5); }
    case 0xdb: { const auto address = static_cast<std::uint8_t>(fetch8() + registers_.x); (void)read_direct(address); write_direct(address, registers_.y); return finish(opcode, 2, 5); }
    case 0xfa: { const auto source = fetch8(); const auto value = read_direct(source); const auto target = fetch8(); (void)read_direct(target); write_direct(target, value); return finish(opcode, 3, 5); }

    case 0xcd: // MOV X,#imm (documented IPL bootstrap before trace attachment)
        registers_.x = fetch8(); set_nz8(registers_.x); return finish(opcode, 2, 2);
    case 0xbd: // MOV SP,X
        registers_.sp = registers_.x; return finish(opcode, 1, 2);
    case 0xe8: // MOV A,#imm
        registers_.a = fetch8(); set_nz8(registers_.a); return finish(opcode, 2, 2);
    case 0x1d: // DEC X
        --registers_.x; set_nz8(registers_.x); return finish(opcode, 1, 2);
    case 0x2f: { // BRA rel8
        const auto displacement = static_cast<std::int8_t>(fetch8());
        registers_.pc = static_cast<std::uint16_t>(registers_.pc + displacement);
        return finish(opcode, 2, 4);
    }
    case 0x5d: // MOV X,A
        registers_.x = registers_.a; set_nz8(registers_.x); return finish(opcode, 1, 2);
    case 0x78: { // CMP dp,#imm
        const auto immediate = fetch8(); const auto address = fetch8();
        compare8(read_direct(address), immediate); return finish(opcode, 3, 5);
    }
    case 0x7e: { // CMP Y,dp
        const auto address = fetch8(); compare8(registers_.y, read_direct(address));
        return finish(opcode, 2, 3);
    }
    case 0x8f: { // MOV dp,#imm
        const auto immediate = fetch8(); const auto address = fetch8();
        (void)read_direct(address); write_direct(address, immediate); return finish(opcode, 3, 5);
    }
    case 0xba: { // MOVW YA,dp
        const auto address = fetch8();
        const auto low = read_direct(address);
        const auto high = read_direct(static_cast<std::uint8_t>(address + 1U));
        registers_.a = low; registers_.y = high;
        set_nz16(static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U)));
        return finish(opcode, 2, 5);
    }
    case 0xc4: { // MOV dp,A
        const auto address = fetch8(); (void)read_direct(address);
        write_direct(address, registers_.a); return finish(opcode, 2, 4);
    }
    case 0xc6: // MOV (X),A
        (void)read_direct(registers_.x); write_direct(registers_.x, registers_.a);
        return finish(opcode, 1, 4);
    case 0xcb: { // MOV dp,Y
        const auto address = fetch8(); (void)read_direct(address);
        write_direct(address, registers_.y); return finish(opcode, 2, 4);
    }
    case 0xd0: { // BNE rel8
        const auto displacement = static_cast<std::int8_t>(fetch8());
        if ((registers_.ps & kZero) == 0) {
            registers_.pc = static_cast<std::uint16_t>(registers_.pc + displacement);
            return finish(opcode, 2, 4);
        }
        return finish(opcode, 2, 2);
    }
    case 0xd7: { // MOV [dp]+Y,A
        const auto indirect = fetch8();
        const auto low = read_direct(indirect);
        const auto high = read_direct(static_cast<std::uint8_t>(indirect + 1U));
        const auto address = static_cast<std::uint16_t>(
            low | (static_cast<std::uint16_t>(high) << 8U));
        const auto target = static_cast<std::uint16_t>(address + registers_.y);
        (void)read8(target); write8(target, registers_.a); return finish(opcode, 2, 7);
    }
    case 0xda: { // MOVW dp,YA
        const auto address = fetch8(); (void)read_direct(address);
        write_direct(address, registers_.a);
        write_direct(static_cast<std::uint8_t>(address + 1U), registers_.y);
        return finish(opcode, 2, 5);
    }
    case 0xdd: // MOV A,Y
        registers_.a = registers_.y; set_nz8(registers_.a); return finish(opcode, 1, 2);
    case 0xe4: { // MOV A,dp
        registers_.a = read_direct(fetch8()); set_nz8(registers_.a); return finish(opcode, 2, 3);
    }
    case 0xeb: { // MOV Y,dp
        registers_.y = read_direct(fetch8()); set_nz8(registers_.y); return finish(opcode, 2, 3);
    }
    case 0xfc: // INC Y
        ++registers_.y; set_nz8(registers_.y); return finish(opcode, 1, 2);
    default: break;
    }
    stopped_ = true;
    return {SpcStepStatus::unsupported_opcode, opcode, 0, 0};
}

} // namespace kss::apu
