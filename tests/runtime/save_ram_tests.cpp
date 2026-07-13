#include "kss/dual_bus.hpp"
#include "kss/save_ram.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path unique_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path()
        / ("kss-save-test-" + std::to_string(stamp));
}

void test_missing_round_trip_and_replace() {
    const auto root = unique_root();
    const auto path = root / "nested" / "kirby.srm";
    std::array<std::uint8_t, kss::kKssSaveRamSize> bytes{};
    bytes.fill(0x7a);
    auto result = kss::load_save_ram(path, bytes);
    assert(result.status == kss::SaveRamStatus::initialized_empty);
    assert(std::all_of(bytes.begin(), bytes.end(), [](auto value) { return value == 0; }));

    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>((index * 37U) & 0xffU);
    }
    assert(kss::store_save_ram(path, bytes).status == kss::SaveRamStatus::stored);
    std::array<std::uint8_t, kss::kKssSaveRamSize> loaded{};
    assert(kss::load_save_ram(path, loaded).status == kss::SaveRamStatus::loaded);
    assert(loaded == bytes);

    bytes.fill(0xa5);
    assert(kss::store_save_ram(path, bytes).status == kss::SaveRamStatus::stored);
    assert(kss::load_save_ram(path, loaded).status == kss::SaveRamStatus::loaded);
    assert(loaded == bytes);
    assert(!std::filesystem::exists(path.string() + ".tmp"));
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_default_path_and_clean_shutdown_commit() {
    assert(kss::default_save_ram_path("Kirby Super Star (USA).sfc")
        == std::filesystem::path("Kirby Super Star (USA).srm"));
    assert(kss::default_save_ram_path("rom-without-extension")
        == std::filesystem::path("rom-without-extension.srm"));

    const auto root = unique_root();
    const auto path = root / "override" / "slot-a.srm";
    std::array<std::uint8_t, kss::kKssSaveRamSize> bytes{};
    bytes.fill(0x39);
    const auto result = kss::finalize_save_ram(
        path, bytes, kss::SaveRamLifecycleOutcome::clean_shutdown);
    assert(result.status == kss::SaveRamStatus::stored);
    std::array<std::uint8_t, kss::kKssSaveRamSize> loaded{};
    assert(kss::load_save_ram(path, loaded).status == kss::SaveRamStatus::loaded);
    assert(loaded == bytes);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_unsafe_lifecycle_outcomes_never_write() {
    const auto root = unique_root();
    std::filesystem::create_directories(root);
    const auto established_path = root / "established.srm";
    std::array<std::uint8_t, kss::kKssSaveRamSize> established{};
    established.fill(0x17);
    assert(kss::store_save_ram(established_path, established).ok());
    std::array<std::uint8_t, kss::kKssSaveRamSize> candidate{};
    candidate.fill(0xe4);

    constexpr std::array unsafe_outcomes{
        kss::SaveRamLifecycleOutcome::validation_only,
        kss::SaveRamLifecycleOutcome::rom_rejected,
        kss::SaveRamLifecycleOutcome::execution_failed,
        kss::SaveRamLifecycleOutcome::translated_frontier,
    };
    for (const auto outcome : unsafe_outcomes) {
        const auto result = kss::finalize_save_ram(established_path, candidate, outcome);
        assert(result.status == kss::SaveRamStatus::not_stored);
        std::array<std::uint8_t, kss::kKssSaveRamSize> observed{};
        assert(kss::load_save_ram(established_path, observed).ok());
        assert(observed == established);
    }

    const auto absent_path = root / "never-created" / "unsafe.srm";
    assert(kss::finalize_save_ram(absent_path, candidate,
        kss::SaveRamLifecycleOutcome::translated_frontier).status
        == kss::SaveRamStatus::not_stored);
    assert(!std::filesystem::exists(absent_path));
    assert(!std::filesystem::exists(absent_path.parent_path()));
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_persistent_bwram_window_is_bus_visible() {
    const std::array<std::uint8_t, 1> synthetic_rom{};
    kss::RomBackedDualBus bus(synthetic_rom);
    auto persistent = bus.persistent_bwram();
    static_assert(decltype(persistent)::extent == kss::kKssSaveRamSize);
    persistent[0x123] = 0xa6;
    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x006123) == 0xa6);
    assert(bus.read8(kss::ProcessorId::sa1, 0x006123) == 0xa6);
    assert(bus.read8(kss::ProcessorId::sa1, 0x400123) == 0xa6);

    bus.write8(kss::ProcessorId::snes_cpu, 0x007fff, 0x5d);
    assert(persistent.back() == 0x5d);
    bus.write8(kss::ProcessorId::sa1, 0x400321, 0xc2);
    assert(persistent[0x321] == 0xc2);

    // Canonical $2000 is the first volatile byte and must not leak into .srm.
    bus.write8(kss::ProcessorId::sa1, 0x402000, 0x88);
    assert(bus.bwram()[kss::kKssSaveRamSize] == 0x88);
    const auto& const_bus = bus;
    assert(const_bus.persistent_bwram().size() == kss::kKssSaveRamSize);
}

void test_wrong_size_does_not_mutate_destination() {
    const auto root = unique_root();
    std::filesystem::create_directories(root);
    const auto path = root / "bad.srm";
    {
        std::ofstream output(path, std::ios::binary);
        output.put('\x01');
    }
    std::array<std::uint8_t, kss::kKssSaveRamSize> bytes{};
    bytes.fill(0x5c);
    const auto result = kss::load_save_ram(path, bytes);
    assert(result.status == kss::SaveRamStatus::wrong_file_size && result.file_size == 1U);
    assert(std::all_of(bytes.begin(), bytes.end(), [](auto value) { return value == 0x5c; }));
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_invalid_buffer_sizes_fail_closed() {
    const auto path = unique_root() / "bad.srm";
    std::array<std::uint8_t, 3> bytes{};
    assert(kss::load_save_ram(path, bytes).status == kss::SaveRamStatus::invalid_buffer_size);
    assert(kss::store_save_ram(path, bytes).status == kss::SaveRamStatus::invalid_buffer_size);
}

} // namespace

int main() {
    test_missing_round_trip_and_replace();
    test_default_path_and_clean_shutdown_commit();
    test_unsafe_lifecycle_outcomes_never_write();
    test_persistent_bwram_window_is_bus_visible();
    test_wrong_size_does_not_mutate_destination();
    test_invalid_buffer_sizes_fail_closed();
}
