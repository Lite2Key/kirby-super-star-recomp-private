#pragma once

#include "kss/cpu.hpp"

#include <cstdint>

namespace kss {

enum class BusAccessKind : std::uint8_t {
    opcode,
    operand,
    data,
    stack,
    vector,
    dma,
};

class Bus {
public:
    virtual ~Bus() = default;

    [[nodiscard]] virtual std::uint8_t read8(
        ProcessorId processor,
        std::uint32_t address,
        BusAccessKind kind = BusAccessKind::data) = 0;

    virtual void write8(
        ProcessorId processor,
        std::uint32_t address,
        std::uint8_t value,
        BusAccessKind kind = BusAccessKind::data) = 0;

    // Generated instructions carry decoded bytes and therefore do not read
    // those bytes through read8(). Timing observers use this value-free hook
    // to retain the real opcode/operand fetch addresses in execution order.
    virtual void observe_generated_fetch(
        ProcessorId, std::uint32_t, BusAccessKind) noexcept {}
};

inline void observe_generated_instruction_fetches(
    Bus& bus,
    ProcessorId processor,
    std::uint32_t instruction_address,
    std::uint8_t operand_count) noexcept {
    const auto bank = instruction_address & 0x00ff'0000U;
    const auto pc = instruction_address & 0x0000'ffffU;
    bus.observe_generated_fetch(processor, bank | pc, BusAccessKind::opcode);
    for (std::uint32_t index = 1; index <= operand_count; ++index) {
        bus.observe_generated_fetch(
            processor, bank | ((pc + index) & 0xffffU), BusAccessKind::operand);
    }
}

[[nodiscard]] inline std::uint16_t read16_bank_wrapped(
    Bus& bus,
    ProcessorId processor,
    std::uint32_t address,
    BusAccessKind kind = BusAccessKind::data) {
    const auto bank = address & 0x00ff'0000U;
    const auto next = bank | ((address + 1U) & 0x0000'ffffU);
    const auto low = bus.read8(processor, address & 0x00ff'ffffU, kind);
    const auto high = bus.read8(processor, next, kind);
    return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
}

inline void write16_bank_wrapped(
    Bus& bus,
    ProcessorId processor,
    std::uint32_t address,
    std::uint16_t value,
    BusAccessKind kind = BusAccessKind::data) {
    const auto bank = address & 0x00ff'0000U;
    const auto next = bank | ((address + 1U) & 0x0000'ffffU);
    bus.write8(processor, address & 0x00ff'ffffU, static_cast<std::uint8_t>(value), kind);
    bus.write8(processor, next, static_cast<std::uint8_t>(value >> 8U), kind);
}

} // namespace kss
