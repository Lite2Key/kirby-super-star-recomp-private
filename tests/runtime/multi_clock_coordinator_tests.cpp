#include "kss/multi_clock_coordinator.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

namespace {

using kss::ClockDomain;
using kss::ClockPhase;
using kss::CoordinatorEventKind;
using kss::CoordinatorStatus;
using kss::MultiClockCoordinator;

void test_exact_domain_progress_and_timing_debt() {
    MultiClockCoordinator coordinator;
    const auto sa1 = coordinator.account_sa1_cycles(153450);
    assert(sa1.status == CoordinatorStatus::accepted);
    assert(sa1.master_clocks == 306900 && sa1.ready_at == 306900);
    assert(coordinator.ready_at(ClockDomain::sa1) == kss::kSnesFirstFrameMasterClock);

    const auto debt = coordinator.account_scpu_accesses({});
    assert(debt.status == CoordinatorStatus::timing_debt);
    assert(debt.master_clocks == 0 && debt.ready_at == 0);

    constexpr std::array accesses{
        kss::SnesBusAccessTiming{0x008004, false}, // slow cartridge: 8
        kss::SnesBusAccessTiming{0x002100, false}, // B-bus: 6
        kss::SnesBusAccessTiming{0x004000, false}, // controller I/O: 12
        kss::SnesBusAccessTiming{0xc08000, true},  // FastROM: 6
    };
    const auto exact = coordinator.account_scpu_accesses(accesses);
    assert(exact.status == CoordinatorStatus::accepted);
    assert(exact.master_clocks == 32 && exact.ready_at == 32);

    const auto overflow = coordinator.account_sa1_cycles(
        std::numeric_limits<std::uint64_t>::max());
    assert(overflow.status == CoordinatorStatus::overflow);
    assert(coordinator.ready_at(ClockDomain::sa1) == 306900);
}

void test_stable_cross_domain_event_ordering() {
    MultiClockCoordinator coordinator;
    assert(coordinator.schedule(ClockDomain::ppu, 100, ClockPhase::frame_boundary,
        CoordinatorEventKind::custom, 4) == CoordinatorStatus::accepted);
    assert(coordinator.schedule(ClockDomain::spc, 100, ClockPhase::device_sample,
        CoordinatorEventKind::custom, 3) == CoordinatorStatus::accepted);
    assert(coordinator.schedule(ClockDomain::sa1, 100, ClockPhase::bus_commit,
        CoordinatorEventKind::custom, 2) == CoordinatorStatus::accepted);
    assert(coordinator.schedule(ClockDomain::scpu, 100, ClockPhase::bus_commit,
        CoordinatorEventKind::custom, 1) == CoordinatorStatus::accepted);
    assert(coordinator.schedule(ClockDomain::scpu, 100, ClockPhase::bus_commit,
        CoordinatorEventKind::custom, 5) == CoordinatorStatus::accepted);

    const auto first = coordinator.pop_next();
    const auto second = coordinator.pop_next();
    const auto third = coordinator.pop_next();
    const auto fourth = coordinator.pop_next();
    const auto fifth = coordinator.pop_next();
    assert(first && first->payload == 1);
    assert(second && second->payload == 5); // stable insertion order inside a domain
    assert(third && third->payload == 2);
    assert(fourth && fourth->payload == 3);
    assert(fifth && fifth->payload == 4);
    assert(coordinator.master_now() == 100 && coordinator.pending() == 0);
    assert(coordinator.schedule(ClockDomain::scpu, 99, ClockPhase::bus_commit,
        CoordinatorEventKind::custom) == CoordinatorStatus::past_timestamp);
    assert(coordinator.schedule(ClockDomain::scpu, 100, ClockPhase::bus_commit,
        CoordinatorEventKind::custom) == CoordinatorStatus::past_timestamp);

    MultiClockCoordinator domain_guard;
    assert(domain_guard.schedule(ClockDomain::sa1, 20, ClockPhase::bus_commit,
        CoordinatorEventKind::custom) == CoordinatorStatus::accepted);
    assert(domain_guard.pop_next());
    assert(domain_guard.schedule(ClockDomain::scpu, 20, ClockPhase::bus_commit,
        CoordinatorEventKind::custom) == CoordinatorStatus::past_timestamp);
}

void test_first_frame_sa1_alignment() {
    MultiClockCoordinator coordinator;
    assert(kss::kSnesFirstFrameMasterClock == 306900);
    assert(coordinator.account_sa1_cycles(153450).ready_at == 306900);
    assert(coordinator.schedule_first_frame() == CoordinatorStatus::accepted);
    const auto frame = coordinator.pop_next();
    assert(frame && frame->kind == CoordinatorEventKind::first_frame);
    assert(frame->domain == ClockDomain::ppu);
    assert(frame->phase == ClockPhase::frame_boundary);
    assert(frame->at == 306900 && coordinator.master_now() == 306900);
}

void test_cpu_to_spc_phase_ordering_and_monotonic_stamps() {
    MultiClockCoordinator coordinator;
    assert(coordinator.schedule_spc_sample(500, 7) == CoordinatorStatus::accepted);
    assert(coordinator.schedule_cpu_to_spc_port(500, 2, 0xa5) == CoordinatorStatus::accepted);
    const auto write = coordinator.pop_next();
    const auto sample = coordinator.pop_next();
    assert(write && write->kind == CoordinatorEventKind::cpu_to_spc_port);
    assert(write->phase == ClockPhase::bus_commit);
    assert((write->payload & 0xffU) == 2U && (write->payload >> 8U) == 0xa5U);
    assert(sample && sample->kind == CoordinatorEventKind::spc_phase);
    assert(sample->phase == ClockPhase::device_sample && sample->payload == 7U);
    assert(coordinator.last_spc_phase()
        && coordinator.last_spc_phase()->at == 500
        && coordinator.last_spc_phase()->phase == 7);
    assert(coordinator.record_spc_phase(499, 8) == CoordinatorStatus::past_timestamp);
    assert(coordinator.record_spc_phase(500, 6) == CoordinatorStatus::past_timestamp);
    assert(coordinator.record_spc_phase(500, 8) == CoordinatorStatus::accepted);
    assert(coordinator.schedule_cpu_to_spc_port(600, 4, 0)
        == CoordinatorStatus::timing_debt);

    MultiClockCoordinator atomic;
    assert(atomic.schedule(ClockDomain::ppu, 600, ClockPhase::frame_boundary,
        CoordinatorEventKind::custom) == CoordinatorStatus::accepted);
    assert(atomic.pop_next());
    assert(atomic.schedule_spc_sample(500, 1) == CoordinatorStatus::past_timestamp);
    assert(!atomic.last_spc_phase());
}

void test_scpu_signal_ordering_against_bus_commit_and_frame_boundary() {
    MultiClockCoordinator coordinator;
    assert(coordinator.schedule(ClockDomain::scpu, 100, ClockPhase::bus_commit,
        CoordinatorEventKind::custom, 0xaa) == CoordinatorStatus::accepted);
    assert(coordinator.schedule_scpu_irq(100) == CoordinatorStatus::accepted);
    assert(coordinator.schedule_scpu_nmi(100) == CoordinatorStatus::accepted);
    assert(coordinator.schedule_scpu_reset(100) == CoordinatorStatus::accepted);
    assert(coordinator.schedule(ClockDomain::ppu, 100, ClockPhase::frame_boundary,
        CoordinatorEventKind::custom, 0xff) == CoordinatorStatus::accepted);

    const auto commit = coordinator.pop_next();
    const auto irq = coordinator.pop_next();
    const auto nmi = coordinator.pop_next();
    const auto reset = coordinator.pop_next();
    const auto frame = coordinator.pop_next();
    assert(commit && commit->phase == ClockPhase::bus_commit && commit->payload == 0xaa);
    assert(irq && irq->kind == CoordinatorEventKind::scpu_irq
        && irq->phase == ClockPhase::device_sample);
    assert(nmi && nmi->kind == CoordinatorEventKind::scpu_nmi);
    assert(reset && reset->kind == CoordinatorEventKind::scpu_reset);
    assert(frame && frame->phase == ClockPhase::frame_boundary && frame->payload == 0xff);

    MultiClockCoordinator first_frame;
    assert(first_frame.schedule_scpu_nmi(kss::kSnesFirstFrameMasterClock)
        == CoordinatorStatus::accepted);
    assert(first_frame.schedule_first_frame() == CoordinatorStatus::accepted);
    assert(first_frame.pop_next()->kind == CoordinatorEventKind::scpu_nmi);
    assert(first_frame.pop_next()->kind == CoordinatorEventKind::first_frame);
}

} // namespace

int main() {
    test_exact_domain_progress_and_timing_debt();
    test_stable_cross_domain_event_ordering();
    test_first_frame_sa1_alignment();
    test_cpu_to_spc_phase_ordering_and_monotonic_stamps();
    test_scpu_signal_ordering_against_bus_commit_and_frame_boundary();
}
