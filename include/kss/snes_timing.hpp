#pragma once

#include "kss/scheduler.hpp"

#include <cstdint>

namespace kss {

// The initial NTSC frame ends after 225 scanlines. Overscan and interlace
// changes belong to the later PPU state machine rather than this reset value.
inline constexpr MasterClock kSnesMasterClocksPerScanline = 1364;
inline constexpr MasterClock kSnesFirstFrameScanlines = 225;
inline constexpr MasterClock kSnesFirstFrameMasterClock =
    kSnesMasterClocksPerScanline * kSnesFirstFrameScanlines;

// The SA-1 executes at half of the SNES master oscillator. Contention and
// register-specific waits are explicit additions in the SA-1 bus model.
[[nodiscard]] constexpr MasterClock sa1_cycles_to_master(std::uint64_t cycles) noexcept {
    return cycles * 2U;
}

// Duration of one S-CPU bus cycle. This is the hardware address-speed table;
// instruction microcycles will consume it through the timing-aware bus layer.
[[nodiscard]] constexpr std::uint8_t snes_bus_cycle_master_clocks(
    std::uint32_t address,
    bool fast_rom_enabled) noexcept {
    address &= 0x00ff'ffffU;
    const auto bank = static_cast<std::uint8_t>(address >> 16U);
    const auto page = static_cast<std::uint8_t>(address >> 8U);
    const auto quadrant = static_cast<std::uint8_t>(bank >> 6U);

    if (quadrant == 1U) { // $40-$7F
        return 8U;
    }
    if (quadrant == 3U) { // $C0-$FF
        return fast_rom_enabled ? 6U : 8U;
    }
    if (page <= 0x1fU) return 8U;
    if (page <= 0x3fU) return 6U;
    if (page <= 0x41U) return 12U;
    if (page <= 0x5fU) return 6U;
    if (page <= 0x7fU) return 8U;
    if (quadrant == 0U) return 8U; // $00-$3F high cartridge region
    return fast_rom_enabled ? 6U : 8U; // $80-$BF high cartridge region
}

} // namespace kss
