#pragma once

#include "kss/bus.hpp"
#include "kss/multi_clock_coordinator.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kss {

enum class BusAccessDirection : std::uint8_t {
    read,
    write,
};

// Deliberately value-free: this is enough to prove access ordering and charge
// the address-speed table without retaining ROM, RAM, register, or trace bytes.
struct ScpuMicroAccess {
    std::uint32_t address{};
    BusAccessKind kind{BusAccessKind::data};
    BusAccessDirection direction{BusAccessDirection::read};
    bool fast_rom_enabled{};

    friend constexpr bool operator==(const ScpuMicroAccess&, const ScpuMicroAccess&) = default;
};

class ScpuMicroAccessRecorder final : public Bus {
public:
    explicit ScpuMicroAccessRecorder(Bus& inner) noexcept;

    [[nodiscard]] std::uint8_t read8(
        ProcessorId processor,
        std::uint32_t address,
        BusAccessKind kind = BusAccessKind::data) override;
    void write8(
        ProcessorId processor,
        std::uint32_t address,
        std::uint8_t value,
        BusAccessKind kind = BusAccessKind::data) override;
    void observe_generated_fetch(
        ProcessorId processor,
        std::uint32_t address,
        BusAccessKind kind) noexcept override;

    [[nodiscard]] std::span<const ScpuMicroAccess> accesses() const noexcept;
    [[nodiscard]] std::span<const SnesBusAccessTiming> timing_accesses() const noexcept;
    [[nodiscard]] MasterClock master_clocks() const noexcept;
    [[nodiscard]] bool fast_rom_enabled() const noexcept;

private:
    void record(
        ProcessorId processor,
        std::uint32_t address,
        BusAccessKind kind,
        BusAccessDirection direction);
    [[nodiscard]] static bool is_memsel(std::uint32_t address) noexcept;

    Bus& inner_;
    bool fast_rom_enabled_{};
    std::vector<ScpuMicroAccess> accesses_{};
    std::vector<SnesBusAccessTiming> timing_accesses_{};
};

} // namespace kss
