#include "kss/dsp_brr_decoder.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <span>

namespace {

using kss::apu::dsp::BrrBlockDecoder;
using kss::apu::dsp::BrrDecodeStatus;
using kss::apu::dsp::BrrHistory;

void test_filter_zero_nibble_order_flags_and_exact_count() {
    constexpr std::array<std::uint8_t, 9> block{
        0x13, 0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
    }; // range=1, filter=0, loop+end
    constexpr std::array<std::int16_t, 16> expected{
        0,2,4,6,8,10,12,14,-16,-14,-12,-10,-8,-6,-4,-2,
    };
    const auto result = BrrBlockDecoder::decode(block);
    assert(result.status == BrrDecodeStatus::decoded);
    assert(result.samples == expected && result.samples.size() == 16);
    assert(result.range == 1 && result.filter == 0 && result.end && result.loop);
    assert((result.history == BrrHistory{-2,-4}));
}

void test_filters_one_through_three() {
    constexpr std::array<std::array<std::int16_t, 16>, 3> expected{{
        {11250,10546,9886,9268,8688,8144,7634,7156,
         6708,6288,5894,5524,5178,4854,4550,4264},
        {30374,-18886,1058,19720,-28938,0,27128,-13824,
         13750,-26366,2382,29258,-11998,15234,-25250,3120},
        {28062,-24864,0,20202,-29236,0,23754,-22854,
         5168,27854,-19686,7530,29524,-18606,8112,29692},
    }};
    for (std::uint8_t filter = 1; filter <= 3; ++filter) {
        std::array<std::uint8_t, 9> block{};
        block[0] = static_cast<std::uint8_t>(filter << 2U);
        const auto result = BrrBlockDecoder::decode(block, {12000,-8000});
        assert(result.status == BrrDecodeStatus::decoded);
        assert(result.filter == filter && result.samples == expected[filter - 1U]);
        assert(result.history.previous1 == result.samples[15]);
        assert(result.history.previous2 == result.samples[14]);
    }
}

void test_range_thirteen_through_fifteen_and_saturation_wrap() {
    for (std::uint8_t range = 13; range <= 15; ++range) {
        std::array<std::uint8_t, 9> block{};
        block[0] = static_cast<std::uint8_t>(range << 4U);
        for (std::size_t index = 1; index < block.size(); ++index) block[index] = 0x7f;
        const auto result = BrrBlockDecoder::decode(block);
        for (std::size_t index = 0; index < result.samples.size(); index += 2) {
            assert(result.samples[index] == 0);
            assert(result.samples[index + 1] == -4096);
        }
    }

    std::array<std::uint8_t, 9> saturated{};
    saturated[0] = 0xc8; // range=12, filter=2
    saturated[1] = 0x70;
    constexpr std::array<std::int16_t, 16> expected{
        -2,-30724,6968,-23452,14296,-16300,21060,-10112,
        26516,-5510,30172,-2858,31798,-2244,31446,-3492,
    };
    const auto result = BrrBlockDecoder::decode(saturated, {32767,-32768});
    assert(result.samples == expected);
    assert(result.samples[0] == -2); // +32767 saturates before hardware doubling wraps.
}

void test_range_twelve_extremes_and_independent_flags() {
    std::array<std::uint8_t, 9> block{};
    block[0] = 0xc0;
    for (std::size_t index = 1; index < block.size(); ++index) block[index] = 0x78;
    const auto result = BrrBlockDecoder::decode(block);
    for (std::size_t index = 0; index < result.samples.size(); index += 2) {
        assert(result.samples[index] == 28672);
        assert(result.samples[index + 1] == -32768);
    }

    for (std::uint8_t flags = 0; flags < 4; ++flags) {
        block[0] = flags;
        const auto flagged = BrrBlockDecoder::decode(block);
        assert(flagged.end == ((flags & 1U) != 0));
        assert(flagged.loop == ((flags & 2U) != 0));
    }
}

void test_invalid_size_fails_without_samples() {
    constexpr std::array<std::uint8_t, 8> short_block{};
    const auto result = BrrBlockDecoder::decode(short_block, {123,-456});
    assert(result.status == BrrDecodeStatus::invalid_block_size);
    assert((result.history == BrrHistory{123,-456}));
    for (const auto sample : result.samples) assert(sample == 0);
}

} // namespace

int main() {
    test_filter_zero_nibble_order_flags_and_exact_count();
    test_filters_one_through_three();
    test_range_thirteen_through_fifteen_and_saturation_wrap();
    test_range_twelve_extremes_and_independent_flags();
    test_invalid_size_fails_without_samples();
}
