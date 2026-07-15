#pragma once

#include "kss/snes_timing.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace kss {

enum class ClockDomain : std::uint8_t {
    scpu,
    sa1,
    spc,
    ppu,
};

// Phases are an explicit ordering contract, not fractional clock durations.
// A CPU bus commit at master T is visible to a device sample at the same T.
enum class ClockPhase : std::uint8_t {
    bus_commit,
    device_sample,
    frame_boundary,
};

enum class CoordinatorEventKind : std::uint8_t {
    cpu_to_spc_port,
    scpu_irq,
    scpu_nmi,
    scpu_reset,
    spc_phase,
    first_frame,
    custom,
};

enum class CoordinatorStatus : std::uint8_t {
    accepted,
    past_timestamp,
    overflow,
    timing_debt,
};

struct CoordinatorEvent {
    MasterClock at{};
    ClockPhase phase{ClockPhase::bus_commit};
    ClockDomain domain{ClockDomain::scpu};
    std::uint64_t sequence{};
    CoordinatorEventKind kind{CoordinatorEventKind::custom};
    std::uint32_t payload{};

    friend constexpr bool operator==(const CoordinatorEvent&, const CoordinatorEvent&) = default;
};

struct DomainAdvanceResult {
    CoordinatorStatus status{CoordinatorStatus::timing_debt};
    MasterClock master_clocks{};
    MasterClock ready_at{};
};

// A non-mutating projection from the current SPC clock-source phase.  The
// remainder is part of the clock position: dropping it would make a later
// instruction boundary drift by a master clock.
struct SpcClockProjection {
    CoordinatorStatus status{CoordinatorStatus::timing_debt};
    std::uint64_t architectural_cycles{};
    MasterClock start_at{};
    MasterClock master_clocks{};
    MasterClock ready_at{};
    std::uint64_t start_remainder{};
    std::uint64_t completion_remainder{};
};

struct SnesBusAccessTiming {
    std::uint32_t address{};
    bool fast_rom_enabled{};
};

struct SpcPhaseTimestamp {
    MasterClock at{};
    std::uint32_t phase{};
    std::uint64_t sequence{};

    friend constexpr bool operator==(const SpcPhaseTimestamp&, const SpcPhaseTimestamp&) = default;
};

class MultiClockCoordinator {
public:
    [[nodiscard]] MasterClock master_now() const noexcept;
    [[nodiscard]] MasterClock ready_at(ClockDomain domain) const noexcept;

    // Exact because the SA-1 clock is master/2. This advances only the SA-1
    // domain cursor; processing queued events remains master-clock-authoritative.
    [[nodiscard]] DomainAdvanceResult account_sa1_cycles(std::uint64_t cycles) noexcept;

    // Every S-CPU micro-access must be supplied. An empty sequence is timing
    // debt and does not mutate the cursor; instruction-cycle approximations
    // are deliberately not accepted by this API.
    [[nodiscard]] DomainAdvanceResult account_scpu_accesses(
        std::span<const SnesBusAccessTiming> accesses) noexcept;

    // The SPC700 architectural cycle clock is 1.024 MHz. Convert it to the
    // SNES master domain with a retained rational remainder so repeated small
    // instruction advances cannot accumulate truncation drift.
    [[nodiscard]] DomainAdvanceResult account_spc_cycles(std::uint64_t cycles) noexcept;
    [[nodiscard]] SpcClockProjection preview_spc_cycles(
        std::uint64_t cycles) const noexcept;
    [[nodiscard]] std::uint64_t spc_clock_remainder() const noexcept;

    // A hardware wait may make one processor causally dependent on another
    // domain without consuming fabricated processor cycles.
    [[nodiscard]] CoordinatorStatus align_domain(
        ClockDomain domain, MasterClock at) noexcept;

    // SPC frequency conversion belongs to the SPC clock source. This records
    // its explicit master-clock/phase position without inventing a ratio.
    [[nodiscard]] CoordinatorStatus record_spc_phase(
        MasterClock at, std::uint32_t phase) noexcept;
    [[nodiscard]] std::optional<SpcPhaseTimestamp> last_spc_phase() const noexcept;

    [[nodiscard]] CoordinatorStatus schedule(
        ClockDomain domain, MasterClock at, ClockPhase phase,
        CoordinatorEventKind kind, std::uint32_t payload = 0);
    [[nodiscard]] CoordinatorStatus schedule_cpu_to_spc_port(
        MasterClock at, std::uint8_t port, std::uint8_t value);
    [[nodiscard]] CoordinatorStatus schedule_scpu_irq(MasterClock at);
    [[nodiscard]] CoordinatorStatus schedule_scpu_nmi(MasterClock at);
    [[nodiscard]] CoordinatorStatus schedule_scpu_reset(MasterClock at);
    [[nodiscard]] CoordinatorStatus schedule_spc_sample(
        MasterClock at, std::uint32_t phase);
    [[nodiscard]] CoordinatorStatus schedule_first_frame();

    // Pops the globally earliest event and advances authoritative master time.
    // Ties are phase, then domain (S-CPU, SA-1, SPC, PPU), then insertion order.
    [[nodiscard]] std::optional<CoordinatorEvent> pop_next() noexcept;
    [[nodiscard]] std::size_t pending() const noexcept;

private:
    [[nodiscard]] static constexpr std::size_t domain_index(ClockDomain domain) noexcept {
        return static_cast<std::size_t>(domain);
    }

    MasterClock master_now_{};
    ClockPhase current_phase_{ClockPhase::bus_commit};
    ClockDomain current_domain_{ClockDomain::scpu};
    bool has_current_phase_{};
    std::array<MasterClock, 4> ready_at_{};
    std::vector<CoordinatorEvent> events_{};
    std::uint64_t next_sequence_{};
    std::optional<SpcPhaseTimestamp> last_spc_phase_{};
    std::uint64_t spc_clock_remainder_{};
};

} // namespace kss
