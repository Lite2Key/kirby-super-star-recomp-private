#include "kss/scpu_micro_access_recorder.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <unordered_map>

namespace {

class ValueBus final : public kss::Bus {
public:
    std::uint8_t read8(
        kss::ProcessorId,
        std::uint32_t address,
        kss::BusAccessKind) override {
        const auto found = bytes.find(address & 0x00ff'ffffU);
        return found == bytes.end() ? 0 : found->second;
    }

    void write8(
        kss::ProcessorId,
        std::uint32_t address,
        std::uint8_t value,
        kss::BusAccessKind) override {
        bytes[address & 0x00ff'ffffU] = value;
    }

    std::unordered_map<std::uint32_t, std::uint8_t> bytes{};
};

void test_value_free_order_and_exact_master_total() {
    ValueBus values;
    values.bytes[0x002100] = 0xa5;
    kss::ScpuMicroAccessRecorder recorder(values);

    kss::observe_generated_instruction_fetches(
        recorder, kss::ProcessorId::snes_cpu, 0x80ffff, 2);
    assert(recorder.read8(kss::ProcessorId::snes_cpu, 0x002100,
        kss::BusAccessKind::data) == 0xa5);
    recorder.write8(kss::ProcessorId::snes_cpu, 0x00420d, 0x01,
        kss::BusAccessKind::data);
    kss::observe_generated_instruction_fetches(
        recorder, kss::ProcessorId::snes_cpu, 0xc08000, 0);
    (void)recorder.read8(kss::ProcessorId::sa1, 0x001234, kss::BusAccessKind::data);

    const auto accesses = recorder.accesses();
    assert(accesses.size() == 6);
    assert((accesses[0] == kss::ScpuMicroAccess{0x80ffff, kss::BusAccessKind::opcode,
        kss::BusAccessDirection::read, false}));
    assert((accesses[1] == kss::ScpuMicroAccess{0x800000, kss::BusAccessKind::operand,
        kss::BusAccessDirection::read, false}));
    assert((accesses[2] == kss::ScpuMicroAccess{0x800001, kss::BusAccessKind::operand,
        kss::BusAccessDirection::read, false}));
    assert((accesses[3] == kss::ScpuMicroAccess{0x002100, kss::BusAccessKind::data,
        kss::BusAccessDirection::read, false}));
    assert((accesses[4] == kss::ScpuMicroAccess{0x00420d, kss::BusAccessKind::data,
        kss::BusAccessDirection::write, false}));
    assert((accesses[5] == kss::ScpuMicroAccess{0xc08000, kss::BusAccessKind::opcode,
        kss::BusAccessDirection::read, true}));
    assert(recorder.fast_rom_enabled());

    // 3 slow cartridge fetches + B-bus read + MEMSEL write + FastROM fetch.
    assert(recorder.master_clocks() == 42);
    kss::MultiClockCoordinator clocks;
    const auto advance = clocks.account_scpu_accesses(recorder.timing_accesses());
    assert(advance.status == kss::CoordinatorStatus::accepted);
    assert(advance.master_clocks == 42 && advance.ready_at == 42);
}

} // namespace

int main() {
    test_value_free_order_and_exact_master_total();
}
