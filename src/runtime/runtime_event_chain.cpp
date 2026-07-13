#include "kss/runtime_event_chain.hpp"

#include <string_view>

namespace kss {
namespace {

constexpr std::string_view processor_name(ProcessorId processor) noexcept {
    return processor == ProcessorId::sa1 ? "sa1" : "scpu";
}

constexpr std::string_view direction_name(SpcPortDirection direction) noexcept {
    return direction == SpcPortDirection::spc_to_cpu ? "spc_to_cpu" : "cpu_to_spc";
}

constexpr bool is_hardware_bank(std::uint32_t address) noexcept {
    return ((address >> 16U) & 0x40U) == 0U;
}

constexpr bool is_ppu_register(std::uint32_t address) noexcept {
    const auto offset = address & 0xffffU;
    return is_hardware_bank(address) && offset >= 0x2100U && offset <= 0x213fU;
}

constexpr bool is_dma_register(std::uint32_t address) noexcept {
    const auto offset = address & 0xffffU;
    return is_hardware_bank(address)
        && (offset == 0x420bU || offset == 0x420cU
            || (offset >= 0x4300U && offset <= 0x437fU));
}

template <typename Predicate>
EventChainFingerprint fingerprint_cpu_writes(
    std::span<const CpuWriteEvent> events, Predicate include) {
    std::string canonical{"["};
    std::size_t records = 0;
    for (const auto& event : events) {
        if (!include(event)) continue;
        if (records++ != 0U) canonical.push_back(',');
        canonical += '[' + std::to_string(event.ordinal) + ",\"";
        canonical += processor_name(event.processor);
        canonical += "\"," + std::to_string(event.local_cycle);
        canonical += ',' + std::to_string(event.address);
        canonical += ',' + std::to_string(event.value) + ']';
    }
    canonical.push_back(']');
    return {records, sha256_hex(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size()})};
}

EventChainFingerprint fingerprint_spc_ports(std::span<const SpcPortEvent> events) {
    std::string canonical{"["};
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto& event = events[index];
        if (index != 0U) canonical.push_back(',');
        canonical += '[' + std::to_string(event.ordinal) + ",\"";
        canonical += direction_name(event.direction);
        canonical += "\"," + std::to_string(event.local_cycle);
        canonical += ',' + std::to_string(event.port);
        canonical += ',' + std::to_string(event.value) + ']';
    }
    canonical.push_back(']');
    return {events.size(), sha256_hex(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size()})};
}

EventChainFingerprint fingerprint_cross_domain(
    std::span<const CpuWriteEvent> cpu_writes,
    std::span<const std::uint64_t> cpu_global_ordinals,
    std::span<const SpcPortEvent> spc_ports,
    std::span<const std::uint64_t> spc_global_ordinals) {
    std::string canonical{"["};
    std::size_t cpu = 0;
    std::size_t spc = 0;
    std::size_t records = 0;
    while (cpu < cpu_writes.size() || spc < spc_ports.size()) {
        const bool take_cpu = spc == spc_ports.size()
            || (cpu < cpu_writes.size()
                && cpu_global_ordinals[cpu] < spc_global_ordinals[spc]);
        if (records++ != 0U) canonical.push_back(',');
        if (take_cpu) {
            const auto& event = cpu_writes[cpu];
            canonical += '[' + std::to_string(cpu_global_ordinals[cpu])
                + ",\"cpu_write\",\"";
            canonical += processor_name(event.processor);
            canonical += "\"," + std::to_string(event.master_clock);
            canonical += ',' + std::to_string(event.local_cycle);
            canonical += ',' + std::to_string(event.address);
            canonical += ',' + std::to_string(event.value) + ']';
            ++cpu;
        } else {
            const auto& event = spc_ports[spc];
            canonical += '[' + std::to_string(spc_global_ordinals[spc])
                + ",\"spc_port\",\"";
            canonical += direction_name(event.direction);
            canonical += "\"," + std::to_string(event.master_clock);
            canonical += ',' + std::to_string(event.local_cycle);
            canonical += ',' + std::to_string(event.port);
            canonical += ',' + std::to_string(event.value) + ']';
            ++spc;
        }
    }
    canonical.push_back(']');
    return {records, sha256_hex(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size()})};
}

} // namespace

EventRecordStatus RuntimeEventChainRecorder::record_cpu_write(
    ProcessorId processor, std::uint64_t local_cycle, MasterClock master_clock,
    std::uint32_t address, std::uint8_t value) {
    if (processor != ProcessorId::snes_cpu && processor != ProcessorId::sa1) {
        return EventRecordStatus::invalid_processor;
    }
    if (address > 0x00ff'ffffU) return EventRecordStatus::invalid_address;
    if (const auto master = validate_master(master_clock);
        master != EventRecordStatus::accepted) return master;

    auto& last_cycle = processor == ProcessorId::sa1 ? last_sa1_cycle_ : last_scpu_cycle_;
    auto& has_cycle = processor == ProcessorId::sa1 ? has_sa1_cycle_ : has_scpu_cycle_;
    if (has_cycle && local_cycle < last_cycle) return EventRecordStatus::past_local_cycle;

    cpu_writes_.push_back({cpu_writes_.size() + 1U, processor, local_cycle,
        master_clock, address, value});
    cpu_global_ordinals_.push_back(next_global_ordinal_++);
    last_master_clock_ = master_clock;
    has_master_clock_ = true;
    last_cycle = local_cycle;
    has_cycle = true;
    return EventRecordStatus::accepted;
}

EventRecordStatus RuntimeEventChainRecorder::record_spc_port(
    SpcPortDirection direction, std::uint64_t local_cycle, MasterClock master_clock,
    std::uint8_t port, std::uint8_t value) {
    if (port > 3U) return EventRecordStatus::invalid_port;
    if (const auto master = validate_master(master_clock);
        master != EventRecordStatus::accepted) return master;
    if (has_spc_cycle_ && local_cycle < last_spc_cycle_) {
        return EventRecordStatus::past_local_cycle;
    }

    spc_ports_.push_back({spc_ports_.size() + 1U, direction, local_cycle,
        master_clock, port, value});
    spc_global_ordinals_.push_back(next_global_ordinal_++);
    last_master_clock_ = master_clock;
    has_master_clock_ = true;
    last_spc_cycle_ = local_cycle;
    has_spc_cycle_ = true;
    return EventRecordStatus::accepted;
}

std::span<const CpuWriteEvent> RuntimeEventChainRecorder::cpu_writes() const noexcept {
    return cpu_writes_;
}

std::span<const SpcPortEvent> RuntimeEventChainRecorder::spc_ports() const noexcept {
    return spc_ports_;
}

RuntimeEventChainSummary RuntimeEventChainRecorder::summary() const {
    const auto all = [](const CpuWriteEvent&) { return true; };
    const auto scpu = [](const CpuWriteEvent& event) {
        return event.processor == ProcessorId::snes_cpu;
    };
    const auto sa1 = [](const CpuWriteEvent& event) {
        return event.processor == ProcessorId::sa1;
    };
    return {
        fingerprint_cpu_writes(cpu_writes_, all),
        fingerprint_cpu_writes(cpu_writes_, scpu),
        fingerprint_cpu_writes(cpu_writes_, sa1),
        fingerprint_cpu_writes(cpu_writes_, [](const CpuWriteEvent& event) {
            return is_ppu_register(event.address);
        }),
        fingerprint_cpu_writes(cpu_writes_, [](const CpuWriteEvent& event) {
            return is_dma_register(event.address);
        }),
        fingerprint_spc_ports(spc_ports_),
        fingerprint_cross_domain(cpu_writes_, cpu_global_ordinals_,
            spc_ports_, spc_global_ordinals_),
    };
}

void RuntimeEventChainRecorder::clear() noexcept {
    cpu_writes_.clear();
    spc_ports_.clear();
    cpu_global_ordinals_.clear();
    spc_global_ordinals_.clear();
    last_master_clock_ = 0;
    has_master_clock_ = false;
    last_scpu_cycle_ = 0;
    last_sa1_cycle_ = 0;
    last_spc_cycle_ = 0;
    has_scpu_cycle_ = false;
    has_sa1_cycle_ = false;
    has_spc_cycle_ = false;
    next_global_ordinal_ = 1;
}

EventRecordStatus RuntimeEventChainRecorder::validate_master(
    MasterClock master_clock) const noexcept {
    return has_master_clock_ && master_clock < last_master_clock_
        ? EventRecordStatus::past_master_clock : EventRecordStatus::accepted;
}

EventChainMismatch compare_event_chain_summaries(
    const RuntimeEventChainSummary& expected,
    const RuntimeEventChainSummary& actual,
    EventChainMismatch comparisons) noexcept {
    auto result = EventChainMismatch::none;
    if (has_mismatch(comparisons, EventChainMismatch::cpu_writes)
        && expected.cpu_writes != actual.cpu_writes)
        result = result | EventChainMismatch::cpu_writes;
    if (has_mismatch(comparisons, EventChainMismatch::scpu_writes)
        && expected.scpu_writes != actual.scpu_writes)
        result = result | EventChainMismatch::scpu_writes;
    if (has_mismatch(comparisons, EventChainMismatch::sa1_writes)
        && expected.sa1_writes != actual.sa1_writes)
        result = result | EventChainMismatch::sa1_writes;
    if (has_mismatch(comparisons, EventChainMismatch::ppu_register_writes)
        && expected.ppu_register_writes != actual.ppu_register_writes)
        result = result | EventChainMismatch::ppu_register_writes;
    if (has_mismatch(comparisons, EventChainMismatch::dma_register_writes)
        && expected.dma_register_writes != actual.dma_register_writes)
        result = result | EventChainMismatch::dma_register_writes;
    if (has_mismatch(comparisons, EventChainMismatch::spc_ports)
        && expected.spc_ports != actual.spc_ports)
        result = result | EventChainMismatch::spc_ports;
    if (has_mismatch(comparisons, EventChainMismatch::cross_domain_order)
        && expected.cross_domain_order != actual.cross_domain_order)
        result = result | EventChainMismatch::cross_domain_order;
    return result;
}

} // namespace kss
