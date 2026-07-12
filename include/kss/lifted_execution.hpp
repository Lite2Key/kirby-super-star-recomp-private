#pragma once

#include "kss/bus.hpp"
#include "kss/cpu.hpp"

#include <array>
#include <cstdint>

namespace kss {

// A decoded instruction is deliberately independent of the source ROM.  The
// recompiler supplies opcode/operand bytes; this layer owns architectural
// state transitions and rejects malformed or not-yet-lifted instructions.
struct LiftedInstruction {
    std::uint8_t opcode{};
    std::array<std::uint8_t, 3> operands{};
    std::uint8_t operand_count{};
};

enum class LiftStatus : std::uint8_t {
    executed,
    unsupported_opcode,
    invalid_encoding,
};

struct LiftResult {
    LiftStatus status{LiftStatus::unsupported_opcode};
    std::uint8_t instruction_bytes{};
    std::uint8_t instruction_cycles{};
};

[[nodiscard]] LiftResult execute_lifted(
    CpuContext& cpu,
    Bus& bus,
    const LiftedInstruction& instruction) noexcept;

} // namespace kss
