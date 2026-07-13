#include "kss/multi_clock_coordinator.hpp"

#include <algorithm>
#include <limits>

namespace kss {
namespace {

constexpr bool event_before(const CoordinatorEvent& left, const CoordinatorEvent& right) noexcept {
    if (left.at != right.at) return left.at < right.at;
    if (left.phase != right.phase) return left.phase < right.phase;
    if (left.domain != right.domain) return left.domain < right.domain;
    return left.sequence < right.sequence;
}

constexpr bool add_overflows(MasterClock left, MasterClock right) noexcept {
    return right > std::numeric_limits<MasterClock>::max() - left;
}

} // namespace

MasterClock MultiClockCoordinator::master_now() const noexcept { return master_now_; }

MasterClock MultiClockCoordinator::ready_at(ClockDomain domain) const noexcept {
    return ready_at_[domain_index(domain)];
}

DomainAdvanceResult MultiClockCoordinator::account_sa1_cycles(std::uint64_t cycles) noexcept {
    if (cycles > std::numeric_limits<MasterClock>::max() / 2U) {
        return {CoordinatorStatus::overflow, 0, ready_at(ClockDomain::sa1)};
    }
    const auto delta = sa1_cycles_to_master(cycles);
    auto& cursor = ready_at_[domain_index(ClockDomain::sa1)];
    if (add_overflows(cursor, delta)) {
        return {CoordinatorStatus::overflow, 0, cursor};
    }
    cursor += delta;
    return {CoordinatorStatus::accepted, delta, cursor};
}

DomainAdvanceResult MultiClockCoordinator::account_scpu_accesses(
    std::span<const SnesBusAccessTiming> accesses) noexcept {
    auto& cursor = ready_at_[domain_index(ClockDomain::scpu)];
    if (accesses.empty()) return {CoordinatorStatus::timing_debt, 0, cursor};
    MasterClock delta = 0;
    for (const auto& access : accesses) {
        const auto duration = snes_bus_cycle_master_clocks(
            access.address, access.fast_rom_enabled);
        if (add_overflows(delta, duration)) {
            return {CoordinatorStatus::overflow, 0, cursor};
        }
        delta += duration;
    }
    if (add_overflows(cursor, delta)) {
        return {CoordinatorStatus::overflow, 0, cursor};
    }
    cursor += delta;
    return {CoordinatorStatus::accepted, delta, cursor};
}

DomainAdvanceResult MultiClockCoordinator::account_spc_cycles(
    std::uint64_t cycles) noexcept {
    // NTSC SNES master clock and the S-SMP architectural cycle clock. The
    // integer master frequency matches the runtime's master-clock contract;
    // retaining the remainder makes the conversion deterministic.
    constexpr std::uint64_t master_hz = 21'477'272U;
    constexpr std::uint64_t spc_hz = 1'024'000U;
    auto& cursor = ready_at_[domain_index(ClockDomain::spc)];
    if (cycles == 0U) return {CoordinatorStatus::timing_debt, 0, cursor};
    if (cycles > (std::numeric_limits<std::uint64_t>::max() - spc_clock_remainder_)
            / master_hz) {
        return {CoordinatorStatus::overflow, 0, cursor};
    }
    const auto scaled = cycles * master_hz + spc_clock_remainder_;
    const auto delta = scaled / spc_hz;
    const auto remainder = scaled % spc_hz;
    if (add_overflows(cursor, delta)) {
        return {CoordinatorStatus::overflow, 0, cursor};
    }
    cursor += delta;
    spc_clock_remainder_ = remainder;
    return {CoordinatorStatus::accepted, delta, cursor};
}

CoordinatorStatus MultiClockCoordinator::align_domain(
    ClockDomain domain, MasterClock at) noexcept {
    auto& cursor = ready_at_[domain_index(domain)];
    if (at < cursor || at < master_now_) return CoordinatorStatus::past_timestamp;
    cursor = at;
    return CoordinatorStatus::accepted;
}

CoordinatorStatus MultiClockCoordinator::record_spc_phase(
    MasterClock at, std::uint32_t phase) noexcept {
    if (at < master_now_
        || (last_spc_phase_
        && (at < last_spc_phase_->at
            || (at == last_spc_phase_->at && phase < last_spc_phase_->phase)))) {
        return CoordinatorStatus::past_timestamp;
    }
    auto& cursor = ready_at_[domain_index(ClockDomain::spc)];
    if (at < cursor) return CoordinatorStatus::past_timestamp;
    cursor = at;
    last_spc_phase_ = SpcPhaseTimestamp{at, phase, next_sequence_++};
    return CoordinatorStatus::accepted;
}

std::optional<SpcPhaseTimestamp> MultiClockCoordinator::last_spc_phase() const noexcept {
    return last_spc_phase_;
}

CoordinatorStatus MultiClockCoordinator::schedule(
    ClockDomain domain, MasterClock at, ClockPhase phase,
    CoordinatorEventKind kind, std::uint32_t payload) {
    if (at < master_now_
        || (at == master_now_ && has_current_phase_
            && (phase < current_phase_
                || (phase == current_phase_ && domain < current_domain_)))) {
        return CoordinatorStatus::past_timestamp;
    }
    CoordinatorEvent event{at, phase, domain, next_sequence_++, kind, payload};
    const auto position = std::upper_bound(events_.begin(), events_.end(), event, event_before);
    events_.insert(position, event);
    return CoordinatorStatus::accepted;
}

CoordinatorStatus MultiClockCoordinator::schedule_cpu_to_spc_port(
    MasterClock at, std::uint8_t port, std::uint8_t value) {
    if (port >= 4U) return CoordinatorStatus::timing_debt;
    const auto payload = static_cast<std::uint32_t>(port)
        | (static_cast<std::uint32_t>(value) << 8U);
    return schedule(ClockDomain::scpu, at, ClockPhase::bus_commit,
        CoordinatorEventKind::cpu_to_spc_port, payload);
}

CoordinatorStatus MultiClockCoordinator::schedule_scpu_irq(MasterClock at) {
    return schedule(ClockDomain::scpu, at, ClockPhase::device_sample,
        CoordinatorEventKind::scpu_irq);
}

CoordinatorStatus MultiClockCoordinator::schedule_scpu_nmi(MasterClock at) {
    return schedule(ClockDomain::scpu, at, ClockPhase::device_sample,
        CoordinatorEventKind::scpu_nmi);
}

CoordinatorStatus MultiClockCoordinator::schedule_scpu_reset(MasterClock at) {
    return schedule(ClockDomain::scpu, at, ClockPhase::device_sample,
        CoordinatorEventKind::scpu_reset);
}

CoordinatorStatus MultiClockCoordinator::schedule_spc_sample(
    MasterClock at, std::uint32_t phase) {
    if ((last_spc_phase_
            && (at < last_spc_phase_->at
                || (at == last_spc_phase_->at && phase < last_spc_phase_->phase)))
        || at < ready_at(ClockDomain::spc)) {
        return CoordinatorStatus::past_timestamp;
    }
    const auto scheduled = schedule(ClockDomain::spc, at, ClockPhase::device_sample,
        CoordinatorEventKind::spc_phase, phase);
    if (scheduled != CoordinatorStatus::accepted) return scheduled;
    ready_at_[domain_index(ClockDomain::spc)] = at;
    last_spc_phase_ = SpcPhaseTimestamp{at, phase, next_sequence_ - 1U};
    return CoordinatorStatus::accepted;
}

CoordinatorStatus MultiClockCoordinator::schedule_first_frame() {
    return schedule(ClockDomain::ppu, kSnesFirstFrameMasterClock,
        ClockPhase::frame_boundary, CoordinatorEventKind::first_frame);
}

std::optional<CoordinatorEvent> MultiClockCoordinator::pop_next() noexcept {
    if (events_.empty()) return std::nullopt;
    const auto event = events_.front();
    events_.erase(events_.begin());
    master_now_ = event.at;
    current_phase_ = event.phase;
    current_domain_ = event.domain;
    has_current_phase_ = true;
    return event;
}

std::size_t MultiClockCoordinator::pending() const noexcept { return events_.size(); }

} // namespace kss
