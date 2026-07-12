#include "kss/sa1_timing.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

struct Reference {
    std::uint32_t pc;
    std::uint8_t opcode;
    std::uint8_t total;
};

void test_private_reference_summary() {
    // Sanitized summary of successive Mesen cycle deltas from 00:8BF4 through
    // the state at 00:8C20.  No operands, ROM bytes, or disassembly are stored.
    constexpr std::array reference{
        Reference{0x008bf4, 0x78, 2}, Reference{0x008bf5, 0x18, 3},
        Reference{0x008bf6, 0xfb, 3}, Reference{0x008bf7, 0xe2, 5},
        Reference{0x008bf9, 0x9c, 8}, Reference{0x008bfc, 0x9c, 7},
        Reference{0x008bff, 0xa9, 2}, Reference{0x008c01, 0x8d, 4},
        Reference{0x008c04, 0x9c, 7}, Reference{0x008c07, 0xa9, 3},
        Reference{0x008c09, 0x8d, 8}, Reference{0x008c0c, 0x8d, 7},
        Reference{0x008c0f, 0x9c, 7}, Reference{0x008c12, 0x9c, 7},
        Reference{0x008c15, 0xc2, 5}, Reference{0x008c17, 0xa2, 6},
        Reference{0x008c1a, 0xa0, 6}, Reference{0x008c1d, 0xa9, 6},
    };
    std::uint32_t base = 0;
    std::uint32_t wait = 0;
    std::uint32_t total = 0;
    for (const auto& item : reference) {
        const auto timing = kss::lookup_sa1_reset_timing(item.pc, item.opcode);
        assert(timing.has_value());
        assert(timing->total_cycles() == item.total);
        base += timing->base_cycles;
        wait += timing->observed_wait_cycles;
        total += timing->total_cycles();
    }
    assert(base == 57 && wait == 39 && total == 96);
}

void test_regions_and_strict_lookup() {
    const auto hardware = kss::lookup_sa1_reset_timing(0x008bf9, 0x9c);
    const auto iram = kss::lookup_sa1_reset_timing(0x008c0f, 0x9c);
    const auto bwram = kss::lookup_sa1_reset_timing(0x008c12, 0x9c);
    assert(hardware && hardware->has_data_access
        && hardware->access_region == kss::MemoryRegion::hardware_register);
    assert(iram && iram->access_region == kss::MemoryRegion::sa1_iram);
    assert(bwram && bwram->access_region == kss::MemoryRegion::bwram);

    // Same opcode at an unmeasured address and a mismatched opcode are not
    // silently assigned timing from the reset-prefix calibration.
    assert(!kss::lookup_sa1_reset_timing(0x008c20, 0x9c));
    assert(!kss::lookup_sa1_reset_timing(0x008bf4, 0xea));
}

} // namespace

int main() {
    test_private_reference_summary();
    test_regions_and_strict_lookup();
}
