#include "kss/spc_exact_master.hpp"

namespace kss {
namespace {

constexpr bool same_step(
    const apu::SpcStepResult& left, const apu::SpcStepResult& right) noexcept {
    return left.status == right.status
        && left.opcode == right.opcode
        && left.instruction_bytes == right.instruction_bytes
        && left.instruction_cycles == right.instruction_cycles;
}

} // namespace

SpcExactAdvanceResult advance_spc_to_exact_master(
    apu::Spc700Core& core,
    MultiClockCoordinator& clocks,
    MasterClock target,
    std::size_t instruction_limit) noexcept {
    SpcExactAdvanceResult result{};
    result.architectural_ready_at = clocks.ready_at(ClockDomain::spc);
    result.observed_at = result.architectural_ready_at;
    if (target < result.architectural_ready_at) {
        result.status = SpcExactAdvanceStatus::target_before_cursor;
        return result;
    }
    if (target == result.architectural_ready_at) {
        result.status = SpcExactAdvanceStatus::target_reached;
        result.observed_at = target;
        return result;
    }

    for (std::size_t count = 0; count < instruction_limit; ++count) {
        // Spc700Core owns all of its mutable state.  Previewing a copy is a
        // side-effect-free way to discover the instruction-atomic frontier.
        auto preview_core = core;
        // Observation hooks are intentionally not architectural state.  A
        // copied hook could otherwise leak a previewed port write into the
        // real event recorder.
        preview_core.set_port_write_sink(nullptr, nullptr);
        const auto preview_step = preview_core.step();
        if (preview_step.status != apu::SpcStepStatus::executed
            || preview_step.instruction_cycles == 0U) {
            result.status = SpcExactAdvanceStatus::spc_step_failed;
            return result;
        }
        const auto projection = clocks.preview_spc_cycles(
            preview_step.instruction_cycles);
        if (projection.status != CoordinatorStatus::accepted) {
            result.status = SpcExactAdvanceStatus::clock_failure;
            return result;
        }
        if (projection.ready_at > target) {
            result.status = SpcExactAdvanceStatus::target_inside_instruction;
            result.observed_at = target;
            result.in_flight = SpcInFlightObservation{
                core.registers(),
                preview_step,
                projection.start_at,
                target,
                projection.ready_at,
                target - projection.start_at,
                projection.ready_at - target,
                projection.start_remainder,
                projection.completion_remainder,
                true,
            };
            return result;
        }

        const auto completed = core.step();
        if (!same_step(completed, preview_step)) {
            result.status = SpcExactAdvanceStatus::preview_mismatch;
            return result;
        }
        const auto advance = clocks.account_spc_cycles(completed.instruction_cycles);
        if (advance.status != CoordinatorStatus::accepted
            || advance.ready_at != projection.ready_at
            || clocks.spc_clock_remainder() != projection.completion_remainder) {
            result.status = SpcExactAdvanceStatus::clock_failure;
            return result;
        }
        result.last_completed = completed;
        ++result.completed_instructions;
        result.completed_architectural_cycles += completed.instruction_cycles;
        result.architectural_ready_at = advance.ready_at;
        result.observed_at = advance.ready_at;
        if (advance.ready_at == target) {
            result.status = SpcExactAdvanceStatus::target_reached;
            return result;
        }
    }

    result.status = SpcExactAdvanceStatus::step_limit;
    return result;
}

} // namespace kss
