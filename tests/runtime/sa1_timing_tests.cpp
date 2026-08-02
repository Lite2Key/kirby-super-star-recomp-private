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

void test_block_move_timing_is_bound_to_the_measured_run() {
    const auto timing = kss::lookup_sa1_reset_block_move_timing(
        0x008c20, 0x54, 2047);
    assert(timing.has_value());
    assert(timing->base_cycles == 14329);
    assert(timing->observed_wait_cycles == 6132);
    assert(timing->total_cycles() == 20461);

    // Repeated iterations at $00:8C20 had different waits.  Neither the
    // ordinary PC/opcode table nor a different run length may inherit the
    // aggregate as though it were a universal seven-plus-three cycle rule.
    assert(!kss::lookup_sa1_reset_timing(0x008c20, 0x54));
    assert(!kss::lookup_sa1_reset_block_move_timing(0x008c20, 0x54, 1));
    assert(!kss::lookup_sa1_reset_block_move_timing(0x008c21, 0x54, 2047));
}

void test_exact_mvn_iteration_wait_sequence() {
    std::uint32_t wait = 0;
    for (std::uint16_t iteration = 0; iteration < 2047U; ++iteration) {
        const auto timing = kss::lookup_sa1_reset_mvn_wait(
            0x008c20U, 0x54U,
            static_cast<std::uint16_t>(0x07feU - iteration),
            static_cast<std::uint16_t>(0x3000U + iteration),
            static_cast<std::uint16_t>(0x3001U + iteration));
        assert(timing.has_value());
        wait += *timing;
    }
    assert(wait == 6132U);
    assert(!kss::lookup_sa1_reset_mvn_wait(0x008c20U, 0x54U, 0x07feU, 0x3001U, 0x3001U));
    assert(!kss::lookup_sa1_reset_mvn_wait(0x008c21U, 0x54U, 0x07feU, 0x3000U, 0x3001U));
}

void test_reference_backed_post_reset_profile() {
    assert(kss::sa1_reset_release_origin_cycles() == 1454U);

    struct PostResetReference {
        std::uint32_t pc;
        std::uint8_t opcode;
        std::uint8_t total;
    };
    constexpr std::array reference{
        PostResetReference{0x008c23U, 0xa2U, 5U},
        PostResetReference{0x008c26U, 0xa0U, 6U},
        PostResetReference{0x008c29U, 0xa9U, 6U},
        PostResetReference{0x008c2fU, 0xa2U, 6U},
        PostResetReference{0x008c32U, 0x9aU, 3U},
        PostResetReference{0x008c33U, 0xa9U, 6U},
        PostResetReference{0x008c36U, 0x5bU, 2U},
        PostResetReference{0x008c37U, 0xa9U, 3U},
        PostResetReference{0x008c3aU, 0x8dU, 6U},
        PostResetReference{0x008c3dU, 0xa9U, 5U},
        PostResetReference{0x008c40U, 0x8dU, 9U},
        PostResetReference{0x008c43U, 0xa9U, 5U},
        PostResetReference{0x008c46U, 0x8dU, 7U},
        PostResetReference{0x008c49U, 0xa9U, 4U},
        PostResetReference{0x008c4cU, 0x8dU, 6U},
        PostResetReference{0x008c4fU, 0x8dU, 7U},
        PostResetReference{0x008c52U, 0x8dU, 7U},
        PostResetReference{0x008c55U, 0x8dU, 8U},
    };
    for (const auto& item : reference) {
        const auto timing = kss::lookup_sa1_post_reset_timing(item.pc, item.opcode);
        assert(timing && timing->total_cycles() == item.total);
    }
    assert(!kss::lookup_sa1_post_reset_timing(0x008c2cU, 0x54U));
    assert(!kss::lookup_sa1_post_reset_timing(0x008c23U, 0xa0U));
}

void test_second_mvn_preserves_only_the_proven_completion_total() {
    const auto timing = kss::lookup_sa1_post_reset_block_move_timing(
        0x008c2cU, 0x54U, 7935U);
    assert(timing);
    assert(timing->base_cycles == 55545U);
    assert(timing->observed_wait_cycles == 35175U);
    assert(timing->total_cycles() == 90720U);

    assert(!kss::lookup_sa1_post_reset_block_move_timing(0x008c2cU, 0x54U, 7934U));
    assert(!kss::lookup_sa1_post_reset_mvn_completion_wait(
        0x008c2cU, 0x54U, 1U, 0x7efdU, 0x7efeU));
    const auto completion = kss::lookup_sa1_post_reset_mvn_completion_wait(
        0x008c2cU, 0x54U, 0U, 0x7efeU, 0x7effU);
    assert(completion && *completion == 35175U);
}

} // namespace

int main() {
    test_private_reference_summary();
    test_regions_and_strict_lookup();
    test_block_move_timing_is_bound_to_the_measured_run();
    test_exact_mvn_iteration_wait_sequence();
    test_reference_backed_post_reset_profile();
    test_second_mvn_preserves_only_the_proven_completion_total();
}
