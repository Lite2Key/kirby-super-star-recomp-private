#pragma once

#include "kss/dispatcher.hpp"

#include <cstddef>
#include <cstdint>

namespace kss {

enum class GeneratedRunStatus : std::uint8_t {
    checkpoint_reached,
    unknown_block,
    cpu_stopped,
    generated_block_failed_closed,
    step_limit,
};

struct GeneratedRunResult {
    GeneratedRunStatus status{GeneratedRunStatus::step_limit};
    std::size_t completed_blocks{};
    BlockKey last_completed{};
};

// Execute whole generated blocks until the requested instruction-boundary
// identity is reached. The checkpoint includes processor, 24-bit PC and E/M/X;
// reaching the same numeric PC in another mode or processor does not succeed.
[[nodiscard]] GeneratedRunResult run_generated_until(
    CpuContext& cpu,
    Bus& bus,
    Scheduler& scheduler,
    const CheckedDispatcher& dispatcher,
    BlockKey checkpoint,
    std::size_t max_blocks,
    std::size_t minimum_blocks = 0) noexcept;

} // namespace kss
