#include "kss/alu.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace kss::alu {
namespace {

template <typename T>
constexpr T sign_mask = static_cast<T>(T{1} << (std::numeric_limits<T>::digits - 1));

template <typename T>
void set_nz(CpuContext& cpu, T result) noexcept {
    cpu.set_flag(StatusFlag::zero, result == 0);
    cpu.set_flag(StatusFlag::negative, (result & sign_mask<T>) != 0);
}

template <typename T>
T adc_binary(CpuContext& cpu, T left, T right) noexcept {
    using Wide = std::conditional_t<sizeof(T) == 1, std::uint16_t, std::uint32_t>;
    const auto carry = cpu.flag(StatusFlag::carry) ? Wide{1} : Wide{0};
    const auto sum = static_cast<Wide>(left) + static_cast<Wide>(right) + carry;
    const auto result = static_cast<T>(sum);
    cpu.set_flag(StatusFlag::carry, sum > std::numeric_limits<T>::max());
    cpu.set_flag(
        StatusFlag::overflow,
        ((~(left ^ right) & (left ^ result)) & sign_mask<T>) != 0);
    set_nz(cpu, result);
    return result;
}

template <typename T>
T adc_decimal(CpuContext& cpu, T left, T right) noexcept {
    using Wide = std::conditional_t<sizeof(T) == 1, std::uint16_t, std::uint32_t>;
    const auto carry = cpu.flag(StatusFlag::carry) ? Wide{1} : Wide{0};
    auto adjusted = static_cast<Wide>(left) + static_cast<Wide>(right) + carry;
    const auto binary_result = static_cast<T>(adjusted);
    cpu.set_flag(
        StatusFlag::overflow,
        ((~(left ^ right) & (left ^ binary_result)) & sign_mask<T>) != 0);

    if ((left & 0x000fU) + (right & 0x000fU) + carry > 0x0009U) {
        adjusted += 0x0006U;
    }
    if constexpr (sizeof(T) == 2) {
        if ((adjusted & 0x00ffU) > 0x009fU) {
            adjusted += 0x0060U;
        }
        if ((adjusted & 0x0fffU) > 0x09ffU) {
            adjusted += 0x0600U;
        }
        if (adjusted > 0x9fffU) {
            adjusted += 0x6000U;
        }
    } else if (adjusted > static_cast<Wide>(0x009fU)) {
        adjusted += 0x0060U;
    }

    cpu.set_flag(StatusFlag::carry, adjusted > std::numeric_limits<T>::max());
    const auto result = static_cast<T>(adjusted);
    set_nz(cpu, result);
    return result;
}

template <typename T>
T sbc_binary(CpuContext& cpu, T left, T right) noexcept {
    using Wide = std::conditional_t<sizeof(T) == 1, std::uint16_t, std::uint32_t>;
    const auto borrow = cpu.flag(StatusFlag::carry) ? Wide{0} : Wide{1};
    const auto subtrahend = static_cast<Wide>(right) + borrow;
    const auto result = static_cast<T>(static_cast<Wide>(left) - subtrahend);
    cpu.set_flag(StatusFlag::carry, static_cast<Wide>(left) >= subtrahend);
    cpu.set_flag(
        StatusFlag::overflow,
        (((left ^ right) & (left ^ result)) & sign_mask<T>) != 0);
    set_nz(cpu, result);
    return result;
}

template <typename T>
T sbc_decimal(CpuContext& cpu, T left, T right) noexcept {
    using SignedWide = std::conditional_t<sizeof(T) == 1, std::int16_t, std::int32_t>;
    using UnsignedWide = std::make_unsigned_t<SignedWide>;
    const auto borrow = cpu.flag(StatusFlag::carry) ? SignedWide{0} : SignedWide{1};
    auto adjusted = static_cast<SignedWide>(left) - static_cast<SignedWide>(right) - borrow;
    const auto binary_result = static_cast<T>(adjusted);
    cpu.set_flag(
        StatusFlag::overflow,
        (((left ^ right) & (left ^ binary_result)) & sign_mask<T>) != 0);

    if (static_cast<SignedWide>(left & 0x000fU) - borrow
        < static_cast<SignedWide>(right & 0x000fU)) {
        adjusted -= 0x0006;
    }
    if constexpr (sizeof(T) == 2) {
        if (static_cast<SignedWide>(left & 0x00ffU) - borrow
            < static_cast<SignedWide>(right & 0x00ffU)) {
            adjusted -= 0x0060;
        }
        if (static_cast<SignedWide>(left & 0x0fffU) - borrow
            < static_cast<SignedWide>(right & 0x0fffU)) {
            adjusted -= 0x0600;
        }
        if (static_cast<SignedWide>(left) - borrow < static_cast<SignedWide>(right)) {
            adjusted -= 0x6000;
        }
    } else if (static_cast<SignedWide>(left) - borrow < static_cast<SignedWide>(right)) {
        adjusted -= 0x0060;
    }

    const auto subtrahend = static_cast<UnsignedWide>(right) + static_cast<UnsignedWide>(borrow);
    cpu.set_flag(StatusFlag::carry, static_cast<UnsignedWide>(left) >= subtrahend);
    const auto result = static_cast<T>(adjusted);
    set_nz(cpu, result);
    return result;
}

template <typename T, typename Operation>
T logical(CpuContext& cpu, T left, T right, Operation operation) noexcept {
    const auto result = static_cast<T>(operation(left, right));
    set_nz(cpu, result);
    return result;
}

template <typename T>
void compare(CpuContext& cpu, T left, T right) noexcept {
    const auto result = static_cast<T>(left - right);
    cpu.set_flag(StatusFlag::carry, left >= right);
    set_nz(cpu, result);
}

template <typename T>
T shift_left(CpuContext& cpu, T value, bool rotate) noexcept {
    const auto carry_in = rotate && cpu.flag(StatusFlag::carry) ? T{1} : T{0};
    cpu.set_flag(StatusFlag::carry, (value & sign_mask<T>) != 0);
    const auto result = static_cast<T>((value << 1U) | carry_in);
    set_nz(cpu, result);
    return result;
}

template <typename T>
T shift_right(CpuContext& cpu, T value, bool rotate) noexcept {
    const auto carry_in = rotate && cpu.flag(StatusFlag::carry) ? sign_mask<T> : T{0};
    cpu.set_flag(StatusFlag::carry, (value & T{1}) != 0);
    const auto result = static_cast<T>((value >> 1U) | carry_in);
    set_nz(cpu, result);
    return result;
}

template <typename T>
T increment(CpuContext& cpu, T value) noexcept {
    const auto result = static_cast<T>(value + T{1});
    set_nz(cpu, result);
    return result;
}

template <typename T>
T decrement(CpuContext& cpu, T value) noexcept {
    const auto result = static_cast<T>(value - T{1});
    set_nz(cpu, result);
    return result;
}

template <typename T>
void bit_test(CpuContext& cpu, T accumulator, T operand, bool memory_form) noexcept {
    cpu.set_flag(StatusFlag::zero, (accumulator & operand) == 0);
    if (memory_form) {
        cpu.set_flag(StatusFlag::negative, (operand & sign_mask<T>) != 0);
        cpu.set_flag(StatusFlag::overflow, (operand & static_cast<T>(sign_mask<T> >> 1U)) != 0);
    }
}

template <typename T>
T trb(CpuContext& cpu, T accumulator, T memory) noexcept {
    cpu.set_flag(StatusFlag::zero, (accumulator & memory) == 0);
    return static_cast<T>(memory & static_cast<T>(~accumulator));
}

template <typename T>
T tsb(CpuContext& cpu, T accumulator, T memory) noexcept {
    cpu.set_flag(StatusFlag::zero, (accumulator & memory) == 0);
    return static_cast<T>(memory | accumulator);
}

} // namespace

std::uint8_t adc8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept { return cpu.flag(StatusFlag::decimal) ? adc_decimal(cpu, left, right) : adc_binary(cpu, left, right); }
std::uint16_t adc16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept { return cpu.flag(StatusFlag::decimal) ? adc_decimal(cpu, left, right) : adc_binary(cpu, left, right); }
std::uint8_t sbc8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept { return cpu.flag(StatusFlag::decimal) ? sbc_decimal(cpu, left, right) : sbc_binary(cpu, left, right); }
std::uint16_t sbc16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept { return cpu.flag(StatusFlag::decimal) ? sbc_decimal(cpu, left, right) : sbc_binary(cpu, left, right); }

std::uint8_t bit_and8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept { return logical(cpu, left, right, [](auto a, auto b) { return a & b; }); }
std::uint16_t bit_and16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept { return logical(cpu, left, right, [](auto a, auto b) { return a & b; }); }
std::uint8_t bit_or8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept { return logical(cpu, left, right, [](auto a, auto b) { return a | b; }); }
std::uint16_t bit_or16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept { return logical(cpu, left, right, [](auto a, auto b) { return a | b; }); }
std::uint8_t bit_xor8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept { return logical(cpu, left, right, [](auto a, auto b) { return a ^ b; }); }
std::uint16_t bit_xor16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept { return logical(cpu, left, right, [](auto a, auto b) { return a ^ b; }); }

void compare8(CpuContext& cpu, std::uint8_t left, std::uint8_t right) noexcept { compare(cpu, left, right); }
void compare16(CpuContext& cpu, std::uint16_t left, std::uint16_t right) noexcept { compare(cpu, left, right); }

std::uint8_t asl8(CpuContext& cpu, std::uint8_t value) noexcept { return shift_left(cpu, value, false); }
std::uint16_t asl16(CpuContext& cpu, std::uint16_t value) noexcept { return shift_left(cpu, value, false); }
std::uint8_t lsr8(CpuContext& cpu, std::uint8_t value) noexcept { return shift_right(cpu, value, false); }
std::uint16_t lsr16(CpuContext& cpu, std::uint16_t value) noexcept { return shift_right(cpu, value, false); }
std::uint8_t rol8(CpuContext& cpu, std::uint8_t value) noexcept { return shift_left(cpu, value, true); }
std::uint16_t rol16(CpuContext& cpu, std::uint16_t value) noexcept { return shift_left(cpu, value, true); }
std::uint8_t ror8(CpuContext& cpu, std::uint8_t value) noexcept { return shift_right(cpu, value, true); }
std::uint16_t ror16(CpuContext& cpu, std::uint16_t value) noexcept { return shift_right(cpu, value, true); }

std::uint8_t increment8(CpuContext& cpu, std::uint8_t value) noexcept { return increment(cpu, value); }
std::uint16_t increment16(CpuContext& cpu, std::uint16_t value) noexcept { return increment(cpu, value); }
std::uint8_t decrement8(CpuContext& cpu, std::uint8_t value) noexcept { return decrement(cpu, value); }
std::uint16_t decrement16(CpuContext& cpu, std::uint16_t value) noexcept { return decrement(cpu, value); }

void bit_test8(CpuContext& cpu, std::uint8_t accumulator, std::uint8_t operand, bool memory_form) noexcept { bit_test(cpu, accumulator, operand, memory_form); }
void bit_test16(CpuContext& cpu, std::uint16_t accumulator, std::uint16_t operand, bool memory_form) noexcept { bit_test(cpu, accumulator, operand, memory_form); }
std::uint8_t trb8(CpuContext& cpu, std::uint8_t accumulator, std::uint8_t memory) noexcept { return trb(cpu, accumulator, memory); }
std::uint16_t trb16(CpuContext& cpu, std::uint16_t accumulator, std::uint16_t memory) noexcept { return trb(cpu, accumulator, memory); }
std::uint8_t tsb8(CpuContext& cpu, std::uint8_t accumulator, std::uint8_t memory) noexcept { return tsb(cpu, accumulator, memory); }
std::uint16_t tsb16(CpuContext& cpu, std::uint16_t accumulator, std::uint16_t memory) noexcept { return tsb(cpu, accumulator, memory); }

} // namespace kss::alu
