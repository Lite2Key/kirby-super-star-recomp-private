#include "kss/lifted_execution.hpp"

#include <cstdint>

namespace kss {
namespace {

void set_nz8(CpuContext& cpu, std::uint8_t value) noexcept {
    cpu.set_flag(StatusFlag::zero, value == 0);
    cpu.set_flag(StatusFlag::negative, (value & 0x80U) != 0);
}

void set_nz16(CpuContext& cpu, std::uint16_t value) noexcept {
    cpu.set_flag(StatusFlag::zero, value == 0);
    cpu.set_flag(StatusFlag::negative, (value & 0x8000U) != 0);
}

std::uint16_t operand16(const LiftedInstruction& instruction) noexcept {
    return static_cast<std::uint16_t>(instruction.operands[0]
        | (static_cast<std::uint16_t>(instruction.operands[1]) << 8U));
}

LiftResult finish(CpuContext& cpu, std::uint8_t bytes, std::uint8_t cycles) noexcept {
    cpu.pc = static_cast<std::uint16_t>(cpu.pc + bytes);
    cpu.cycles += cycles;
    return {LiftStatus::executed, bytes, cycles};
}

LiftResult reject_encoding() noexcept {
    return {LiftStatus::invalid_encoding, 0, 0};
}

std::uint32_t stack_address(const CpuContext& cpu) noexcept {
    return cpu.emulation ? static_cast<std::uint32_t>(0x0100U | (cpu.stack_pointer & 0x00ffU))
                         : static_cast<std::uint32_t>(cpu.stack_pointer);
}

void decrement_stack(CpuContext& cpu) noexcept {
    if (cpu.emulation) {
        cpu.stack_pointer = static_cast<std::uint16_t>(
            0x0100U | ((cpu.stack_pointer - 1U) & 0x00ffU));
    } else {
        --cpu.stack_pointer;
    }
}

void increment_stack(CpuContext& cpu) noexcept {
    if (cpu.emulation) {
        cpu.stack_pointer = static_cast<std::uint16_t>(
            0x0100U | ((cpu.stack_pointer + 1U) & 0x00ffU));
    } else {
        ++cpu.stack_pointer;
    }
}

void push8(CpuContext& cpu, Bus& bus, std::uint8_t value) noexcept {
    bus.write8(cpu.processor, stack_address(cpu), value, BusAccessKind::stack);
    decrement_stack(cpu);
}

std::uint8_t pop8(CpuContext& cpu, Bus& bus) noexcept {
    increment_stack(cpu);
    return bus.read8(cpu.processor, stack_address(cpu), BusAccessKind::stack);
}

} // namespace

LiftResult execute_lifted(
    CpuContext& cpu,
    Bus& bus,
    const LiftedInstruction& instruction) noexcept {
    // Implied flag/control instructions (all two cycles).
    if (instruction.opcode == 0xeaU || instruction.opcode == 0x18U
        || instruction.opcode == 0x38U || instruction.opcode == 0x58U
        || instruction.opcode == 0x78U || instruction.opcode == 0xb8U
        || instruction.opcode == 0xd8U || instruction.opcode == 0xf8U
        || instruction.opcode == 0xfbU || instruction.opcode == 0x9aU
        || instruction.opcode == 0x4bU || instruction.opcode == 0xabU
        || instruction.opcode == 0x2bU) {
        if (instruction.operand_count != 0U) {
            return reject_encoding();
        }
        switch (instruction.opcode) {
        case 0x18: cpu.set_flag(StatusFlag::carry, false); break; // CLC
        case 0x38: cpu.set_flag(StatusFlag::carry, true); break;  // SEC
        case 0x58: cpu.set_flag(StatusFlag::irq_disable, false); break; // CLI
        case 0x78: cpu.set_flag(StatusFlag::irq_disable, true); break;  // SEI
        case 0xb8: cpu.set_flag(StatusFlag::overflow, false); break; // CLV
        case 0xd8: cpu.set_flag(StatusFlag::decimal, false); break; // CLD
        case 0xf8: cpu.set_flag(StatusFlag::decimal, true); break; // SED
        case 0xfb: { // XCE: exchange carry and emulation mode.
            const auto old_carry = cpu.flag(StatusFlag::carry);
            cpu.set_flag(StatusFlag::carry, cpu.emulation);
            cpu.emulation = old_carry;
            cpu.normalize_after_mode_change();
            break;
        }
        case 0x9a: // TXS
            cpu.stack_pointer = cpu.x;
            if (cpu.emulation) {
                cpu.stack_pointer = static_cast<std::uint16_t>(0x0100U | (cpu.stack_pointer & 0xffU));
            }
            break;
        case 0x4b: // PHK
            push8(cpu, bus, cpu.program_bank);
            break;
        case 0xab: { // PLB
            const auto value = pop8(cpu, bus);
            cpu.data_bank = value;
            set_nz8(cpu, value);
            break;
        }
        case 0x2b: { // PLD
            const auto low = pop8(cpu, bus);
            const auto high = pop8(cpu, bus);
            cpu.direct_page = static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
            set_nz16(cpu, cpu.direct_page);
            break;
        }
        default: break; // NOP
        }
        auto cycles = std::uint8_t{2};
        if (instruction.opcode == 0x4bU) cycles = 3;
        else if (instruction.opcode == 0xabU) cycles = 4;
        else if (instruction.opcode == 0x2bU) cycles = 5;
        return finish(cpu, 1, cycles);
    }

    if (instruction.opcode == 0xf4U) { // PEA imm16
        if (instruction.operand_count != 2U) {
            return reject_encoding();
        }
        const auto value = operand16(instruction);
        push8(cpu, bus, static_cast<std::uint8_t>(value >> 8U));
        push8(cpu, bus, static_cast<std::uint8_t>(value));
        return finish(cpu, 3, 5);
    }

    if (instruction.opcode == 0xc2U || instruction.opcode == 0xe2U) { // REP/SEP
        if (instruction.operand_count != 1U) {
            return reject_encoding();
        }
        const auto mask = instruction.operands[0];
        if (instruction.opcode == 0xc2U) {
            cpu.status = static_cast<std::uint8_t>(cpu.status & static_cast<std::uint8_t>(~mask));
        } else {
            cpu.status = static_cast<std::uint8_t>(cpu.status | mask);
        }
        cpu.normalize_after_mode_change();
        return finish(cpu, 2, 3);
    }

    if (instruction.opcode == 0xa9U || instruction.opcode == 0xa2U
        || instruction.opcode == 0xa0U) { // LDA/LDX/LDY immediate
        const auto accumulator = instruction.opcode == 0xa9U;
        const auto width8 = accumulator ? cpu.mode().accumulator_8bit : cpu.mode().index_8bit;
        const auto required = static_cast<std::uint8_t>(width8 ? 1U : 2U);
        if (instruction.operand_count != required) {
            return reject_encoding();
        }
        if (width8) {
            const auto value = instruction.operands[0];
            if (instruction.opcode == 0xa9U) {
                cpu.a = static_cast<std::uint16_t>((cpu.a & 0xff00U) | value);
            } else if (instruction.opcode == 0xa2U) {
                cpu.x = value;
            } else {
                cpu.y = value;
            }
            set_nz8(cpu, value);
        } else {
            const auto value = operand16(instruction);
            if (instruction.opcode == 0xa9U) cpu.a = value;
            else if (instruction.opcode == 0xa2U) cpu.x = value;
            else cpu.y = value;
            set_nz16(cpu, value);
        }
        return finish(cpu, static_cast<std::uint8_t>(required + 1U),
            static_cast<std::uint8_t>(width8 ? 2U : 3U));
    }

    if (instruction.opcode == 0xadU) { // LDA abs
        if (instruction.operand_count != 2U) {
            return reject_encoding();
        }
        const auto width8 = cpu.mode().accumulator_8bit;
        const auto address = (static_cast<std::uint32_t>(cpu.data_bank) << 16U)
            | operand16(instruction);
        if (width8) {
            const auto value = bus.read8(cpu.processor, address, BusAccessKind::data);
            cpu.a = static_cast<std::uint16_t>((cpu.a & 0xff00U) | value);
            set_nz8(cpu, value);
        } else {
            // Absolute data operands wrap the high-byte access inside DBR; they
            // never carry into the next bank at $xx:FFFF.
            const auto value = read16_bank_wrapped(
                bus, cpu.processor, address, BusAccessKind::data);
            cpu.a = value;
            set_nz16(cpu, value);
        }
        return finish(cpu, 3, static_cast<std::uint8_t>(width8 ? 4U : 5U));
    }

    if (instruction.opcode == 0x8dU || instruction.opcode == 0x8eU
        || instruction.opcode == 0x8cU || instruction.opcode == 0x9cU) { // STA/STX/STY/STZ abs
        if (instruction.operand_count != 2U) {
            return reject_encoding();
        }
        const auto accumulator = instruction.opcode == 0x8dU || instruction.opcode == 0x9cU;
        const auto width8 = accumulator ? cpu.mode().accumulator_8bit : cpu.mode().index_8bit;
        const auto address = (static_cast<std::uint32_t>(cpu.data_bank) << 16U) | operand16(instruction);
        std::uint16_t value = 0;
        if (instruction.opcode == 0x8dU) value = cpu.a;
        else if (instruction.opcode == 0x8eU) value = cpu.x;
        else if (instruction.opcode == 0x8cU) value = cpu.y;
        bus.write8(cpu.processor, address, static_cast<std::uint8_t>(value), BusAccessKind::data);
        if (!width8) {
            const auto next = (address & 0x00ff'0000U) | ((address + 1U) & 0xffffU);
            bus.write8(cpu.processor, next, static_cast<std::uint8_t>(value >> 8U), BusAccessKind::data);
        }
        return finish(cpu, 3, static_cast<std::uint8_t>(width8 ? 4U : 5U));
    }

    if (instruction.opcode == 0x85U || instruction.opcode == 0x64U) { // STA/STZ dp
        if (instruction.operand_count != 1U) {
            return reject_encoding();
        }
        const auto width8 = cpu.mode().accumulator_8bit;
        const auto address = static_cast<std::uint16_t>(
            cpu.direct_page + instruction.operands[0]);
        const auto value = instruction.opcode == 0x85U ? cpu.a : std::uint16_t{0};
        bus.write8(cpu.processor, address, static_cast<std::uint8_t>(value), BusAccessKind::data);
        if (!width8) {
            bus.write8(cpu.processor, static_cast<std::uint16_t>(address + 1U),
                static_cast<std::uint8_t>(value >> 8U), BusAccessKind::data);
        }
        const auto direct_page_penalty = (cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U;
        const auto cycles = static_cast<std::uint8_t>((width8 ? 3U : 4U) + direct_page_penalty);
        return finish(cpu, 2, cycles);
    }

    if (instruction.opcode == 0x54U) { // MVN destination-bank, source-bank
        if (instruction.operand_count != 2U) {
            return reject_encoding();
        }

        // One dispatch performs one restartable architectural transfer.  PC
        // remains on MVN while bytes remain and advances only after A wraps.
        const auto destination_bank = instruction.operands[0];
        const auto source_bank = instruction.operands[1];
        cpu.data_bank = destination_bank;
        const auto source_address = (static_cast<std::uint32_t>(source_bank) << 16U) | cpu.x;
        const auto destination_address =
            (static_cast<std::uint32_t>(destination_bank) << 16U) | cpu.y;
        const auto value = bus.read8(cpu.processor, source_address, BusAccessKind::data);
        bus.write8(cpu.processor, destination_address, value, BusAccessKind::data);

        cpu.x = static_cast<std::uint16_t>(cpu.x + 1U);
        cpu.y = static_cast<std::uint16_t>(cpu.y + 1U);
        if (cpu.mode().index_8bit) {
            cpu.x &= 0x00ffU;
            cpu.y &= 0x00ffU;
        }
        cpu.a = static_cast<std::uint16_t>(cpu.a - 1U);
        if (cpu.a == 0xffffU) {
            cpu.pc = static_cast<std::uint16_t>(cpu.pc + 3U);
        }
        cpu.cycles += 7U;
        return {LiftStatus::executed, 3, 7};
    }

    if (instruction.opcode == 0x80U) { // BRA rel8
        if (instruction.operand_count != 1U) {
            return reject_encoding();
        }
        const auto displacement = static_cast<std::int8_t>(instruction.operands[0]);
        cpu.pc = static_cast<std::uint16_t>(cpu.pc + 2U + displacement);
        cpu.cycles += 3U;
        return {LiftStatus::executed, 2, 3};
    }

    if (instruction.opcode == 0x10U) { // BPL rel8
        if (instruction.operand_count != 1U) {
            return reject_encoding();
        }
        const auto next_pc = static_cast<std::uint16_t>(cpu.pc + 2U);
        auto cycles = std::uint8_t{2};
        if (!cpu.flag(StatusFlag::negative)) {
            const auto displacement = static_cast<std::int8_t>(instruction.operands[0]);
            const auto target = static_cast<std::uint16_t>(next_pc + displacement);
            cycles = 3;
            // The 65816 retains the 6502 page-cross penalty only while in
            // emulation mode. Relative control flow wraps PC within PBR.
            if (cpu.emulation && (next_pc & 0xff00U) != (target & 0xff00U)) {
                ++cycles;
            }
            cpu.pc = target;
        } else {
            cpu.pc = next_pc;
        }
        cpu.cycles += cycles;
        return {LiftStatus::executed, 2, cycles};
    }

    if (instruction.opcode == 0x4cU) { // JMP abs
        if (instruction.operand_count != 2U) {
            return reject_encoding();
        }
        cpu.pc = operand16(instruction);
        cpu.cycles += 3U;
        return {LiftStatus::executed, 3, 3};
    }

    return {LiftStatus::unsupported_opcode, 0, 0};
}

} // namespace kss
