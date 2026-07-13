#pragma once

#include "kss/cpu.hpp"
#include "kss/rom_validation.hpp"
#include "kss/snes_timing.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kss {

enum class SpcPortDirection : std::uint8_t {
    cpu_to_spc,
    spc_to_cpu,
};

enum class EventRecordStatus : std::uint8_t {
    accepted,
    past_master_clock,
    past_local_cycle,
    invalid_processor,
    invalid_address,
    invalid_port,
};

struct CpuWriteEvent {
    std::uint64_t ordinal{};
    ProcessorId processor{ProcessorId::snes_cpu};
    std::uint64_t local_cycle{};
    MasterClock master_clock{};
    std::uint32_t address{};
    std::uint8_t value{};

    friend constexpr bool operator==(const CpuWriteEvent&, const CpuWriteEvent&) = default;
};

struct SpcPortEvent {
    std::uint64_t ordinal{};
    SpcPortDirection direction{SpcPortDirection::cpu_to_spc};
    std::uint64_t local_cycle{};
    MasterClock master_clock{};
    std::uint8_t port{};
    std::uint8_t value{};

    friend constexpr bool operator==(const SpcPortEvent&, const SpcPortEvent&) = default;
};

struct EventChainFingerprint {
    std::size_t records{};
    std::string sha256{};

    friend bool operator==(const EventChainFingerprint&, const EventChainFingerprint&) = default;
};

struct RuntimeEventChainSummary {
    EventChainFingerprint cpu_writes{};
    EventChainFingerprint scpu_writes{};
    EventChainFingerprint sa1_writes{};
    EventChainFingerprint ppu_register_writes{};
    EventChainFingerprint dma_register_writes{};
    EventChainFingerprint spc_ports{};
    EventChainFingerprint cross_domain_order{};

    friend bool operator==(const RuntimeEventChainSummary&, const RuntimeEventChainSummary&) = default;
};

enum class EventChainMismatch : std::uint32_t {
    none = 0,
    cpu_writes = 1U << 0U,
    scpu_writes = 1U << 1U,
    sa1_writes = 1U << 2U,
    ppu_register_writes = 1U << 3U,
    dma_register_writes = 1U << 4U,
    spc_ports = 1U << 5U,
    cross_domain_order = 1U << 6U,
};

[[nodiscard]] constexpr EventChainMismatch operator|(
    EventChainMismatch lhs, EventChainMismatch rhs) noexcept {
    return static_cast<EventChainMismatch>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool has_mismatch(
    EventChainMismatch value, EventChainMismatch flag) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0U;
}

inline constexpr auto kAllEventChainComparisons = static_cast<EventChainMismatch>(0x7fU);
inline constexpr auto kOracleEventChainComparisons = static_cast<EventChainMismatch>(0x3fU);

// Captures only write/port facts needed by the sanitized first-endFrame
// comparison. Raw records are deliberately memory-only; callers persist the
// count/digest summary, never captured values.
class RuntimeEventChainRecorder {
public:
    [[nodiscard]] EventRecordStatus record_cpu_write(
        ProcessorId processor, std::uint64_t local_cycle, MasterClock master_clock,
        std::uint32_t address, std::uint8_t value);
    [[nodiscard]] EventRecordStatus record_spc_port(
        SpcPortDirection direction, std::uint64_t local_cycle, MasterClock master_clock,
        std::uint8_t port, std::uint8_t value);

    [[nodiscard]] std::span<const CpuWriteEvent> cpu_writes() const noexcept;
    [[nodiscard]] std::span<const SpcPortEvent> spc_ports() const noexcept;
    [[nodiscard]] RuntimeEventChainSummary summary() const;
    void clear() noexcept;

private:
    [[nodiscard]] EventRecordStatus validate_master(MasterClock master_clock) const noexcept;

    std::vector<CpuWriteEvent> cpu_writes_{};
    std::vector<SpcPortEvent> spc_ports_{};
    MasterClock last_master_clock_{};
    bool has_master_clock_{};
    std::uint64_t last_scpu_cycle_{};
    std::uint64_t last_sa1_cycle_{};
    std::uint64_t last_spc_cycle_{};
    bool has_scpu_cycle_{};
    bool has_sa1_cycle_{};
    bool has_spc_cycle_{};
    std::uint64_t next_global_ordinal_{1};
    std::vector<std::uint64_t> cpu_global_ordinals_{};
    std::vector<std::uint64_t> spc_global_ordinals_{};
};

[[nodiscard]] EventChainMismatch compare_event_chain_summaries(
    const RuntimeEventChainSummary& expected,
    const RuntimeEventChainSummary& actual,
    EventChainMismatch comparisons = kAllEventChainComparisons) noexcept;

} // namespace kss
