#include "kss/sa1_timing.hpp"

#include <array>

namespace kss {
namespace {

// Derived from the private Mesen differential capture.  This table contains
// no ROM bytes beyond the standard 65C816 opcode identifiers needed to select
// timing behavior, and no operands or copyrighted payload data.
constexpr std::array kResetPrefix{
    // PC       opcode region                           data   base wait total
    Sa1ResetTiming{0x008bf4U, 0x78U, MemoryRegion::rom, false, 2, 0}, // 2
    Sa1ResetTiming{0x008bf5U, 0x18U, MemoryRegion::rom, false, 2, 1}, // 3
    Sa1ResetTiming{0x008bf6U, 0xfbU, MemoryRegion::rom, false, 2, 1}, // 3
    Sa1ResetTiming{0x008bf7U, 0xe2U, MemoryRegion::rom, false, 3, 2}, // 5
    Sa1ResetTiming{0x008bf9U, 0x9cU, MemoryRegion::hardware_register, true, 4, 4}, // 8
    Sa1ResetTiming{0x008bfcU, 0x9cU, MemoryRegion::hardware_register, true, 4, 3}, // 7
    Sa1ResetTiming{0x008bffU, 0xa9U, MemoryRegion::rom, false, 2, 0}, // 2
    Sa1ResetTiming{0x008c01U, 0x8dU, MemoryRegion::hardware_register, true, 4, 0}, // 4
    Sa1ResetTiming{0x008c04U, 0x9cU, MemoryRegion::hardware_register, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c07U, 0xa9U, MemoryRegion::rom, false, 2, 1}, // 3
    Sa1ResetTiming{0x008c09U, 0x8dU, MemoryRegion::hardware_register, true, 4, 4}, // 8
    Sa1ResetTiming{0x008c0cU, 0x8dU, MemoryRegion::hardware_register, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c0fU, 0x9cU, MemoryRegion::sa1_iram, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c12U, 0x9cU, MemoryRegion::bwram, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c15U, 0xc2U, MemoryRegion::rom, false, 3, 2}, // 5
    Sa1ResetTiming{0x008c17U, 0xa2U, MemoryRegion::rom, false, 3, 3}, // 6
    Sa1ResetTiming{0x008c1aU, 0xa0U, MemoryRegion::rom, false, 3, 3}, // 6
    Sa1ResetTiming{0x008c1dU, 0xa9U, MemoryRegion::rom, false, 3, 3}, // 6
};

// Adjacent execution timestamps in first-frame-dual.json prove these exact
// instruction totals.  Entries through $8C55 are sufficient to establish the
// first $8C58 identity timestamp.  The long $8C2C MVN is represented by its
// separate aggregate below, never by a guessed per-iteration entry here.
constexpr std::array kPostResetPrefix{
    // PC       opcode region             data   base wait total
    Sa1ResetTiming{0x008c23U, 0xa2U, MemoryRegion::rom, false, 3, 2}, // 5
    Sa1ResetTiming{0x008c26U, 0xa0U, MemoryRegion::rom, false, 3, 3}, // 6
    Sa1ResetTiming{0x008c29U, 0xa9U, MemoryRegion::rom, false, 3, 3}, // 6
    Sa1ResetTiming{0x008c2fU, 0xa2U, MemoryRegion::rom, false, 3, 3}, // 6
    Sa1ResetTiming{0x008c32U, 0x9aU, MemoryRegion::rom, false, 2, 1}, // 3
    Sa1ResetTiming{0x008c33U, 0xa9U, MemoryRegion::rom, false, 3, 3}, // 6
    Sa1ResetTiming{0x008c36U, 0x5bU, MemoryRegion::rom, false, 2, 0}, // 2
    Sa1ResetTiming{0x008c37U, 0xa9U, MemoryRegion::rom, false, 3, 0}, // 3
    Sa1ResetTiming{0x008c3aU, 0x8dU, MemoryRegion::sa1_iram, true, 4, 2}, // 6
    Sa1ResetTiming{0x008c3dU, 0xa9U, MemoryRegion::rom, false, 3, 2}, // 5
    Sa1ResetTiming{0x008c40U, 0x8dU, MemoryRegion::sa1_iram, true, 4, 5}, // 9
    Sa1ResetTiming{0x008c43U, 0xa9U, MemoryRegion::rom, false, 3, 2}, // 5
    Sa1ResetTiming{0x008c46U, 0x8dU, MemoryRegion::sa1_iram, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c49U, 0xa9U, MemoryRegion::rom, false, 3, 1}, // 4
    Sa1ResetTiming{0x008c4cU, 0x8dU, MemoryRegion::sa1_iram, true, 4, 2}, // 6
    Sa1ResetTiming{0x008c4fU, 0x8dU, MemoryRegion::sa1_iram, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c52U, 0x8dU, MemoryRegion::sa1_iram, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c55U, 0x8dU, MemoryRegion::sa1_iram, true, 4, 4}, // 8
};

} // namespace

std::optional<Sa1ResetTiming> lookup_sa1_reset_timing(
    std::uint32_t pc,
    std::uint8_t opcode) noexcept {
    pc &= 0x00ff'ffffU;
    for (const auto& timing : kResetPrefix) {
        if (timing.pc == pc && timing.opcode == opcode) {
            return timing;
        }
    }
    return std::nullopt;
}

std::optional<Sa1ResetBlockMoveTiming> lookup_sa1_reset_block_move_timing(
    std::uint32_t pc,
    std::uint8_t opcode,
    std::uint32_t iterations) noexcept {
    // The private Mesen reference entered the loop at cycle 1550 and reached
    // its exit instruction at cycle 22011 after 2047 iterations.  Architectural
    // MVN timing accounts for 2047 * 7 cycles; the remainder is measured SA-1
    // bus arbitration for this exact run, not a reusable wait-state formula.
    constexpr Sa1ResetBlockMoveTiming measured{
        0x008c20U, 0x54U, 2047U, 2047U * 7U, 6132U};
    if ((pc & 0x00ff'ffffU) == measured.pc && opcode == measured.opcode
        && iterations == measured.iterations) {
        return measured;
    }
    return std::nullopt;
}

std::optional<std::uint8_t> lookup_sa1_reset_mvn_wait(
    std::uint32_t pc,
    std::uint8_t opcode,
    std::uint16_t a_before,
    std::uint16_t x_before,
    std::uint16_t y_before) noexcept {
    if ((pc & 0x00ff'ffffU) != 0x008c20U || opcode != 0x54U || a_before > 0x07feU) {
        return std::nullopt;
    }
    const auto iteration = static_cast<std::uint16_t>(0x07feU - a_before);
    if (x_before != static_cast<std::uint16_t>(0x3000U + iteration)
        || y_before != static_cast<std::uint16_t>(0x3001U + iteration)) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(
        iteration == 0U || iteration == 2U || iteration == 12U ? 0U : 3U);
}

std::optional<Sa1ResetTiming> lookup_sa1_post_reset_timing(
    std::uint32_t pc,
    std::uint8_t opcode) noexcept {
    pc &= 0x00ff'ffffU;
    for (const auto& timing : kPostResetPrefix) {
        if (timing.pc == pc && timing.opcode == opcode) return timing;
    }
    return std::nullopt;
}

std::optional<Sa1ResetBlockMoveTiming> lookup_sa1_post_reset_block_move_timing(
    std::uint32_t pc,
    std::uint8_t opcode,
    std::uint32_t iterations) noexcept {
    // Reference identities enter at cycle 22028 and reach $8C2F at 112748:
    // 7935 * 7 architectural cycles plus 35175 aggregate arbitration cycles.
    constexpr Sa1ResetBlockMoveTiming measured{
        0x008c2cU, 0x54U, 7935U, 7935U * 7U, 35175U};
    if ((pc & 0x00ff'ffffU) == measured.pc && opcode == measured.opcode
        && iterations == measured.iterations) {
        return measured;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> lookup_sa1_post_reset_mvn_completion_wait(
    std::uint32_t pc,
    std::uint8_t opcode,
    std::uint16_t a_before,
    std::uint16_t x_before,
    std::uint16_t y_before) noexcept {
    // The final measured iteration begins with A=0, X=$7EFE, Y=$7EFF.
    // Charging the aggregate here is intentionally completion-only: the
    // aggregate evidence cannot support a fabricated per-iteration schedule.
    if ((pc & 0x00ff'ffffU) != 0x008c2cU || opcode != 0x54U
        || a_before != 0U || x_before != 0x7efeU || y_before != 0x7effU) {
        return std::nullopt;
    }
    return 35175U;
}

} // namespace kss
