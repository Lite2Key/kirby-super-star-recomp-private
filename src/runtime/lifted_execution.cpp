#include "kss/lifted_execution.hpp"
#include "kss/alu.hpp"

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

std::uint32_t operand24(const LiftedInstruction& instruction) noexcept {
    return static_cast<std::uint32_t>(operand16(instruction))
        | (static_cast<std::uint32_t>(instruction.operands[2]) << 16U);
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

LiftResult branch_relative8(
    CpuContext& cpu, const LiftedInstruction& instruction, bool taken) noexcept {
    if (instruction.operand_count != 1U) return reject_encoding();
    const auto next_pc = static_cast<std::uint16_t>(cpu.pc + 2U);
    auto cycles = std::uint8_t{2};
    if (taken) {
        const auto displacement = static_cast<std::int8_t>(instruction.operands[0]);
        const auto target = static_cast<std::uint16_t>(next_pc + displacement);
        cycles = 3;
        if (cpu.emulation && (next_pc & 0xff00U) != (target & 0xff00U)) ++cycles;
        cpu.pc = target;
    } else {
        cpu.pc = next_pc;
    }
    cpu.cycles += cycles;
    return {LiftStatus::executed, 2, cycles};
}

enum class AccumulatorAluOperation : std::uint8_t {
    bit_or,
    bit_and,
    bit_xor,
    add,
    compare,
    subtract,
};

enum class AccumulatorAluMode : std::uint8_t {
    immediate,
    direct,
    absolute,
    absolute_long,
    direct_x,
    absolute_x,
    absolute_y,
    absolute_long_x,
};

bool decode_accumulator_alu(
    std::uint8_t opcode,
    AccumulatorAluOperation& operation,
    AccumulatorAluMode& mode) noexcept {
    switch (opcode & 0xe0U) {
    case 0x00: operation = AccumulatorAluOperation::bit_or; break;
    case 0x20: operation = AccumulatorAluOperation::bit_and; break;
    case 0x40: operation = AccumulatorAluOperation::bit_xor; break;
    case 0x60: operation = AccumulatorAluOperation::add; break;
    case 0xc0: operation = AccumulatorAluOperation::compare; break;
    case 0xe0: operation = AccumulatorAluOperation::subtract; break;
    default: return false;
    }
    switch (opcode & 0x1fU) {
    case 0x09: mode = AccumulatorAluMode::immediate; return true;
    case 0x05: mode = AccumulatorAluMode::direct; return true;
    case 0x0d: mode = AccumulatorAluMode::absolute; return true;
    case 0x0f: mode = AccumulatorAluMode::absolute_long; return true;
    case 0x15: mode = AccumulatorAluMode::direct_x; return true;
    case 0x1d: mode = AccumulatorAluMode::absolute_x; return true;
    case 0x19: mode = AccumulatorAluMode::absolute_y; return true;
    case 0x1f: mode = AccumulatorAluMode::absolute_long_x; return true;
    default: return false;
    }
}

std::uint16_t direct_address(const CpuContext& cpu, std::uint16_t offset) noexcept {
    if (cpu.emulation && (cpu.direct_page & 0x00ffU) == 0U) {
        return static_cast<std::uint16_t>((cpu.direct_page & 0xff00U) | (offset & 0x00ffU));
    }
    return static_cast<std::uint16_t>(cpu.direct_page + offset);
}

std::uint16_t read_direct_pointer_word(
    CpuContext& cpu, Bus& bus, std::uint16_t offset, bool indexed_x_bug) noexcept {
    const auto low_address = direct_address(cpu, offset);
    auto high_address = direct_address(cpu, static_cast<std::uint16_t>(offset + 1U));
    if (indexed_x_bug && cpu.emulation && (cpu.direct_page & 0x00ffU) != 0U
        && (high_address & 0x00ffU) == 0U) {
        high_address = static_cast<std::uint16_t>(high_address - 0x0100U);
    }
    const auto low = bus.read8(cpu.processor, low_address, BusAccessKind::data);
    const auto high = bus.read8(cpu.processor, high_address, BusAccessKind::data);
    return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
}

std::uint32_t read_direct_pointer_long(
    CpuContext& cpu, Bus& bus, std::uint16_t offset) noexcept {
    const auto address = static_cast<std::uint16_t>(cpu.direct_page + offset);
    const auto low = bus.read8(cpu.processor, address, BusAccessKind::data);
    const auto high = bus.read8(cpu.processor,
        static_cast<std::uint16_t>(address + 1U), BusAccessKind::data);
    const auto bank = bus.read8(cpu.processor,
        static_cast<std::uint16_t>(address + 2U), BusAccessKind::data);
    return static_cast<std::uint32_t>(low)
        | (static_cast<std::uint32_t>(high) << 8U)
        | (static_cast<std::uint32_t>(bank) << 16U);
}

std::uint16_t read16_linear24(
    Bus& bus, ProcessorId processor, std::uint32_t address) noexcept {
    const auto low = bus.read8(processor, address & 0x00ff'ffffU, BusAccessKind::data);
    const auto high = bus.read8(
        processor, (address + 1U) & 0x00ff'ffffU, BusAccessKind::data);
    return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
}

void write16_linear24(
    Bus& bus, ProcessorId processor, std::uint32_t address, std::uint16_t value) noexcept {
    bus.write8(processor, address & 0x00ff'ffffU,
        static_cast<std::uint8_t>(value), BusAccessKind::data);
    bus.write8(processor, (address + 1U) & 0x00ff'ffffU,
        static_cast<std::uint8_t>(value >> 8U), BusAccessKind::data);
}

enum class LoadStoreOperation : std::uint8_t { lda, ldx, ldy, sta, stx, sty, stz };
enum class LoadStoreMode : std::uint8_t {
    immediate,
    direct,
    absolute,
    absolute_long,
    direct_x,
    direct_y,
    absolute_x,
    absolute_y,
    absolute_long_x,
};

bool decode_load_store(
    std::uint8_t opcode, LoadStoreOperation& operation, LoadStoreMode& mode) noexcept {
    switch (opcode) {
    case 0xa9: operation = LoadStoreOperation::lda; mode = LoadStoreMode::immediate; return true;
    case 0xa5: operation = LoadStoreOperation::lda; mode = LoadStoreMode::direct; return true;
    case 0xad: operation = LoadStoreOperation::lda; mode = LoadStoreMode::absolute; return true;
    case 0xaf: operation = LoadStoreOperation::lda; mode = LoadStoreMode::absolute_long; return true;
    case 0xb5: operation = LoadStoreOperation::lda; mode = LoadStoreMode::direct_x; return true;
    case 0xbd: operation = LoadStoreOperation::lda; mode = LoadStoreMode::absolute_x; return true;
    case 0xb9: operation = LoadStoreOperation::lda; mode = LoadStoreMode::absolute_y; return true;
    case 0xbf: operation = LoadStoreOperation::lda; mode = LoadStoreMode::absolute_long_x; return true;
    case 0xa2: operation = LoadStoreOperation::ldx; mode = LoadStoreMode::immediate; return true;
    case 0xa6: operation = LoadStoreOperation::ldx; mode = LoadStoreMode::direct; return true;
    case 0xae: operation = LoadStoreOperation::ldx; mode = LoadStoreMode::absolute; return true;
    case 0xb6: operation = LoadStoreOperation::ldx; mode = LoadStoreMode::direct_y; return true;
    case 0xbe: operation = LoadStoreOperation::ldx; mode = LoadStoreMode::absolute_y; return true;
    case 0xa0: operation = LoadStoreOperation::ldy; mode = LoadStoreMode::immediate; return true;
    case 0xa4: operation = LoadStoreOperation::ldy; mode = LoadStoreMode::direct; return true;
    case 0xac: operation = LoadStoreOperation::ldy; mode = LoadStoreMode::absolute; return true;
    case 0xb4: operation = LoadStoreOperation::ldy; mode = LoadStoreMode::direct_x; return true;
    case 0xbc: operation = LoadStoreOperation::ldy; mode = LoadStoreMode::absolute_x; return true;
    case 0x85: operation = LoadStoreOperation::sta; mode = LoadStoreMode::direct; return true;
    case 0x8d: operation = LoadStoreOperation::sta; mode = LoadStoreMode::absolute; return true;
    case 0x8f: operation = LoadStoreOperation::sta; mode = LoadStoreMode::absolute_long; return true;
    case 0x95: operation = LoadStoreOperation::sta; mode = LoadStoreMode::direct_x; return true;
    case 0x9d: operation = LoadStoreOperation::sta; mode = LoadStoreMode::absolute_x; return true;
    case 0x99: operation = LoadStoreOperation::sta; mode = LoadStoreMode::absolute_y; return true;
    case 0x9f: operation = LoadStoreOperation::sta; mode = LoadStoreMode::absolute_long_x; return true;
    case 0x86: operation = LoadStoreOperation::stx; mode = LoadStoreMode::direct; return true;
    case 0x8e: operation = LoadStoreOperation::stx; mode = LoadStoreMode::absolute; return true;
    case 0x96: operation = LoadStoreOperation::stx; mode = LoadStoreMode::direct_y; return true;
    case 0x84: operation = LoadStoreOperation::sty; mode = LoadStoreMode::direct; return true;
    case 0x8c: operation = LoadStoreOperation::sty; mode = LoadStoreMode::absolute; return true;
    case 0x94: operation = LoadStoreOperation::sty; mode = LoadStoreMode::direct_x; return true;
    case 0x64: operation = LoadStoreOperation::stz; mode = LoadStoreMode::direct; return true;
    case 0x9c: operation = LoadStoreOperation::stz; mode = LoadStoreMode::absolute; return true;
    case 0x74: operation = LoadStoreOperation::stz; mode = LoadStoreMode::direct_x; return true;
    case 0x9e: operation = LoadStoreOperation::stz; mode = LoadStoreMode::absolute_x; return true;
    default: return false;
    }
}

enum class RmwOperation : std::uint8_t { asl, rol, lsr, ror, decrement, increment };
enum class RmwMode : std::uint8_t { accumulator, direct, absolute, direct_x, absolute_x };

bool decode_rmw(std::uint8_t opcode, RmwOperation& operation, RmwMode& mode) noexcept {
    switch (opcode) {
    case 0x0a: operation = RmwOperation::asl; mode = RmwMode::accumulator; return true;
    case 0x06: operation = RmwOperation::asl; mode = RmwMode::direct; return true;
    case 0x0e: operation = RmwOperation::asl; mode = RmwMode::absolute; return true;
    case 0x16: operation = RmwOperation::asl; mode = RmwMode::direct_x; return true;
    case 0x1e: operation = RmwOperation::asl; mode = RmwMode::absolute_x; return true;
    case 0x2a: operation = RmwOperation::rol; mode = RmwMode::accumulator; return true;
    case 0x26: operation = RmwOperation::rol; mode = RmwMode::direct; return true;
    case 0x2e: operation = RmwOperation::rol; mode = RmwMode::absolute; return true;
    case 0x36: operation = RmwOperation::rol; mode = RmwMode::direct_x; return true;
    case 0x3e: operation = RmwOperation::rol; mode = RmwMode::absolute_x; return true;
    case 0x4a: operation = RmwOperation::lsr; mode = RmwMode::accumulator; return true;
    case 0x46: operation = RmwOperation::lsr; mode = RmwMode::direct; return true;
    case 0x4e: operation = RmwOperation::lsr; mode = RmwMode::absolute; return true;
    case 0x56: operation = RmwOperation::lsr; mode = RmwMode::direct_x; return true;
    case 0x5e: operation = RmwOperation::lsr; mode = RmwMode::absolute_x; return true;
    case 0x6a: operation = RmwOperation::ror; mode = RmwMode::accumulator; return true;
    case 0x66: operation = RmwOperation::ror; mode = RmwMode::direct; return true;
    case 0x6e: operation = RmwOperation::ror; mode = RmwMode::absolute; return true;
    case 0x76: operation = RmwOperation::ror; mode = RmwMode::direct_x; return true;
    case 0x7e: operation = RmwOperation::ror; mode = RmwMode::absolute_x; return true;
    case 0x3a: operation = RmwOperation::decrement; mode = RmwMode::accumulator; return true;
    case 0xc6: operation = RmwOperation::decrement; mode = RmwMode::direct; return true;
    case 0xce: operation = RmwOperation::decrement; mode = RmwMode::absolute; return true;
    case 0xd6: operation = RmwOperation::decrement; mode = RmwMode::direct_x; return true;
    case 0xde: operation = RmwOperation::decrement; mode = RmwMode::absolute_x; return true;
    case 0x1a: operation = RmwOperation::increment; mode = RmwMode::accumulator; return true;
    case 0xe6: operation = RmwOperation::increment; mode = RmwMode::direct; return true;
    case 0xee: operation = RmwOperation::increment; mode = RmwMode::absolute; return true;
    case 0xf6: operation = RmwOperation::increment; mode = RmwMode::direct_x; return true;
    case 0xfe: operation = RmwOperation::increment; mode = RmwMode::absolute_x; return true;
    default: return false;
    }
}

enum class IndirectAccumulatorOperation : std::uint8_t {
    bit_or, bit_and, bit_xor, add, store, load, compare, subtract,
};
enum class IndirectAccumulatorMode : std::uint8_t {
    direct_x_indirect,
    stack_relative,
    direct_indirect_long,
    direct_indirect_y,
    direct_indirect,
    stack_relative_indirect_y,
    direct_indirect_long_y,
};

bool decode_indirect_accumulator(std::uint8_t opcode,
    IndirectAccumulatorOperation& operation, IndirectAccumulatorMode& mode) noexcept {
    switch (opcode & 0xe0U) {
    case 0x00: operation = IndirectAccumulatorOperation::bit_or; break;
    case 0x20: operation = IndirectAccumulatorOperation::bit_and; break;
    case 0x40: operation = IndirectAccumulatorOperation::bit_xor; break;
    case 0x60: operation = IndirectAccumulatorOperation::add; break;
    case 0x80: operation = IndirectAccumulatorOperation::store; break;
    case 0xa0: operation = IndirectAccumulatorOperation::load; break;
    case 0xc0: operation = IndirectAccumulatorOperation::compare; break;
    case 0xe0: operation = IndirectAccumulatorOperation::subtract; break;
    default: return false;
    }
    switch (opcode & 0x1fU) {
    case 0x01: mode = IndirectAccumulatorMode::direct_x_indirect; return true;
    case 0x03: mode = IndirectAccumulatorMode::stack_relative; return true;
    case 0x07: mode = IndirectAccumulatorMode::direct_indirect_long; return true;
    case 0x11: mode = IndirectAccumulatorMode::direct_indirect_y; return true;
    case 0x12: mode = IndirectAccumulatorMode::direct_indirect; return true;
    case 0x13: mode = IndirectAccumulatorMode::stack_relative_indirect_y; return true;
    case 0x17: mode = IndirectAccumulatorMode::direct_indirect_long_y; return true;
    default: return false;
    }
}

std::uint8_t apply_rmw8(CpuContext& cpu, RmwOperation operation, std::uint8_t value) noexcept {
    switch (operation) {
    case RmwOperation::asl: return alu::asl8(cpu, value);
    case RmwOperation::rol: return alu::rol8(cpu, value);
    case RmwOperation::lsr: return alu::lsr8(cpu, value);
    case RmwOperation::ror: return alu::ror8(cpu, value);
    case RmwOperation::decrement: return alu::decrement8(cpu, value);
    case RmwOperation::increment: return alu::increment8(cpu, value);
    }
    return value;
}

std::uint16_t apply_rmw16(
    CpuContext& cpu, RmwOperation operation, std::uint16_t value) noexcept {
    switch (operation) {
    case RmwOperation::asl: return alu::asl16(cpu, value);
    case RmwOperation::rol: return alu::rol16(cpu, value);
    case RmwOperation::lsr: return alu::lsr16(cpu, value);
    case RmwOperation::ror: return alu::ror16(cpu, value);
    case RmwOperation::decrement: return alu::decrement16(cpu, value);
    case RmwOperation::increment: return alu::increment16(cpu, value);
    }
    return value;
}

std::uint8_t apply_accumulator_alu8(
    CpuContext& cpu, AccumulatorAluOperation operation, std::uint8_t right) noexcept {
    const auto left = static_cast<std::uint8_t>(cpu.a);
    switch (operation) {
    case AccumulatorAluOperation::bit_or: return alu::bit_or8(cpu, left, right);
    case AccumulatorAluOperation::bit_and: return alu::bit_and8(cpu, left, right);
    case AccumulatorAluOperation::bit_xor: return alu::bit_xor8(cpu, left, right);
    case AccumulatorAluOperation::add: return alu::adc8(cpu, left, right);
    case AccumulatorAluOperation::subtract: return alu::sbc8(cpu, left, right);
    case AccumulatorAluOperation::compare: alu::compare8(cpu, left, right); return left;
    }
    return left;
}

std::uint16_t apply_accumulator_alu16(
    CpuContext& cpu, AccumulatorAluOperation operation, std::uint16_t right) noexcept {
    switch (operation) {
    case AccumulatorAluOperation::bit_or: return alu::bit_or16(cpu, cpu.a, right);
    case AccumulatorAluOperation::bit_and: return alu::bit_and16(cpu, cpu.a, right);
    case AccumulatorAluOperation::bit_xor: return alu::bit_xor16(cpu, cpu.a, right);
    case AccumulatorAluOperation::add: return alu::adc16(cpu, cpu.a, right);
    case AccumulatorAluOperation::subtract: return alu::sbc16(cpu, cpu.a, right);
    case AccumulatorAluOperation::compare: alu::compare16(cpu, cpu.a, right); return cpu.a;
    }
    return cpu.a;
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
        || instruction.opcode == 0xfbU
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

    if (instruction.opcode == 0x00U || instruction.opcode == 0x02U) { // BRK / COP
        if (instruction.operand_count != 1U) return reject_encoding();
        const auto return_address = static_cast<std::uint16_t>(cpu.pc + 2U);
        if (!cpu.emulation) push8(cpu, bus, cpu.program_bank);
        push8(cpu, bus, static_cast<std::uint8_t>(return_address >> 8U));
        push8(cpu, bus, static_cast<std::uint8_t>(return_address));
        const auto pushed_status = cpu.emulation
            ? static_cast<std::uint8_t>(cpu.status | 0x30U) : cpu.status;
        push8(cpu, bus, pushed_status);
        cpu.set_flag(StatusFlag::irq_disable, true);
        cpu.set_flag(StatusFlag::decimal, false);
        cpu.program_bank = 0;
        const auto vector = static_cast<std::uint16_t>(instruction.opcode == 0x00U
            ? (cpu.emulation ? 0xfffeU : 0xffe6U)
            : (cpu.emulation ? 0xfff4U : 0xffe4U));
        const auto low = bus.read8(cpu.processor, vector, BusAccessKind::vector);
        const auto high = bus.read8(cpu.processor,
            static_cast<std::uint16_t>(vector + 1U), BusAccessKind::vector);
        cpu.pc = static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
        const auto cycles = static_cast<std::uint8_t>(cpu.emulation ? 7U : 8U);
        cpu.cycles += cycles;
        return {LiftStatus::executed, 2, cycles};
    }

    if (instruction.opcode == 0x42U) { // WDM signature byte
        if (instruction.operand_count != 1U) return reject_encoding();
        return finish(cpu, 2, 2);
    }

    if (instruction.opcode == 0xcbU || instruction.opcode == 0xdbU) { // WAI / STP
        if (instruction.operand_count != 0U) return reject_encoding();
        if (instruction.opcode == 0xcbU) cpu.waiting = true;
        else cpu.stopped = true;
        return finish(cpu, 1, 3);
    }

    if (instruction.opcode == 0x0bU) { // PHD
        if (instruction.operand_count != 0U) {
            return reject_encoding();
        }
        push8(cpu, bus, static_cast<std::uint8_t>(cpu.direct_page >> 8U));
        push8(cpu, bus, static_cast<std::uint8_t>(cpu.direct_page));
        return finish(cpu, 1, 4);
    }

    if (instruction.opcode == 0x48U) { // PHA
        if (instruction.operand_count != 0U) {
            return reject_encoding();
        }
        if (cpu.mode().accumulator_8bit) {
            push8(cpu, bus, static_cast<std::uint8_t>(cpu.a));
            return finish(cpu, 1, 3);
        }
        push8(cpu, bus, static_cast<std::uint8_t>(cpu.a >> 8U));
        push8(cpu, bus, static_cast<std::uint8_t>(cpu.a));
        return finish(cpu, 1, 4);
    }

    if (instruction.opcode == 0x68U) { // PLA
        if (instruction.operand_count != 0U) {
            return reject_encoding();
        }
        if (cpu.mode().accumulator_8bit) {
            const auto value = pop8(cpu, bus);
            cpu.a = static_cast<std::uint16_t>((cpu.a & 0xff00U) | value);
            set_nz8(cpu, value);
            return finish(cpu, 1, 4);
        }
        const auto low = pop8(cpu, bus);
        const auto high = pop8(cpu, bus);
        cpu.a = static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
        set_nz16(cpu, cpu.a);
        return finish(cpu, 1, 5);
    }

    if (instruction.opcode == 0x08U || instruction.opcode == 0x8bU
        || instruction.opcode == 0xdaU || instruction.opcode == 0x5aU) { // PHP/PHB/PHX/PHY
        if (instruction.operand_count != 0U) return reject_encoding();
        if (instruction.opcode == 0x08U) push8(cpu, bus, cpu.status);
        else if (instruction.opcode == 0x8bU) push8(cpu, bus, cpu.data_bank);
        else {
            const auto value = instruction.opcode == 0xdaU ? cpu.x : cpu.y;
            if (!cpu.mode().index_8bit) push8(cpu, bus, static_cast<std::uint8_t>(value >> 8U));
            push8(cpu, bus, static_cast<std::uint8_t>(value));
        }
        const auto cycles = static_cast<std::uint8_t>(
            (instruction.opcode == 0xdaU || instruction.opcode == 0x5aU)
                && !cpu.mode().index_8bit ? 4U : 3U);
        return finish(cpu, 1, cycles);
    }

    if (instruction.opcode == 0x28U || instruction.opcode == 0xfaU
        || instruction.opcode == 0x7aU) { // PLP/PLX/PLY
        if (instruction.operand_count != 0U) return reject_encoding();
        if (instruction.opcode == 0x28U) {
            cpu.status = pop8(cpu, bus);
            cpu.normalize_after_mode_change();
            return finish(cpu, 1, 4);
        }
        const auto width8 = cpu.mode().index_8bit;
        const auto low = pop8(cpu, bus);
        const auto value = width8 ? static_cast<std::uint16_t>(low)
            : static_cast<std::uint16_t>(low
                | (static_cast<std::uint16_t>(pop8(cpu, bus)) << 8U));
        if (instruction.opcode == 0xfaU) cpu.x = value;
        else cpu.y = value;
        if (width8) set_nz8(cpu, low);
        else set_nz16(cpu, value);
        return finish(cpu, 1, static_cast<std::uint8_t>(width8 ? 4U : 5U));
    }

    if (instruction.opcode == 0xd4U) { // PEI dp
        if (instruction.operand_count != 1U) return reject_encoding();
        const auto address = direct_address(cpu, instruction.operands[0]);
        const auto value = read16_bank_wrapped(
            bus, cpu.processor, address, BusAccessKind::data);
        push8(cpu, bus, static_cast<std::uint8_t>(value >> 8U));
        push8(cpu, bus, static_cast<std::uint8_t>(value));
        const auto cycles = static_cast<std::uint8_t>(
            6U + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
        return finish(cpu, 2, cycles);
    }

    if (instruction.opcode == 0x62U) { // PER rel16
        if (instruction.operand_count != 2U) return reject_encoding();
        const auto displacement = static_cast<std::int16_t>(operand16(instruction));
        const auto value = static_cast<std::uint16_t>(cpu.pc + 3U + displacement);
        push8(cpu, bus, static_cast<std::uint8_t>(value >> 8U));
        push8(cpu, bus, static_cast<std::uint8_t>(value));
        return finish(cpu, 3, 6);
    }

    RmwOperation rmw_operation{};
    RmwMode rmw_mode{};
    if (decode_rmw(instruction.opcode, rmw_operation, rmw_mode)) {
        const auto accumulator = rmw_mode == RmwMode::accumulator;
        const auto direct = rmw_mode == RmwMode::direct || rmw_mode == RmwMode::direct_x;
        const auto required = static_cast<std::uint8_t>(accumulator ? 0U : direct ? 1U : 2U);
        if (instruction.operand_count != required) return reject_encoding();
        const auto width8 = cpu.mode().accumulator_8bit;
        if (accumulator) {
            if (width8) {
                const auto result = apply_rmw8(cpu, rmw_operation, static_cast<std::uint8_t>(cpu.a));
                cpu.a = static_cast<std::uint16_t>((cpu.a & 0xff00U) | result);
            } else {
                cpu.a = apply_rmw16(cpu, rmw_operation, cpu.a);
            }
            return finish(cpu, 1, 2);
        }

        std::uint32_t address = 0;
        auto cycles = std::uint8_t{0};
        if (rmw_mode == RmwMode::direct || rmw_mode == RmwMode::direct_x) {
            const auto index = rmw_mode == RmwMode::direct_x ? cpu.x : 0U;
            address = direct_address(cpu,
                static_cast<std::uint16_t>(instruction.operands[0] + index));
            cycles = static_cast<std::uint8_t>(
                (rmw_mode == RmwMode::direct_x ? 6U : 5U)
                + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
        } else {
            const auto base = (static_cast<std::uint32_t>(cpu.data_bank) << 16U)
                | operand16(instruction);
            address = rmw_mode == RmwMode::absolute_x
                ? (base + cpu.x) & 0x00ff'ffffU : base;
            cycles = rmw_mode == RmwMode::absolute_x ? 7U : 6U;
        }
        if (width8) {
            const auto old_value = bus.read8(cpu.processor, address, BusAccessKind::data);
            const auto new_value = apply_rmw8(cpu, rmw_operation, old_value);
            // In emulation mode the idle becomes a 6502-compatible dummy write;
            // native M=1 performs an internal idle instead.
            if (cpu.emulation) {
                bus.write8(cpu.processor, address, old_value, BusAccessKind::data);
            }
            bus.write8(cpu.processor, address, new_value, BusAccessKind::data);
        } else {
            const auto old_value = direct
                ? read16_bank_wrapped(bus, cpu.processor, address, BusAccessKind::data)
                : read16_linear24(bus, cpu.processor, address);
            const auto new_value = apply_rmw16(cpu, rmw_operation, old_value);
            const auto high_address = direct
                ? (address & 0x00ff'0000U) | ((address + 1U) & 0x0000'ffffU)
                : (address + 1U) & 0x00ff'ffffU;
            // 16-bit RMW writes the result high byte first, then low byte.
            bus.write8(cpu.processor, high_address,
                static_cast<std::uint8_t>(new_value >> 8U), BusAccessKind::data);
            bus.write8(cpu.processor, address & 0x00ff'ffffU,
                static_cast<std::uint8_t>(new_value), BusAccessKind::data);
            cycles = static_cast<std::uint8_t>(cycles + 2U);
        }
        return finish(cpu, static_cast<std::uint8_t>(required + 1U), cycles);
    }

    if (instruction.opcode == 0xaaU || instruction.opcode == 0xa8U
        || instruction.opcode == 0xbaU || instruction.opcode == 0x8aU
        || instruction.opcode == 0x9aU || instruction.opcode == 0x9bU
        || instruction.opcode == 0x98U || instruction.opcode == 0xbbU
        || instruction.opcode == 0x5bU || instruction.opcode == 0x1bU
        || instruction.opcode == 0x7bU || instruction.opcode == 0x3bU) {
        if (instruction.operand_count != 0U) return reject_encoding();
        const auto set_index = [&](std::uint16_t& target, std::uint16_t value) {
            if (cpu.mode().index_8bit) {
                target = static_cast<std::uint8_t>(value);
                set_nz8(cpu, static_cast<std::uint8_t>(target));
            } else {
                target = value;
                set_nz16(cpu, target);
            }
        };
        const auto set_accumulator = [&](std::uint16_t value, bool force16 = false) {
            if (!force16 && cpu.mode().accumulator_8bit) {
                cpu.a = static_cast<std::uint16_t>((cpu.a & 0xff00U) | (value & 0x00ffU));
                set_nz8(cpu, static_cast<std::uint8_t>(cpu.a));
            } else {
                cpu.a = value;
                set_nz16(cpu, cpu.a);
            }
        };
        switch (instruction.opcode) {
        case 0xaa: set_index(cpu.x, cpu.a); break; // TAX
        case 0xa8: set_index(cpu.y, cpu.a); break; // TAY
        case 0xba: set_index(cpu.x, cpu.stack_pointer); break; // TSX
        case 0x8a: set_accumulator(cpu.x); break; // TXA
        case 0x9b: set_index(cpu.y, cpu.x); break; // TXY
        case 0x98: set_accumulator(cpu.y); break; // TYA
        case 0xbb: set_index(cpu.x, cpu.y); break; // TYX
        case 0x5b: cpu.direct_page = cpu.a; set_nz16(cpu, cpu.direct_page); break; // TCD
        case 0x7b: set_accumulator(cpu.direct_page, true); break; // TDC
        case 0x3b: set_accumulator(cpu.stack_pointer, true); break; // TSC
        case 0x1b: // TCS
            cpu.stack_pointer = cpu.a;
            if (cpu.emulation) cpu.stack_pointer = static_cast<std::uint16_t>(
                0x0100U | (cpu.stack_pointer & 0x00ffU));
            break;
        case 0x9a: // TXS
            cpu.stack_pointer = cpu.x;
            if (cpu.emulation) cpu.stack_pointer = static_cast<std::uint16_t>(
                0x0100U | (cpu.stack_pointer & 0x00ffU));
            break;
        default: break;
        }
        return finish(cpu, 1, 2);
    }

    if (instruction.opcode == 0x5bU) { // TCD
        if (instruction.operand_count != 0U) {
            return reject_encoding();
        }
        cpu.direct_page = cpu.a;
        set_nz16(cpu, cpu.direct_page);
        return finish(cpu, 1, 2);
    }

    if (instruction.opcode == 0x1aU) { // INC A
        if (instruction.operand_count != 0U) {
            return reject_encoding();
        }
        if (cpu.mode().accumulator_8bit) {
            const auto value = static_cast<std::uint8_t>(cpu.a + 1U);
            cpu.a = static_cast<std::uint16_t>((cpu.a & 0xff00U) | value);
            set_nz8(cpu, value);
        } else {
            cpu.a = static_cast<std::uint16_t>(cpu.a + 1U);
            set_nz16(cpu, cpu.a);
        }
        return finish(cpu, 1, 2);
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

    LoadStoreOperation load_store_operation{};
    LoadStoreMode load_store_mode{};
    if (decode_load_store(instruction.opcode, load_store_operation, load_store_mode)) {
        const auto accumulator_width = load_store_operation == LoadStoreOperation::lda
            || load_store_operation == LoadStoreOperation::sta
            || load_store_operation == LoadStoreOperation::stz;
        const auto width8 = accumulator_width
            ? cpu.mode().accumulator_8bit : cpu.mode().index_8bit;
        const auto load = load_store_operation == LoadStoreOperation::lda
            || load_store_operation == LoadStoreOperation::ldx
            || load_store_operation == LoadStoreOperation::ldy;
        const auto immediate = load_store_mode == LoadStoreMode::immediate;
        const auto long_address = load_store_mode == LoadStoreMode::absolute_long
            || load_store_mode == LoadStoreMode::absolute_long_x;
        const auto direct = load_store_mode == LoadStoreMode::direct
            || load_store_mode == LoadStoreMode::direct_x
            || load_store_mode == LoadStoreMode::direct_y;
        const auto required = static_cast<std::uint8_t>(
            immediate ? (width8 ? 1U : 2U) : long_address ? 3U : direct ? 1U : 2U);
        if (instruction.operand_count != required) return reject_encoding();

        std::uint32_t address = 0;
        auto cycles = std::uint8_t{2};
        if (!immediate) {
            switch (load_store_mode) {
            case LoadStoreMode::direct:
                address = direct_address(cpu, instruction.operands[0]);
                cycles = static_cast<std::uint8_t>(3U
                    + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
                break;
            case LoadStoreMode::direct_x:
            case LoadStoreMode::direct_y: {
                const auto index = load_store_mode == LoadStoreMode::direct_x ? cpu.x : cpu.y;
                address = direct_address(cpu,
                    static_cast<std::uint16_t>(instruction.operands[0] + index));
                cycles = static_cast<std::uint8_t>(4U
                    + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
                break;
            }
            case LoadStoreMode::absolute:
                address = (static_cast<std::uint32_t>(cpu.data_bank) << 16U)
                    | operand16(instruction);
                cycles = 4;
                break;
            case LoadStoreMode::absolute_x:
            case LoadStoreMode::absolute_y: {
                const auto base = (static_cast<std::uint32_t>(cpu.data_bank) << 16U)
                    | operand16(instruction);
                const auto index = load_store_mode == LoadStoreMode::absolute_x ? cpu.x : cpu.y;
                address = (base + index) & 0x00ff'ffffU;
                const auto index_penalty = !load || !cpu.mode().index_8bit
                    || (base & 0x0000'ff00U) != (address & 0x0000'ff00U);
                cycles = static_cast<std::uint8_t>(4U + (index_penalty ? 1U : 0U));
                break;
            }
            case LoadStoreMode::absolute_long:
                address = operand24(instruction);
                cycles = 5;
                break;
            case LoadStoreMode::absolute_long_x:
                address = (operand24(instruction) + cpu.x) & 0x00ff'ffffU;
                cycles = 5;
                break;
            case LoadStoreMode::immediate: break;
            }
        }
        if (!width8) ++cycles;

        if (load) {
            const auto value = width8
                ? static_cast<std::uint16_t>(immediate ? instruction.operands[0]
                    : bus.read8(cpu.processor, address, BusAccessKind::data))
                : (immediate ? operand16(instruction)
                    : direct ? read16_bank_wrapped(
                        bus, cpu.processor, address, BusAccessKind::data)
                    : read16_linear24(bus, cpu.processor, address));
            if (load_store_operation == LoadStoreOperation::lda) {
                cpu.a = width8 ? static_cast<std::uint16_t>((cpu.a & 0xff00U) | value) : value;
            } else if (load_store_operation == LoadStoreOperation::ldx) {
                cpu.x = value;
            } else {
                cpu.y = value;
            }
            if (width8) set_nz8(cpu, static_cast<std::uint8_t>(value));
            else set_nz16(cpu, value);
        } else {
            std::uint16_t value = 0;
            if (load_store_operation == LoadStoreOperation::sta) value = cpu.a;
            else if (load_store_operation == LoadStoreOperation::stx) value = cpu.x;
            else if (load_store_operation == LoadStoreOperation::sty) value = cpu.y;
            if (width8) {
                bus.write8(cpu.processor, address,
                    static_cast<std::uint8_t>(value), BusAccessKind::data);
            } else if (direct) {
                write16_bank_wrapped(
                    bus, cpu.processor, address, value, BusAccessKind::data);
            } else {
                write16_linear24(bus, cpu.processor, address, value);
            }
        }
        return finish(cpu, static_cast<std::uint8_t>(required + 1U), cycles);
    }

    AccumulatorAluOperation alu_operation{};
    AccumulatorAluMode alu_mode{};
    if (decode_accumulator_alu(instruction.opcode, alu_operation, alu_mode)) {
        const auto width8 = cpu.mode().accumulator_8bit;
        const auto immediate = alu_mode == AccumulatorAluMode::immediate;
        const auto required = static_cast<std::uint8_t>(
            immediate ? (width8 ? 1U : 2U)
                      : (alu_mode == AccumulatorAluMode::absolute_long
                              || alu_mode == AccumulatorAluMode::absolute_long_x ? 3U
                          : alu_mode == AccumulatorAluMode::direct
                              || alu_mode == AccumulatorAluMode::direct_x ? 1U : 2U));
        if (instruction.operand_count != required) return reject_encoding();

        std::uint32_t address = 0;
        auto cycles = std::uint8_t{2};
        if (!immediate) {
            switch (alu_mode) {
            case AccumulatorAluMode::direct:
                address = direct_address(cpu, instruction.operands[0]);
                cycles = static_cast<std::uint8_t>(3U
                    + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
                break;
            case AccumulatorAluMode::direct_x:
                address = direct_address(cpu,
                    static_cast<std::uint16_t>(instruction.operands[0] + cpu.x));
                cycles = static_cast<std::uint8_t>(4U
                    + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
                break;
            case AccumulatorAluMode::absolute:
                address = (static_cast<std::uint32_t>(cpu.data_bank) << 16U)
                    | operand16(instruction);
                cycles = 4;
                break;
            case AccumulatorAluMode::absolute_x:
            case AccumulatorAluMode::absolute_y: {
                const auto base = (static_cast<std::uint32_t>(cpu.data_bank) << 16U)
                    | operand16(instruction);
                const auto index = alu_mode == AccumulatorAluMode::absolute_x ? cpu.x : cpu.y;
                address = (base + index) & 0x00ff'ffffU;
                cycles = static_cast<std::uint8_t>(4U
                    + ((!cpu.mode().index_8bit
                        || (base & 0x0000'ff00U) != (address & 0x0000'ff00U)) ? 1U : 0U));
                break;
            }
            case AccumulatorAluMode::absolute_long:
                address = operand24(instruction);
                cycles = 5;
                break;
            case AccumulatorAluMode::absolute_long_x:
                address = (operand24(instruction) + cpu.x) & 0x00ff'ffffU;
                cycles = 5;
                break;
            case AccumulatorAluMode::immediate: break;
            }
        }

        if (!width8) ++cycles;
        if (width8) {
            const auto right = immediate ? instruction.operands[0]
                : bus.read8(cpu.processor, address, BusAccessKind::data);
            const auto result = apply_accumulator_alu8(cpu, alu_operation, right);
            if (alu_operation != AccumulatorAluOperation::compare) {
                cpu.a = static_cast<std::uint16_t>((cpu.a & 0xff00U) | result);
            }
        } else {
            const auto right = immediate ? operand16(instruction)
                : (alu_mode == AccumulatorAluMode::direct
                    || alu_mode == AccumulatorAluMode::direct_x
                    ? read16_bank_wrapped(bus, cpu.processor, address, BusAccessKind::data)
                    : read16_linear24(bus, cpu.processor, address));
            const auto result = apply_accumulator_alu16(cpu, alu_operation, right);
            if (alu_operation != AccumulatorAluOperation::compare) cpu.a = result;
        }
        return finish(cpu, static_cast<std::uint8_t>(required + 1U), cycles);
    }

    if (instruction.opcode == 0x89U || instruction.opcode == 0x24U
        || instruction.opcode == 0x2cU || instruction.opcode == 0x34U
        || instruction.opcode == 0x3cU || instruction.opcode == 0x14U
        || instruction.opcode == 0x1cU || instruction.opcode == 0x04U
        || instruction.opcode == 0x0cU) { // BIT / TRB / TSB
        const auto immediate = instruction.opcode == 0x89U;
        const auto direct = instruction.opcode == 0x24U || instruction.opcode == 0x34U
            || instruction.opcode == 0x14U || instruction.opcode == 0x04U;
        const auto indexed = instruction.opcode == 0x34U || instruction.opcode == 0x3cU;
        const auto rmw = instruction.opcode == 0x14U || instruction.opcode == 0x1cU
            || instruction.opcode == 0x04U || instruction.opcode == 0x0cU;
        const auto width8 = cpu.mode().accumulator_8bit;
        const auto required = static_cast<std::uint8_t>(
            immediate ? (width8 ? 1U : 2U) : direct ? 1U : 2U);
        if (instruction.operand_count != required) return reject_encoding();
        std::uint32_t address = 0;
        auto cycles = std::uint8_t{2};
        if (!immediate) {
            if (direct) {
                address = direct_address(cpu, static_cast<std::uint16_t>(
                    instruction.operands[0] + (indexed ? cpu.x : 0U)));
                cycles = static_cast<std::uint8_t>((indexed ? 4U : 3U)
                    + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
            } else {
                const auto base = (static_cast<std::uint32_t>(cpu.data_bank) << 16U)
                    | operand16(instruction);
                address = indexed ? (base + cpu.x) & 0x00ff'ffffU : base;
                cycles = static_cast<std::uint8_t>(indexed ? 4U : 4U);
                if (indexed && (!cpu.mode().index_8bit
                    || (base & 0x0000'ff00U) != (address & 0x0000'ff00U))) ++cycles;
            }
        }
        if (rmw) cycles = static_cast<std::uint8_t>((direct ? 5U : 6U)
            + (direct && (cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
        if (!width8) cycles = static_cast<std::uint8_t>(cycles + (rmw ? 2U : 1U));

        if (width8) {
            const auto old_value = immediate ? instruction.operands[0]
                : bus.read8(cpu.processor, address, BusAccessKind::data);
            if (!rmw) {
                alu::bit_test8(cpu, static_cast<std::uint8_t>(cpu.a), old_value, !immediate);
            } else {
                const auto new_value = instruction.opcode == 0x14U || instruction.opcode == 0x1cU
                    ? alu::trb8(cpu, static_cast<std::uint8_t>(cpu.a), old_value)
                    : alu::tsb8(cpu, static_cast<std::uint8_t>(cpu.a), old_value);
                if (cpu.emulation) {
                    bus.write8(cpu.processor, address, old_value, BusAccessKind::data);
                }
                bus.write8(cpu.processor, address, new_value, BusAccessKind::data);
            }
        } else {
            const auto old_value = immediate ? operand16(instruction)
                : direct ? read16_bank_wrapped(bus, cpu.processor, address, BusAccessKind::data)
                         : read16_linear24(bus, cpu.processor, address);
            if (!rmw) {
                alu::bit_test16(cpu, cpu.a, old_value, !immediate);
            } else {
                const auto new_value = instruction.opcode == 0x14U || instruction.opcode == 0x1cU
                    ? alu::trb16(cpu, cpu.a, old_value) : alu::tsb16(cpu, cpu.a, old_value);
                const auto high_address = direct
                    ? (address & 0x00ff'0000U) | ((address + 1U) & 0x0000'ffffU)
                    : (address + 1U) & 0x00ff'ffffU;
                bus.write8(cpu.processor, high_address,
                    static_cast<std::uint8_t>(new_value >> 8U), BusAccessKind::data);
                bus.write8(cpu.processor, address,
                    static_cast<std::uint8_t>(new_value), BusAccessKind::data);
            }
        }
        return finish(cpu, static_cast<std::uint8_t>(required + 1U), cycles);
    }

    if (instruction.opcode == 0xe0U || instruction.opcode == 0xe4U
        || instruction.opcode == 0xecU || instruction.opcode == 0xc0U
        || instruction.opcode == 0xc4U || instruction.opcode == 0xccU) { // CPX / CPY
        const auto immediate = instruction.opcode == 0xe0U || instruction.opcode == 0xc0U;
        const auto direct = instruction.opcode == 0xe4U || instruction.opcode == 0xc4U;
        const auto width8 = cpu.mode().index_8bit;
        const auto required = static_cast<std::uint8_t>(
            immediate ? (width8 ? 1U : 2U) : direct ? 1U : 2U);
        if (instruction.operand_count != required) return reject_encoding();
        std::uint32_t address = 0;
        auto cycles = std::uint8_t{2};
        if (!immediate) {
            if (direct) {
                address = direct_address(cpu, instruction.operands[0]);
                cycles = static_cast<std::uint8_t>(3U
                    + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
            } else {
                address = (static_cast<std::uint32_t>(cpu.data_bank) << 16U)
                    | operand16(instruction);
                cycles = 4;
            }
        }
        if (!width8) ++cycles;
        const auto left = instruction.opcode >= 0xe0U ? cpu.x : cpu.y;
        if (width8) {
            const auto right = immediate ? instruction.operands[0]
                : bus.read8(cpu.processor, address, BusAccessKind::data);
            alu::compare8(cpu, static_cast<std::uint8_t>(left), right);
        } else {
            const auto right = immediate ? operand16(instruction)
                : direct ? read16_bank_wrapped(bus, cpu.processor, address, BusAccessKind::data)
                         : read16_linear24(bus, cpu.processor, address);
            alu::compare16(cpu, left, right);
        }
        return finish(cpu, static_cast<std::uint8_t>(required + 1U), cycles);
    }

    IndirectAccumulatorOperation indirect_operation{};
    IndirectAccumulatorMode indirect_mode{};
    if (decode_indirect_accumulator(
            instruction.opcode, indirect_operation, indirect_mode)) {
        if (instruction.operand_count != 1U) return reject_encoding();
        const auto width8 = cpu.mode().accumulator_8bit;
        const auto store = indirect_operation == IndirectAccumulatorOperation::store;
        const auto operand = instruction.operands[0];
        std::uint32_t address = 0;
        auto cycles = std::uint8_t{0};
        switch (indirect_mode) {
        case IndirectAccumulatorMode::direct_x_indirect: {
            const auto pointer = read_direct_pointer_word(cpu, bus,
                static_cast<std::uint16_t>(operand + cpu.x), true);
            address = (static_cast<std::uint32_t>(cpu.data_bank) << 16U) | pointer;
            cycles = static_cast<std::uint8_t>(6U
                + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
            break;
        }
        case IndirectAccumulatorMode::stack_relative:
            address = static_cast<std::uint16_t>(cpu.stack_pointer + operand);
            cycles = 4;
            break;
        case IndirectAccumulatorMode::direct_indirect_long:
            address = read_direct_pointer_long(cpu, bus, operand);
            cycles = static_cast<std::uint8_t>(6U
                + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
            break;
        case IndirectAccumulatorMode::direct_indirect_y: {
            const auto pointer = read_direct_pointer_word(cpu, bus, operand, false);
            const auto base = (static_cast<std::uint32_t>(cpu.data_bank) << 16U) | pointer;
            address = (base + cpu.y) & 0x00ff'ffffU;
            cycles = static_cast<std::uint8_t>(5U
                + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
            if (store || !cpu.mode().index_8bit
                || (base & 0x0000'ff00U) != (address & 0x0000'ff00U)) ++cycles;
            break;
        }
        case IndirectAccumulatorMode::direct_indirect: {
            const auto pointer = read_direct_pointer_word(cpu, bus, operand, false);
            address = (static_cast<std::uint32_t>(cpu.data_bank) << 16U) | pointer;
            cycles = static_cast<std::uint8_t>(5U
                + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
            break;
        }
        case IndirectAccumulatorMode::stack_relative_indirect_y: {
            const auto pointer_address = static_cast<std::uint16_t>(cpu.stack_pointer + operand);
            const auto low = bus.read8(
                cpu.processor, pointer_address, BusAccessKind::data);
            const auto high = bus.read8(cpu.processor,
                static_cast<std::uint16_t>(pointer_address + 1U), BusAccessKind::data);
            const auto pointer = static_cast<std::uint16_t>(
                low | (static_cast<std::uint16_t>(high) << 8U));
            address = (((static_cast<std::uint32_t>(cpu.data_bank) << 16U) | pointer)
                + cpu.y) & 0x00ff'ffffU;
            cycles = 7;
            break;
        }
        case IndirectAccumulatorMode::direct_indirect_long_y:
            address = (read_direct_pointer_long(cpu, bus, operand) + cpu.y)
                & 0x00ff'ffffU;
            cycles = static_cast<std::uint8_t>(6U
                + ((cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U));
            break;
        }
        if (!width8) ++cycles;

        if (store) {
            if (width8) {
                bus.write8(cpu.processor, address,
                    static_cast<std::uint8_t>(cpu.a), BusAccessKind::data);
            } else {
                write16_linear24(bus, cpu.processor, address, cpu.a);
            }
        } else if (width8) {
            const auto right = bus.read8(cpu.processor, address, BusAccessKind::data);
            const auto left = static_cast<std::uint8_t>(cpu.a);
            std::uint8_t result = left;
            switch (indirect_operation) {
            case IndirectAccumulatorOperation::bit_or: result = alu::bit_or8(cpu, left, right); break;
            case IndirectAccumulatorOperation::bit_and: result = alu::bit_and8(cpu, left, right); break;
            case IndirectAccumulatorOperation::bit_xor: result = alu::bit_xor8(cpu, left, right); break;
            case IndirectAccumulatorOperation::add: result = alu::adc8(cpu, left, right); break;
            case IndirectAccumulatorOperation::load: result = right; set_nz8(cpu, result); break;
            case IndirectAccumulatorOperation::compare: alu::compare8(cpu, left, right); break;
            case IndirectAccumulatorOperation::subtract: result = alu::sbc8(cpu, left, right); break;
            case IndirectAccumulatorOperation::store: break;
            }
            if (indirect_operation != IndirectAccumulatorOperation::compare) {
                cpu.a = static_cast<std::uint16_t>((cpu.a & 0xff00U) | result);
            }
        } else {
            const auto right = read16_linear24(bus, cpu.processor, address);
            auto result = cpu.a;
            switch (indirect_operation) {
            case IndirectAccumulatorOperation::bit_or: result = alu::bit_or16(cpu, cpu.a, right); break;
            case IndirectAccumulatorOperation::bit_and: result = alu::bit_and16(cpu, cpu.a, right); break;
            case IndirectAccumulatorOperation::bit_xor: result = alu::bit_xor16(cpu, cpu.a, right); break;
            case IndirectAccumulatorOperation::add: result = alu::adc16(cpu, cpu.a, right); break;
            case IndirectAccumulatorOperation::load: result = right; set_nz16(cpu, result); break;
            case IndirectAccumulatorOperation::compare: alu::compare16(cpu, cpu.a, right); break;
            case IndirectAccumulatorOperation::subtract: result = alu::sbc16(cpu, cpu.a, right); break;
            case IndirectAccumulatorOperation::store: break;
            }
            if (indirect_operation != IndirectAccumulatorOperation::compare) cpu.a = result;
        }
        return finish(cpu, 2, cycles);
    }

    if (instruction.opcode == 0xb7U) { // LDA [dp],Y
        if (instruction.operand_count != 1U) {
            return reject_encoding();
        }
        const auto pointer = static_cast<std::uint16_t>(
            cpu.direct_page + instruction.operands[0]);
        const auto low = bus.read8(cpu.processor, pointer, BusAccessKind::data);
        const auto high = bus.read8(cpu.processor,
            static_cast<std::uint16_t>(pointer + 1U), BusAccessKind::data);
        const auto bank = bus.read8(cpu.processor,
            static_cast<std::uint16_t>(pointer + 2U), BusAccessKind::data);
        const auto base = static_cast<std::uint32_t>(low)
            | (static_cast<std::uint32_t>(high) << 8U)
            | (static_cast<std::uint32_t>(bank) << 16U);
        const auto address = (base + cpu.y) & 0x00ff'ffffU;
        const auto width8 = cpu.mode().accumulator_8bit;
        if (width8) {
            const auto value = bus.read8(cpu.processor, address, BusAccessKind::data);
            cpu.a = static_cast<std::uint16_t>((cpu.a & 0xff00U) | value);
            set_nz8(cpu, value);
        } else {
            cpu.a = read16_bank_wrapped(bus, cpu.processor, address, BusAccessKind::data);
            set_nz16(cpu, cpu.a);
        }
        const auto direct_page_penalty = (cpu.direct_page & 0x00ffU) != 0U ? 1U : 0U;
        const auto cycles = static_cast<std::uint8_t>(
            (width8 ? 6U : 7U) + direct_page_penalty);
        return finish(cpu, 2, cycles);
    }

    if (instruction.opcode == 0xe0U) { // CPX immediate
        const auto width8 = cpu.mode().index_8bit;
        const auto required = static_cast<std::uint8_t>(width8 ? 1U : 2U);
        if (instruction.operand_count != required) {
            return reject_encoding();
        }
        if (width8) {
            alu::compare8(cpu, static_cast<std::uint8_t>(cpu.x), instruction.operands[0]);
        } else {
            alu::compare16(cpu, cpu.x, operand16(instruction));
        }
        return finish(cpu, static_cast<std::uint8_t>(required + 1U),
            static_cast<std::uint8_t>(width8 ? 2U : 3U));
    }

    if (instruction.opcode == 0x2aU) { // ROL A
        if (instruction.operand_count != 0U) {
            return reject_encoding();
        }
        if (cpu.mode().accumulator_8bit) {
            const auto value = alu::rol8(cpu, static_cast<std::uint8_t>(cpu.a));
            cpu.a = static_cast<std::uint16_t>((cpu.a & 0xff00U) | value);
        } else {
            cpu.a = alu::rol16(cpu, cpu.a);
        }
        return finish(cpu, 1, 2);
    }

    if (instruction.opcode == 0xc8U || instruction.opcode == 0xaaU
        || instruction.opcode == 0xcaU || instruction.opcode == 0xe8U
        || instruction.opcode == 0x88U) { // INY/TAX/DEX/INX/DEY
        if (instruction.operand_count != 0U) {
            return reject_encoding();
        }
        const auto width8 = cpu.mode().index_8bit;
        if (instruction.opcode == 0xc8U || instruction.opcode == 0xe8U) {
            if (width8) {
                auto& target = instruction.opcode == 0xc8U ? cpu.y : cpu.x;
                target = alu::increment8(cpu, static_cast<std::uint8_t>(target));
            } else {
                auto& target = instruction.opcode == 0xc8U ? cpu.y : cpu.x;
                target = alu::increment16(cpu, target);
            }
        } else if (instruction.opcode == 0xcaU || instruction.opcode == 0x88U) {
            auto& target = instruction.opcode == 0xcaU ? cpu.x : cpu.y;
            if (width8) {
                target = alu::decrement8(cpu, static_cast<std::uint8_t>(target));
            } else {
                target = alu::decrement16(cpu, target);
            }
        } else if (width8) {
            cpu.x = static_cast<std::uint8_t>(cpu.a);
            set_nz8(cpu, static_cast<std::uint8_t>(cpu.x));
        } else {
            cpu.x = cpu.a;
            set_nz16(cpu, cpu.x);
        }
        return finish(cpu, 1, 2);
    }

    if (instruction.opcode == 0xebU) { // XBA
        if (instruction.operand_count != 0U) {
            return reject_encoding();
        }
        cpu.a = static_cast<std::uint16_t>((cpu.a << 8U) | (cpu.a >> 8U));
        // XBA always derives N/Z from the new low byte, independent of M.
        set_nz8(cpu, static_cast<std::uint8_t>(cpu.a));
        return finish(cpu, 1, 3);
    }

    if (instruction.opcode == 0x54U || instruction.opcode == 0x44U) { // MVN / MVP
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

        const auto delta = instruction.opcode == 0x54U ? 1U : 0xffffU;
        cpu.x = static_cast<std::uint16_t>(cpu.x + delta);
        cpu.y = static_cast<std::uint16_t>(cpu.y + delta);
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

    if (instruction.opcode == 0x10U || instruction.opcode == 0x30U
        || instruction.opcode == 0x50U || instruction.opcode == 0x70U
        || instruction.opcode == 0x80U || instruction.opcode == 0x90U
        || instruction.opcode == 0xb0U || instruction.opcode == 0xd0U
        || instruction.opcode == 0xf0U) {
        bool taken = true; // BRA
        switch (instruction.opcode) {
        case 0x10: taken = !cpu.flag(StatusFlag::negative); break; // BPL
        case 0x30: taken = cpu.flag(StatusFlag::negative); break;  // BMI
        case 0x50: taken = !cpu.flag(StatusFlag::overflow); break; // BVC
        case 0x70: taken = cpu.flag(StatusFlag::overflow); break;  // BVS
        case 0x90: taken = !cpu.flag(StatusFlag::carry); break;    // BCC
        case 0xb0: taken = cpu.flag(StatusFlag::carry); break;     // BCS
        case 0xd0: taken = !cpu.flag(StatusFlag::zero); break;     // BNE
        case 0xf0: taken = cpu.flag(StatusFlag::zero); break;      // BEQ
        default: break;
        }
        return branch_relative8(cpu, instruction, taken);
    }

    if (instruction.opcode == 0x82U) { // BRL rel16
        if (instruction.operand_count != 2U) return reject_encoding();
        const auto displacement = static_cast<std::int16_t>(operand16(instruction));
        cpu.pc = static_cast<std::uint16_t>(cpu.pc + 3U + displacement);
        cpu.cycles += 4U;
        return {LiftStatus::executed, 3, 4};
    }

    if (instruction.opcode == 0x20U) { // JSR abs
        if (instruction.operand_count != 2U) {
            return reject_encoding();
        }
        const auto return_address = static_cast<std::uint16_t>(cpu.pc + 2U);
        push8(cpu, bus, static_cast<std::uint8_t>(return_address >> 8U));
        push8(cpu, bus, static_cast<std::uint8_t>(return_address));
        cpu.pc = operand16(instruction);
        cpu.cycles += 6U;
        return {LiftStatus::executed, 3, 6};
    }

    if (instruction.opcode == 0x22U) { // JSL abs long
        if (instruction.operand_count != 3U) {
            return reject_encoding();
        }
        const auto target = operand24(instruction);
        const auto return_address = static_cast<std::uint16_t>(cpu.pc + 3U);
        push8(cpu, bus, cpu.program_bank);
        push8(cpu, bus, static_cast<std::uint8_t>(return_address >> 8U));
        push8(cpu, bus, static_cast<std::uint8_t>(return_address));
        cpu.program_bank = static_cast<std::uint8_t>(target >> 16U);
        cpu.pc = static_cast<std::uint16_t>(target);
        cpu.cycles += 8U;
        return {LiftStatus::executed, 4, 8};
    }

    if (instruction.opcode == 0x40U) { // RTI
        if (instruction.operand_count != 0U) return reject_encoding();
        cpu.status = pop8(cpu, bus);
        const auto low = pop8(cpu, bus);
        const auto high = pop8(cpu, bus);
        cpu.pc = static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
        auto cycles = std::uint8_t{6};
        if (!cpu.emulation) {
            cpu.program_bank = pop8(cpu, bus);
            cycles = 7;
        }
        cpu.normalize_after_mode_change();
        cpu.cycles += cycles;
        return {LiftStatus::executed, 1, cycles};
    }

    if (instruction.opcode == 0x60U || instruction.opcode == 0x6bU) { // RTS/RTL
        if (instruction.operand_count != 0U) return reject_encoding();
        const auto low = pop8(cpu, bus);
        const auto high = pop8(cpu, bus);
        cpu.pc = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U)) + 1U);
        if (instruction.opcode == 0x6bU) cpu.program_bank = pop8(cpu, bus);
        cpu.cycles += 6U;
        return {LiftStatus::executed, 1, 6};
    }

    if (instruction.opcode == 0x5cU) { // JML abs long
        if (instruction.operand_count != 3U) return reject_encoding();
        const auto target = operand24(instruction);
        cpu.program_bank = static_cast<std::uint8_t>(target >> 16U);
        cpu.pc = static_cast<std::uint16_t>(target);
        cpu.cycles += 4U;
        return {LiftStatus::executed, 4, 4};
    }

    if (instruction.opcode == 0x6cU || instruction.opcode == 0x7cU
        || instruction.opcode == 0xdcU || instruction.opcode == 0xfcU) {
        if (instruction.operand_count != 2U) return reject_encoding();
        const auto indexed = instruction.opcode == 0x7cU || instruction.opcode == 0xfcU;
        const auto pointer_offset = static_cast<std::uint16_t>(
            operand16(instruction) + (indexed ? cpu.x : 0U));
        const auto pointer_bank = indexed ? static_cast<std::uint32_t>(cpu.program_bank) << 16U : 0U;
        const auto pointer = pointer_bank | pointer_offset;
        const auto low = bus.read8(cpu.processor, pointer, BusAccessKind::data);
        const auto high = bus.read8(cpu.processor,
            pointer_bank | static_cast<std::uint16_t>(pointer_offset + 1U),
            BusAccessKind::data);
        const auto target_pc = static_cast<std::uint16_t>(
            low | (static_cast<std::uint16_t>(high) << 8U));
        if (instruction.opcode == 0xdcU) { // JML [abs]
            const auto bank = bus.read8(cpu.processor,
                static_cast<std::uint16_t>(pointer_offset + 2U), BusAccessKind::data);
            cpu.program_bank = bank;
            cpu.pc = target_pc;
            cpu.cycles += 6U;
            return {LiftStatus::executed, 3, 6};
        }
        if (instruction.opcode == 0xfcU) { // JSR (abs,X)
            const auto return_address = static_cast<std::uint16_t>(cpu.pc + 2U);
            push8(cpu, bus, static_cast<std::uint8_t>(return_address >> 8U));
            push8(cpu, bus, static_cast<std::uint8_t>(return_address));
            cpu.pc = target_pc;
            cpu.cycles += 8U;
            return {LiftStatus::executed, 3, 8};
        }
        cpu.pc = target_pc; // JMP (abs) / JMP (abs,X)
        const auto cycles = static_cast<std::uint8_t>(instruction.opcode == 0x7cU ? 6U : 5U);
        cpu.cycles += cycles;
        return {LiftStatus::executed, 3, cycles};
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

CpuAsyncResult service_lifted_async_signal(
    CpuContext& cpu,
    Bus& bus,
    CpuAsyncSignal signal) noexcept {
    if (signal == CpuAsyncSignal::reset) {
        const auto low = bus.read8(cpu.processor, 0x00fffcU, BusAccessKind::vector);
        const auto high = bus.read8(cpu.processor, 0x00fffdU, BusAccessKind::vector);
        cpu.emulation = true;
        cpu.set_flag(StatusFlag::irq_disable, true);
        cpu.set_flag(StatusFlag::decimal, false);
        cpu.set_flag(StatusFlag::accumulator_width, true);
        cpu.set_flag(StatusFlag::index_width, true);
        cpu.data_bank = 0;
        cpu.direct_page = 0;
        cpu.program_bank = 0;
        cpu.pc = static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
        cpu.waiting = false;
        cpu.stopped = false;
        cpu.irq_pending = false;
        cpu.nmi_pending = false;
        cpu.normalize_after_mode_change();
        // Reset is an external machine transition. The caller owns reset-line
        // timing, so no instruction-entry cycles are charged here.
        return {CpuAsyncStatus::serviced, 0};
    }

    if (cpu.stopped) return {CpuAsyncStatus::ignored_stopped, 0};

    if (signal == CpuAsyncSignal::irq) {
        cpu.irq_pending = true;
        if (cpu.flag(StatusFlag::irq_disable)) {
            if (cpu.waiting) {
                cpu.waiting = false;
                return {CpuAsyncStatus::woke_masked, 0};
            }
            return {CpuAsyncStatus::masked, 0};
        }
    } else {
        cpu.nmi_pending = true;
    }

    cpu.waiting = false;
    const auto emulation = cpu.emulation;
    if (!emulation) push8(cpu, bus, cpu.program_bank);
    push8(cpu, bus, static_cast<std::uint8_t>(cpu.pc >> 8U));
    push8(cpu, bus, static_cast<std::uint8_t>(cpu.pc));
    push8(cpu, bus, emulation
        ? static_cast<std::uint8_t>(cpu.status | 0x30U) : cpu.status);
    cpu.set_flag(StatusFlag::irq_disable, true);
    cpu.set_flag(StatusFlag::decimal, false);
    cpu.program_bank = 0;
    const auto vector = static_cast<std::uint16_t>(signal == CpuAsyncSignal::nmi
        ? (emulation ? 0xfffaU : 0xffeaU)
        : (emulation ? 0xfffeU : 0xffeeU));
    const auto low = bus.read8(cpu.processor, vector, BusAccessKind::vector);
    const auto high = bus.read8(cpu.processor,
        static_cast<std::uint16_t>(vector + 1U), BusAccessKind::vector);
    cpu.pc = static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
    if (signal == CpuAsyncSignal::nmi) cpu.nmi_pending = false;
    else cpu.irq_pending = false;
    const auto cycles = static_cast<std::uint8_t>(emulation ? 7U : 8U);
    cpu.cycles += cycles;
    return {CpuAsyncStatus::serviced, cycles};
}

} // namespace kss
