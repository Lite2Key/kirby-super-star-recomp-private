#include "kss/scpu_access_boundary.hpp"

#include <array>
#include <cassert>
#include <limits>

namespace {

constexpr auto kBlock = kss::BlockKey::make(
    kss::ProcessorId::snes_cpu, 0x00d655U, false, true, false);

constexpr std::array kFinalCmpAccesses{
    kss::ScpuMicroAccess{0x00d655U, kss::BusAccessKind::opcode,
        kss::BusAccessDirection::read, false},
    kss::ScpuMicroAccess{0x00d656U, kss::BusAccessKind::operand,
        kss::BusAccessDirection::read, false},
    kss::ScpuMicroAccess{0x00d657U, kss::BusAccessKind::operand,
        kss::BusAccessDirection::read, false},
    kss::ScpuMicroAccess{0x002140U, kss::BusAccessKind::data,
        kss::BusAccessDirection::read, false},
};

void test_exact_first_endframe_suspends_without_rounding_or_pc_patch() {
    const auto point = kss::observe_scpu_access_boundary(
        kBlock, kFinalCmpAccesses, 306882U, 306900U);
    assert(point.status == kss::ScpuAccessBoundaryStatus::suspended);
    assert(point.block_start == 306882U && point.target == 306900U);
    assert(point.block_end == 306912U);
    assert(point.completed_accesses == 2U && point.current_access_index == 2U);
    assert(point.current_access.address == 0x00d657U);
    assert(point.current_access.kind == kss::BusAccessKind::operand);
    assert(point.current_access_start == 306898U);
    assert(point.current_access_duration == 8U);
    assert(point.elapsed_in_current_access == 2U);
    assert(point.sequencer_address == 0x00d658U);
    assert(!point.architectural_state_committed);
    assert(point.block_end - point.target == 12U);
}

void test_boundaries_and_completion_are_distinct() {
    const auto between = kss::observe_scpu_access_boundary(
        kBlock, kFinalCmpAccesses, 306882U, 306898U);
    assert(between.status == kss::ScpuAccessBoundaryStatus::access_boundary);
    assert(between.completed_accesses == 2U);
    assert(between.current_access.address == 0x00d657U);
    assert(between.elapsed_in_current_access == 0U);
    assert(between.sequencer_address == 0x00d657U);

    const auto complete = kss::observe_scpu_access_boundary(
        kBlock, kFinalCmpAccesses, 306882U, 306912U);
    assert(complete.status == kss::ScpuAccessBoundaryStatus::block_complete);
    assert(complete.completed_accesses == kFinalCmpAccesses.size());
    assert(complete.sequencer_address == 0x00d658U);
    assert(complete.architectural_state_committed);
}

void test_malformed_or_out_of_range_streams_fail_closed() {
    auto malformed = kFinalCmpAccesses;
    malformed[1].address = 0x00d657U;
    assert(kss::observe_scpu_access_boundary(
        kBlock, malformed, 10U, 12U).status
        == kss::ScpuAccessBoundaryStatus::invalid_access_stream);
    assert(kss::observe_scpu_access_boundary(
        kBlock, kFinalCmpAccesses, 10U, 9U).status
        == kss::ScpuAccessBoundaryStatus::target_before_block);
    assert(kss::observe_scpu_access_boundary(
        kBlock, kFinalCmpAccesses, 10U, 41U).status
        == kss::ScpuAccessBoundaryStatus::target_after_block);
    assert(kss::observe_scpu_access_boundary(
        kBlock, kFinalCmpAccesses,
        std::numeric_limits<kss::MasterClock>::max() - 4U,
        std::numeric_limits<kss::MasterClock>::max() - 2U).status
        == kss::ScpuAccessBoundaryStatus::overflow);
}

} // namespace

int main() {
    test_exact_first_endframe_suspends_without_rounding_or_pc_patch();
    test_boundaries_and_completion_are_distinct();
    test_malformed_or_out_of_range_streams_fail_closed();
}
