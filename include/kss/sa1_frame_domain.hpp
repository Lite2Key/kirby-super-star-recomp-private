#pragma once

#include "kss/dispatcher.hpp"

#include <cstddef>
#include <cstdint>

namespace kss {

enum class Sa1PollAdvanceStatus : std::uint8_t {
    target_reached,
    target_inside_instruction,
    poll_released,
    invalid_cpu,
    invalid_identity,
    target_before_cursor,
    unknown_block,
    cpu_stopped,
    generated_block_failed_closed,
    noncanonical_cycle_delta,
    cycle_overflow,
    step_limit,
};

struct Sa1PollAdvanceResult {
    Sa1PollAdvanceStatus status{Sa1PollAdvanceStatus::invalid_cpu};
    std::size_t completed_blocks{};
    std::uint64_t completed_cycles{};
    MasterClock ready_at{};
    MasterClock target{};
    MasterClock shortfall{};
    BlockKey last_completed{};
};

// Resume only the observed native-mode SA-1 $8C58/$8C5B shared-I-RAM poll.
// The caller supplies the domain cursor because reset release and bus
// arbitration establish its master-clock origin. Execution stops at the last
// whole-instruction boundary at or before target_master; it never fabricates
// partial CPU state to make a frame timestamp line up.
[[nodiscard]] Sa1PollAdvanceResult advance_sa1_poll_to(
    CpuContext& cpu,
    Bus& bus,
    Scheduler& scheduler,
    const CheckedDispatcher& dispatcher,
    MasterClock current_master,
    MasterClock target_master,
    std::size_t max_blocks) noexcept;

} // namespace kss
