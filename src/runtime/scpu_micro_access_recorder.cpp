#include "kss/scpu_micro_access_recorder.hpp"

namespace kss {

ScpuMicroAccessRecorder::ScpuMicroAccessRecorder(Bus& inner) noexcept
    : inner_(inner) {}

std::uint8_t ScpuMicroAccessRecorder::read8(
    ProcessorId processor,
    std::uint32_t address,
    BusAccessKind kind) {
    record(processor, address, kind, BusAccessDirection::read);
    return inner_.read8(processor, address, kind);
}

void ScpuMicroAccessRecorder::write8(
    ProcessorId processor,
    std::uint32_t address,
    std::uint8_t value,
    BusAccessKind kind) {
    record(processor, address, kind, BusAccessDirection::write);
    inner_.write8(processor, address, value, kind);
    if (processor == ProcessorId::snes_cpu && is_memsel(address)) {
        fast_rom_enabled_ = (value & 0x01U) != 0;
    }
}

void ScpuMicroAccessRecorder::observe_generated_fetch(
    ProcessorId processor,
    std::uint32_t address,
    BusAccessKind kind) noexcept {
    record(processor, address, kind, BusAccessDirection::read);
}

std::span<const ScpuMicroAccess> ScpuMicroAccessRecorder::accesses() const noexcept {
    return accesses_;
}

std::span<const SnesBusAccessTiming>
ScpuMicroAccessRecorder::timing_accesses() const noexcept {
    return timing_accesses_;
}

MasterClock ScpuMicroAccessRecorder::master_clocks() const noexcept {
    MasterClock total = 0;
    for (const auto& access : timing_accesses_) {
        total += snes_bus_cycle_master_clocks(access.address, access.fast_rom_enabled);
    }
    return total;
}

bool ScpuMicroAccessRecorder::fast_rom_enabled() const noexcept {
    return fast_rom_enabled_;
}

void ScpuMicroAccessRecorder::record(
    ProcessorId processor,
    std::uint32_t address,
    BusAccessKind kind,
    BusAccessDirection direction) {
    if (processor != ProcessorId::snes_cpu) return;
    address &= 0x00ff'ffffU;
    accesses_.push_back({address, kind, direction, fast_rom_enabled_});
    timing_accesses_.push_back({address, fast_rom_enabled_});
}

bool ScpuMicroAccessRecorder::is_memsel(std::uint32_t address) noexcept {
    address &= 0x00ff'ffffU;
    const auto bank = static_cast<std::uint8_t>(address >> 16U);
    return (bank & 0x40U) == 0U && (address & 0xffffU) == 0x420dU;
}

} // namespace kss
