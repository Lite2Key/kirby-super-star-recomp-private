#include "kss/generated_block_runner.hpp"

namespace kss {

GeneratedRunResult run_generated_until(
    CpuContext& cpu,
    Bus& bus,
    Scheduler& scheduler,
    const CheckedDispatcher& dispatcher,
    BlockKey checkpoint,
    std::size_t max_blocks,
    std::size_t minimum_blocks) noexcept {
    GeneratedRunResult result{};
    if (minimum_blocks == 0 && cpu.block_key() == checkpoint) {
        result.status = GeneratedRunStatus::checkpoint_reached;
        return result;
    }
    for (std::size_t step = 0; step < max_blocks; ++step) {
        if (cpu.stopped) {
            result.status = GeneratedRunStatus::cpu_stopped;
            return result;
        }
        const auto before = cpu.block_key();
        const auto status = dispatcher.dispatch(cpu, bus, scheduler);
        if (status == DispatchStatus::unknown_block) {
            result.status = GeneratedRunStatus::unknown_block;
            return result;
        }
        if (status == DispatchStatus::cpu_stopped) {
            result.status = GeneratedRunStatus::cpu_stopped;
            return result;
        }
        if (cpu.stopped) {
            result.status = GeneratedRunStatus::generated_block_failed_closed;
            return result;
        }
        result.last_completed = before;
        ++result.completed_blocks;
        if (result.completed_blocks >= minimum_blocks && cpu.block_key() == checkpoint) {
            result.status = GeneratedRunStatus::checkpoint_reached;
            return result;
        }
    }
    result.status = GeneratedRunStatus::step_limit;
    return result;
}

} // namespace kss
