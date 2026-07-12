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

// Returns a timing only when both PC and opcode match the measured prefix.
// Unknown code is unsupported rather than assigned speculative timing.
[[nodiscard]] std::optional<Sa1ResetTiming> lookup_sa1_reset_timing(
    std::uint32_t pc,
    std::uint8_t opcode) noexcept;

} // namespace kss
