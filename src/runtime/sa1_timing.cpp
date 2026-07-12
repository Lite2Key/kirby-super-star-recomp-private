#include "kss/sa1_timing.hpp"

#include <array>

namespace kss {
namespace {

// Derived from the private Mesen differential capture.  This table contains
// no ROM bytes beyond the standard 65C816 opcode identifiers needed to select
// timing behavior, and no operands or copyrighted payload data.
constexpr std::array kResetPrefix{
    // PC       opcode region                           data   base wait total
    Sa1ResetTiming{0x008bf4U, 0x78U, MemoryRegion::rom, false, 2, 0}, // 2
    Sa1ResetTiming{0x008bf5U, 0x18U, MemoryRegion::rom, false, 2, 1}, // 3
    Sa1ResetTiming{0x008bf6U, 0xfbU, MemoryRegion::rom, false, 2, 1}, // 3
    Sa1ResetTiming{0x008bf7U, 0xe2U, MemoryRegion::rom, false, 3, 2}, // 5
    Sa1ResetTiming{0x008bf9U, 0x9cU, MemoryRegion::hardware_register, true, 4, 4}, // 8
    Sa1ResetTiming{0x008bfcU, 0x9cU, MemoryRegion::hardware_register, true, 4, 3}, // 7
    Sa1ResetTiming{0x008bffU, 0xa9U, MemoryRegion::rom, false, 2, 0}, // 2
    Sa1ResetTiming{0x008c01U, 0x8dU, MemoryRegion::hardware_register, true, 4, 0}, // 4
    Sa1ResetTiming{0x008c04U, 0x9cU, MemoryRegion::hardware_register, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c07U, 0xa9U, MemoryRegion::rom, false, 2, 1}, // 3
    Sa1ResetTiming{0x008c09U, 0x8dU, MemoryRegion::hardware_register, true, 4, 4}, // 8
    Sa1ResetTiming{0x008c0cU, 0x8dU, MemoryRegion::hardware_register, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c0fU, 0x9cU, MemoryRegion::sa1_iram, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c12U, 0x9cU, MemoryRegion::bwram, true, 4, 3}, // 7
    Sa1ResetTiming{0x008c15U, 0xc2U, MemoryRegion::rom, false, 3, 2}, // 5
    Sa1ResetTiming{0x008c17U, 0xa2U, MemoryRegion::rom, false, 3, 3}, // 6
    Sa1ResetTiming{0x008c1aU, 0xa0U, MemoryRegion::rom, false, 3, 3}, // 6
    Sa1ResetTiming{0x008c1dU, 0xa9U, MemoryRegion::rom, false, 3, 3}, // 6
};

} // namespace

std::optional<Sa1ResetTiming> lookup_sa1_reset_timing(
    std::uint32_t pc,
    std::uint8_t opcode) noexcept {
    pc &= 0x00ff'ffffU;
    for (const auto& timing : kResetPrefix) {
        if (timing.pc == pc && timing.opcode == opcode) {
            return timing;
        }
    }
    return std::nullopt;
}

} // namespace kss
