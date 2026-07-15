#pragma once

#include "kss/scpu_micro_access_recorder.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace kss {

enum class ScpuAccessBoundaryStatus : std::uint8_t {
    suspended,
    access_boundary,
    block_complete,
    target_before_block,
    target_after_block,
    invalid_access_stream,
    overflow,
};

// An exact observation of a generated S-CPU block's value-free bus timeline.
// The architectural CpuContext remains at the preceding completed instruction;
// sequencer_address reports the fetch address visible while the instruction is
// in flight.  Keeping those states separate avoids fabricating a completed PC
// or register transition at a sub-instruction frame boundary.
struct ScpuAccessBoundary {
    ScpuAccessBoundaryStatus status{ScpuAccessBoundaryStatus::invalid_access_stream};
    BlockKey block{};
    MasterClock block_start{};
    MasterClock target{};
    MasterClock block_end{};
    std::size_t completed_accesses{};
    std::size_t current_access_index{};
    ScpuMicroAccess current_access{};
    MasterClock current_access_start{};
    MasterClock current_access_duration{};
    MasterClock elapsed_in_current_access{};
    std::uint32_t sequencer_address{};
    bool architectural_state_committed{};
};

// Locate an exact master-clock boundary inside one already-observed generated
// block. The stream must begin with the block's opcode fetch and may contain
// contiguous operand fetches followed by data/stack/vector accesses. This
// helper observes timing only: it never reads a value, writes the bus, changes
// CpuContext, or rounds the target to a whole instruction.
[[nodiscard]] ScpuAccessBoundary observe_scpu_access_boundary(
    BlockKey block,
    std::span<const ScpuMicroAccess> accesses,
    MasterClock block_start,
    MasterClock target) noexcept;

} // namespace kss
