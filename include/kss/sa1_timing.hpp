#pragma once

#include "kss/address_space.hpp"

#include <cstdint>
#include <optional>

namespace kss {

// An evidence-bound timing sample for the measured SA-1 reset prefix.  The
// observed wait component includes ROM fetch arbitration and, when present,
// the named data region's access cost.  It is deliberately not exposed as a
// generic per-region constant: the reference shows that equal regions can
// have different waits depending on bus phase/contention.
struct Sa1ResetTiming {
    std::uint32_t pc{};
    std::uint8_t opcode{};
    MemoryRegion access_region{MemoryRegion::unmapped};
    bool has_data_access{};
    std::uint8_t base_cycles{};
    std::uint8_t observed_wait_cycles{};

    [[nodiscard]] constexpr std::uint8_t total_cycles() const noexcept {
        return static_cast<std::uint8_t>(base_cycles + observed_wait_cycles);
    }
};

// Aggregate timing for the one specifically measured block-move run.  Unlike
// normal instruction timing, the observed wait component is not divisible
// into a universal per-iteration constant: repeated execution at the same PC
// alternated between different arbitration delays in the reference capture.
struct Sa1ResetBlockMoveTiming {
    std::uint32_t pc{};
    std::uint8_t opcode{};
    std::uint32_t iterations{};
    std::uint32_t base_cycles{};
    std::uint32_t observed_wait_cycles{};

    [[nodiscard]] constexpr std::uint32_t total_cycles() const noexcept {
        return base_cycles + observed_wait_cycles;
    }
};

// Returns a timing only when both PC and opcode match the measured prefix.
// Unknown code is unsupported rather than assigned speculative timing.
[[nodiscard]] std::optional<Sa1ResetTiming> lookup_sa1_reset_timing(
    std::uint32_t pc,
    std::uint8_t opcode) noexcept;

// Returns the aggregate only for the exact reset-loop signature measured by
// the private oracle.  A different iteration count is intentionally unknown.
[[nodiscard]] std::optional<Sa1ResetBlockMoveTiming> lookup_sa1_reset_block_move_timing(
    std::uint32_t pc,
    std::uint8_t opcode,
    std::uint32_t iterations) noexcept;

// Evidence-bound per-iteration wait for the exact KSS reset MVN sequence.
[[nodiscard]] std::optional<std::uint8_t> lookup_sa1_reset_mvn_wait(
    std::uint32_t pc,
    std::uint8_t opcode,
    std::uint16_t a_before,
    std::uint16_t x_before,
    std::uint16_t y_before) noexcept;

} // namespace kss
