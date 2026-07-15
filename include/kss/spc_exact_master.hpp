#pragma once

#include "kss/multi_clock_coordinator.hpp"
#include "kss/spc700.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace kss {

enum class SpcExactAdvanceStatus : std::uint8_t {
    target_reached,
    target_inside_instruction,
    target_before_cursor,
    spc_step_failed,
    clock_failure,
    preview_mismatch,
    step_limit,
};

// The SPC core is instruction-atomic.  This record deliberately does not
// invent register or memory mutations at an unknown micro-cycle.  It preserves
// the architectural entry state and identifies the exact master-clock offset
// inside the pending instruction.
struct SpcInFlightObservation {
    apu::Spc700Registers architectural_entry{};
    apu::SpcStepResult pending_instruction{};
    MasterClock instruction_start{};
    MasterClock observed_at{};
    MasterClock instruction_completion{};
    MasterClock elapsed_master_clocks{};
    MasterClock remaining_master_clocks{};
    std::uint64_t clock_remainder_at_entry{};
    std::uint64_t clock_remainder_at_completion{};
    bool architectural_state_is_entry{true};
};

struct SpcExactAdvanceResult {
    SpcExactAdvanceStatus status{SpcExactAdvanceStatus::clock_failure};
    std::size_t completed_instructions{};
    std::uint64_t completed_architectural_cycles{};
    MasterClock architectural_ready_at{};
    MasterClock observed_at{};
    std::optional<SpcInFlightObservation> in_flight{};
    std::optional<apu::SpcStepResult> last_completed{};
};

// Execute only whole instructions whose completion is at or before target.
// When target falls inside the next instruction, the real core and coordinator
// remain at the instruction-entry boundary.  A step performed on a private
// copy supplies the instruction's length; the returned in-flight observation
// represents the exact target without rewind, patching, or fabricated state.
[[nodiscard]] SpcExactAdvanceResult advance_spc_to_exact_master(
    apu::Spc700Core& core,
    MultiClockCoordinator& clocks,
    MasterClock target,
    std::size_t instruction_limit = 16'384U) noexcept;

} // namespace kss
