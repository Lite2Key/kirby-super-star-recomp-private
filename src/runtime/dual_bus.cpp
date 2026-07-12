#include "kss/dual_bus.hpp"

#include <algorithm>

namespace kss {

RomBackedDualBus::RomBackedDualBus(std::span<const std::uint8_t> rom)
    : rom_(rom.begin(), rom.end()) {}

std::uint8_t RomBackedDualBus::read8(
    ProcessorId processor,
    std::uint32_t address,
    BusAccessKind kind) {
    const auto mapping = map_address(processor, address);
    if (kind == BusAccessKind::opcode
        && !has_permission(mapping.permissions, MemoryPermission::execute)) {
        return open_bus(processor);
    }
    if (!has_permission(mapping.permissions, MemoryPermission::read)) {
        return open_bus(processor);
    }

    auto value = open_bus(processor);
    switch (mapping.region) {
    case MemoryRegion::rom:
        if (!rom_.empty()) {
            value = rom_[mapping.canonical_offset % rom_.size()];
        }
        break;
    case MemoryRegion::wram:
        value = wram_[mapping.canonical_offset];
        break;
    case MemoryRegion::bwram:
        value = bwram_[mapping.canonical_offset];
        break;
    case MemoryRegion::sa1_iram:
        value = sa1_iram_[mapping.canonical_offset];
        break;
    case MemoryRegion::hardware_register:
    case MemoryRegion::unmapped:
        // Register behavior is intentionally deferred; reads preserve the
        // processor's independent open-bus latch.
        break;
    }
    open_bus_[processor_index(processor)] = value;
    return value;
}

void RomBackedDualBus::write8(
    ProcessorId processor,
    std::uint32_t address,
    std::uint8_t value,
    BusAccessKind) {
    open_bus_[processor_index(processor)] = value;
    const auto mapping = map_address(processor, address);
    if (!has_permission(mapping.permissions, MemoryPermission::write)) {
        return;
    }

    switch (mapping.region) {
    case MemoryRegion::wram:
        wram_[mapping.canonical_offset] = value;
        break;
    case MemoryRegion::bwram:
        bwram_[mapping.canonical_offset] = value;
        break;
    case MemoryRegion::sa1_iram:
        sa1_iram_[mapping.canonical_offset] = value;
        break;
    case MemoryRegion::hardware_register:
        // Register writes currently only drive open bus.
        break;
    case MemoryRegion::rom:
    case MemoryRegion::unmapped:
        break;
    }
}

std::uint8_t RomBackedDualBus::open_bus(ProcessorId processor) const noexcept {
    return open_bus_[processor_index(processor)];
}

std::span<std::uint8_t> RomBackedDualBus::wram() noexcept {
    return wram_;
}

std::span<std::uint8_t> RomBackedDualBus::bwram() noexcept {
    return bwram_;
}

std::span<std::uint8_t> RomBackedDualBus::sa1_iram() noexcept {
    return sa1_iram_;
}

std::span<const std::uint8_t> RomBackedDualBus::rom() const noexcept {
    return rom_;
}

} // namespace kss
