#include "kss/scpu_access_boundary.hpp"

#include "kss/snes_timing.hpp"

#include <limits>

namespace kss {
namespace {

constexpr bool add_overflows(MasterClock left, MasterClock right) noexcept {
    return right > std::numeric_limits<MasterClock>::max() - left;
}

constexpr std::uint32_t next_in_bank(std::uint32_t address) noexcept {
    return (address & 0x00ff'0000U) | ((address + 1U) & 0xffffU);
}

} // namespace

ScpuAccessBoundary observe_scpu_access_boundary(
    BlockKey block,
    std::span<const ScpuMicroAccess> accesses,
    MasterClock block_start,
    MasterClock target) noexcept {
    ScpuAccessBoundary result{};
    result.block = block;
    result.block_start = block_start;
    result.target = target;
    result.sequencer_address = block.address;

    if (block.processor != ProcessorId::snes_cpu || accesses.empty()
        || accesses.front().address != block.address
        || accesses.front().kind != BusAccessKind::opcode
        || accesses.front().direction != BusAccessDirection::read) {
        return result;
    }
    if (target < block_start) {
        result.status = ScpuAccessBoundaryStatus::target_before_block;
        return result;
    }

    auto expected_fetch = block.address;
    bool data_phase = false;
    for (const auto& access : accesses) {
        const auto is_fetch = access.kind == BusAccessKind::opcode
            || access.kind == BusAccessKind::operand;
        if (is_fetch) {
            if (data_phase || access.direction != BusAccessDirection::read
                || access.address != expected_fetch
                || (expected_fetch == block.address
                    ? access.kind != BusAccessKind::opcode
                    : access.kind != BusAccessKind::operand)) {
                return result;
            }
            expected_fetch = next_in_bank(expected_fetch);
        } else {
            data_phase = true;
        }
    }

    auto cursor = block_start;
    std::uint32_t sequencer = block.address;
    for (std::size_t index = 0; index < accesses.size(); ++index) {
        const auto& access = accesses[index];
        const auto duration = snes_bus_cycle_master_clocks(
            access.address, access.fast_rom_enabled);
        if (add_overflows(cursor, duration)) {
            result.status = ScpuAccessBoundaryStatus::overflow;
            return result;
        }
        const auto end = cursor + duration;
        if (target < end) {
            if (target > cursor && (access.kind == BusAccessKind::opcode
                    || access.kind == BusAccessKind::operand)) {
                sequencer = next_in_bank(access.address);
            }
            result.status = target == cursor
                ? ScpuAccessBoundaryStatus::access_boundary
                : ScpuAccessBoundaryStatus::suspended;
            result.block_end = end;
            for (std::size_t tail = index + 1; tail < accesses.size(); ++tail) {
                const auto tail_duration = snes_bus_cycle_master_clocks(
                    accesses[tail].address, accesses[tail].fast_rom_enabled);
                if (add_overflows(result.block_end, tail_duration)) {
                    result.status = ScpuAccessBoundaryStatus::overflow;
                    return result;
                }
                result.block_end += tail_duration;
            }
            result.completed_accesses = index;
            result.current_access_index = index;
            result.current_access = access;
            result.current_access_start = cursor;
            result.current_access_duration = duration;
            result.elapsed_in_current_access = target - cursor;
            result.sequencer_address = sequencer;
            return result;
        }
        if (access.kind == BusAccessKind::opcode
            || access.kind == BusAccessKind::operand) {
            sequencer = next_in_bank(access.address);
        }
        cursor = end;
    }

    result.block_end = cursor;
    result.completed_accesses = accesses.size();
    result.current_access_index = accesses.size();
    result.sequencer_address = sequencer;
    if (target == cursor) {
        result.status = ScpuAccessBoundaryStatus::block_complete;
        result.architectural_state_committed = true;
    } else {
        result.status = ScpuAccessBoundaryStatus::target_after_block;
    }
    return result;
}

} // namespace kss
