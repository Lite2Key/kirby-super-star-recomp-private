#include "kss/sa1_frame_domain.hpp"

#include <limits>

namespace kss {
namespace {

constexpr auto kPollLoad = BlockKey::make(
    ProcessorId::sa1, 0x008c58U, false, false, false);
constexpr auto kPollBranch = BlockKey::make(
    ProcessorId::sa1, 0x008c5bU, false, false, false);

constexpr std::uint64_t expected_cycles(BlockKey key) noexcept {
    return key == kPollLoad ? 5U : key == kPollBranch ? 3U : 0U;
}

constexpr BlockKey expected_next(BlockKey key) noexcept {
    return key == kPollLoad ? kPollBranch : kPollLoad;
}

} // namespace

Sa1PollAdvanceResult advance_sa1_poll_to(
    CpuContext& cpu,
    Bus& bus,
    Scheduler& scheduler,
    const CheckedDispatcher& dispatcher,
    MasterClock current_master,
    MasterClock target_master,
    std::size_t max_blocks) noexcept {
    Sa1PollAdvanceResult result{};
    result.ready_at = current_master;
    result.target = target_master;
    result.shortfall = target_master >= current_master
        ? target_master - current_master : 0U;

    if (cpu.processor != ProcessorId::sa1) {
        result.status = Sa1PollAdvanceStatus::invalid_cpu;
        return result;
    }
    if (target_master < current_master) {
        result.status = Sa1PollAdvanceStatus::target_before_cursor;
        return result;
    }
    if (cpu.block_key() != kPollLoad && cpu.block_key() != kPollBranch) {
        result.status = Sa1PollAdvanceStatus::invalid_identity;
        return result;
    }
    if (target_master == current_master) {
        result.status = Sa1PollAdvanceStatus::target_reached;
        return result;
    }

    for (std::size_t step = 0; step < max_blocks; ++step) {
        if (cpu.stopped) {
            result.status = Sa1PollAdvanceStatus::cpu_stopped;
            return result;
        }
        const auto before = cpu.block_key();
        if (before != kPollLoad && before != kPollBranch) {
            result.status = Sa1PollAdvanceStatus::invalid_identity;
            return result;
        }
        // A negative 16-bit shared value releases BPL. The generated first-
        // frame corpus intentionally has no post-frame $8C5D block, so expose
        // that causal frontier without dispatching a block that must fail.
        if (before == kPollBranch && cpu.flag(StatusFlag::negative)) {
            result.status = Sa1PollAdvanceStatus::poll_released;
            return result;
        }

        const auto instruction_cycles = expected_cycles(before);
        const auto instruction_master = instruction_cycles * 2U;
        const auto remaining = target_master - result.ready_at;
        if (instruction_master > remaining) {
            result.shortfall = remaining;
            result.status = Sa1PollAdvanceStatus::target_inside_instruction;
            return result;
        }
        if (cpu.cycles > std::numeric_limits<std::uint64_t>::max() - instruction_cycles) {
            result.status = Sa1PollAdvanceStatus::cycle_overflow;
            return result;
        }

        const auto cycles_before = cpu.cycles;
        const auto dispatch = dispatcher.dispatch(cpu, bus, scheduler);
        if (dispatch == DispatchStatus::unknown_block) {
            result.status = Sa1PollAdvanceStatus::unknown_block;
            return result;
        }
        if (dispatch == DispatchStatus::cpu_stopped) {
            result.status = Sa1PollAdvanceStatus::cpu_stopped;
            return result;
        }
        if (cpu.stopped) {
            result.status = Sa1PollAdvanceStatus::generated_block_failed_closed;
            return result;
        }
        const auto elapsed = cpu.cycles - cycles_before;
        if (elapsed != instruction_cycles || cpu.block_key() != expected_next(before)) {
            result.status = Sa1PollAdvanceStatus::noncanonical_cycle_delta;
            return result;
        }

        result.last_completed = before;
        ++result.completed_blocks;
        result.completed_cycles += elapsed;
        result.ready_at += elapsed * 2U;
        result.shortfall = target_master - result.ready_at;
        if (result.ready_at == target_master) {
            result.status = Sa1PollAdvanceStatus::target_reached;
            return result;
        }
    }

    result.status = Sa1PollAdvanceStatus::step_limit;
    return result;
}

} // namespace kss
