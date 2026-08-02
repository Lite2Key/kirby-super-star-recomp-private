#include "kss/scpu_micro_access_recorder.hpp"
#include "kss/lifted_execution.hpp"

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

void test_native_message_poll_microphase_oracle() {
    ValueBus values;
    kss::ScpuMicroAccessRecorder recorder(values);
    kss::CpuContext cpu{};
    cpu.processor = kss::ProcessorId::snes_cpu;
    cpu.emulation = false;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::zero);
    cpu.pc = 0x0014U;
    cpu.a = 0;

    // Native-mode BIT $2300 is the exact first half of the measured message
    // wait.  Keep the generated fetches explicit: the lifted instruction
    // bytes are already decoded and therefore do not pass through read8().
    kss::observe_generated_instruction_fetches(
        recorder, kss::ProcessorId::snes_cpu, 0x0014U, 2);
    const auto bit = kss::execute_lifted(
        cpu, recorder, kss::LiftedInstruction{0x2cU, {0x00U, 0x23U, 0x00U}, 2});
    assert(bit.status == kss::LiftStatus::executed);
    assert(bit.instruction_bytes == 3U && bit.instruction_cycles == 5U);
    assert(cpu.pc == 0x0017U && cpu.cycles == 5U);

    const auto after_bit = recorder.accesses();
    assert(after_bit.size() == 5U);
    assert(after_bit[0] == kss::ScpuMicroAccess{
        0x0014U, kss::BusAccessKind::opcode, kss::BusAccessDirection::read, false});
    assert(after_bit[1] == kss::ScpuMicroAccess{
        0x0015U, kss::BusAccessKind::operand, kss::BusAccessDirection::read, false});
    assert(after_bit[2] == kss::ScpuMicroAccess{
        0x0016U, kss::BusAccessKind::operand, kss::BusAccessDirection::read, false});
    assert(after_bit[3] == kss::ScpuMicroAccess{
        0x2300U, kss::BusAccessKind::data, kss::BusAccessDirection::read, false});
    assert(after_bit[4] == kss::ScpuMicroAccess{
        0x2301U, kss::BusAccessKind::data, kss::BusAccessDirection::read, false});

    // With the latch still zero, BEQ takes the two-byte backward branch.
    kss::observe_generated_instruction_fetches(
        recorder, kss::ProcessorId::snes_cpu, 0x0017U, 1);
    const auto branch = kss::execute_lifted(
        cpu, recorder, kss::LiftedInstruction{0xf0U, {0xfbU, 0x00U, 0x00U}, 1});
    assert(branch.status == kss::LiftStatus::executed);
    assert(branch.instruction_bytes == 2U && branch.instruction_cycles == 3U);
    assert(cpu.pc == 0x0014U && cpu.cycles == 8U);

    const auto accesses = recorder.accesses();
    assert(accesses.size() == 7U);
    assert(accesses[5] == kss::ScpuMicroAccess{
        0x0017U, kss::BusAccessKind::opcode, kss::BusAccessDirection::read, false});
    assert(accesses[6] == kss::ScpuMicroAccess{
        0x0018U, kss::BusAccessKind::operand, kss::BusAccessDirection::read, false});

    // This is the current public contract: bus cycles only, with no invented
    // wait penalty.  A future scoped microphase model must change this oracle
    // deliberately and preserve all access ordering above.
    kss::MultiClockCoordinator clocks;
    const auto advance = clocks.account_scpu_accesses(recorder.timing_accesses());
    assert(advance.status == kss::CoordinatorStatus::accepted);
    assert(advance.master_clocks == 52U && advance.ready_at == 52U);
}

} // namespace

int main() {
    test_value_free_order_and_exact_master_total();
    test_native_message_poll_microphase_oracle();
}
