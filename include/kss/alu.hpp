#pragma once

#include "kss/cpu.hpp"

#include <cstdint>

namespace kss::alu {

[[nodiscard]] std::uint8_t adc8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept;
[[nodiscard]] std::uint16_t adc16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept;
[[nodiscard]] std::uint8_t sbc8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept;
[[nodiscard]] std::uint16_t sbc16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept;

[[nodiscard]] std::uint8_t bit_and8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept;
[[nodiscard]] std::uint16_t bit_and16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept;
[[nodiscard]] std::uint8_t bit_or8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept;
[[nodiscard]] std::uint16_t bit_or16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept;
[[nodiscard]] std::uint8_t bit_xor8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept;
[[nodiscard]] std::uint16_t bit_xor16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept;

void compare8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept;
void compare16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept;

[[nodiscard]] std::uint8_t asl8(CpuContext& cpu, std::uint8_t value) noexcept;
[[nodiscard]] std::uint16_t asl16(CpuContext& cpu, std::uint16_t value) noexcept;
[[nodiscard]] std::uint8_t lsr8(CpuContext& cpu, std::uint8_t value) noexcept;
[[nodiscard]] std::uint16_t lsr16(CpuContext& cpu, std::uint16_t value) noexcept;
[[nodiscard]] std::uint8_t rol8(CpuContext& cpu, std::uint8_t value) noexcept;
[[nodiscard]] std::uint16_t rol16(CpuContext& cpu, std::uint16_t value) noexcept;
[[nodiscard]] std::uint8_t ror8(CpuContext& cpu, std::uint8_t value) noexcept;
[[nodiscard]] std::uint16_t ror16(CpuContext& cpu, std::uint16_t value) noexcept;

[[nodiscard]] std::uint8_t increment8(CpuContext& cpu, std::uint8_t value) noexcept;
[[nodiscard]] std::uint16_t increment16(CpuContext& cpu, std::uint16_t value) noexcept;
[[nodiscard]] std::uint8_t decrement8(CpuContext& cpu, std::uint8_t value) noexcept;
[[nodiscard]] std::uint16_t decrement16(CpuContext& cpu, std::uint16_t value) noexcept;

// Memory-form BIT copies N/V from the operand. Immediate-form BIT changes Z only.
void bit_test8(CpuContext& cpu, std::uint8_t accumulator, std::uint8_t operand, bool memory_form) noexcept;
void bit_test16(CpuContext& cpu, std::uint16_t accumulator, std::uint16_t operand, bool memory_form) noexcept;
[[nodiscard]] std::uint8_t trb8(CpuContext& cpu, std::uint8_t accumulator, std::uint8_t memory) noexcept;
[[nodiscard]] std::uint16_t trb16(CpuContext& cpu, std::uint16_t accumulator, std::uint16_t memory) noexcept;
[[nodiscard]] std::uint8_t tsb8(CpuContext& cpu, std::uint8_t accumulator, std::uint8_t memory) noexcept;
[[nodiscard]] std::uint16_t tsb16(CpuContext& cpu, std::uint16_t accumulator, std::uint16_t memory) noexcept;

} // namespace kss::alu
