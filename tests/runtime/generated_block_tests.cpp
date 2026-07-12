#include "kss/deterministic_scheduler.hpp"
#include "kss/generated_block_runner.hpp"
#include "kss/generated_first_frame_blocks.hpp"
#include "kss/generated_reset_blocks.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace {

struct Write {
    kss::ProcessorId processor;
    std::uint32_t address;
    std::uint8_t value;
    kss::BusAccessKind kind;
};

class RecordingBus final : public kss::Bus {
public:
    std::array<std::uint8_t, 0x10000> bytes{};
    std::vector<Write> writes;
    std::uint8_t read8(kss::ProcessorId, std::uint32_t address, kss::BusAccessKind) override {
        return bytes[address & 0xffffU];
    }
    void write8(kss::ProcessorId processor, std::uint32_t address,
        std::uint8_t value, kss::BusAccessKind kind) override {
        writes.push_back({processor, address, value, kind});
        bytes[address & 0xffffU] = value;
    }
};

void test_registration_is_exact_and_duplicate_safe() {
    kss::CheckedDispatcher dispatcher;
    assert(kss::generated::reset_block_count() == 183U);
    assert(kss::generated::register_reset_blocks(dispatcher));
    assert(dispatcher.size() == 183U);
    assert(!kss::generated::register_reset_blocks(dispatcher));
    assert(dispatcher.size() == 183U);
    assert(dispatcher.contains(kss::BlockKey::make(
        kss::ProcessorId::snes_cpu, 0x008004, true, true, true)));
    assert(dispatcher.contains(kss::BlockKey::make(
        kss::ProcessorId::sa1, 0x008bf4, true, true, true)));
    // The restartable MVN boundary is a registered self-loop.
    assert(dispatcher.contains(kss::BlockKey::make(
        kss::ProcessorId::sa1, 0x008c20, false, false, false)));
}

void test_scpu_complete_prefix_checkpoint() {
    RecordingBus bus;
    kss::DeterministicScheduler scheduler;
    kss::CheckedDispatcher dispatcher;
    assert(kss::generated::register_reset_blocks(dispatcher));
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::snes_cpu;
    cpu.pc = 0x8004;
    cpu.status = 0x34;
    cpu.emulation = true;
    const auto checkpoint = kss::BlockKey::make(
        kss::ProcessorId::snes_cpu, 0x008020, false, true, false);
    const auto result = kss::run_generated_until(
        cpu, bus, scheduler, dispatcher, checkpoint, 16);
    assert(result.status == kss::GeneratedRunStatus::checkpoint_reached);
    assert(result.completed_blocks == 16U);
    assert(cpu.pc == 0x8020 && cpu.program_bank == 0 && cpu.cycles == 48);
    assert(cpu.a == 0x0063 && cpu.x == 0x1fff && cpu.direct_page == 0x2100);
    assert(cpu.status == 0x25 && !cpu.emulation && cpu.stack_pointer == 0x1fff);
    assert(bus.writes.size() == 6U);
    assert(bus.writes[0].address == 0x001fff && bus.writes[0].value == 0);
    assert(bus.writes[4].address == 0x002100 && bus.writes[4].value == 0x8f);
    assert(bus.writes[5].address == 0x002101 && bus.writes[5].value == 0x63);
}

void test_scpu_full_reset_block_checkpoint() {
    RecordingBus bus;
    kss::DeterministicScheduler scheduler;
    kss::CheckedDispatcher dispatcher;
    assert(kss::generated::register_reset_blocks(dispatcher));
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::snes_cpu;
    cpu.pc = 0x8004;
    cpu.status = 0x34;
    cpu.emulation = true;
    const auto checkpoint = kss::BlockKey::make(
        kss::ProcessorId::snes_cpu, 0x00816d, false, false, false);
    const auto result = kss::run_generated_until(
        cpu, bus, scheduler, dispatcher, checkpoint, 160, 160);
    if (result.status != kss::GeneratedRunStatus::checkpoint_reached
        || result.completed_blocks != 160U || cpu.address() != 0x00816d) {
        std::fprintf(stderr, "full S-CPU result status=%u blocks=%zu pc=%06X cycles=%llu\n",
            static_cast<unsigned>(result.status), result.completed_blocks,
            static_cast<unsigned>(cpu.address()), static_cast<unsigned long long>(cpu.cycles));
    }
    assert(result.status == kss::GeneratedRunStatus::checkpoint_reached);
    assert(result.completed_blocks == 160U);
    assert(cpu.processor == kss::ProcessorId::snes_cpu && cpu.address() == 0x00816d);
    assert(cpu.cycles == 516U);
}

void test_sa1_complete_prefix_checkpoint() {
    RecordingBus bus;
    kss::DeterministicScheduler scheduler;
    kss::CheckedDispatcher dispatcher;
    assert(kss::generated::register_reset_blocks(dispatcher));
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::sa1;
    cpu.pc = 0x8bf4;
    cpu.status = 0x34;
    cpu.emulation = true;
    const auto checkpoint = kss::BlockKey::make(
        kss::ProcessorId::sa1, 0x008c20, false, false, false);
    const auto result = kss::run_generated_until(
        cpu, bus, scheduler, dispatcher, checkpoint, 18);
    assert(result.status == kss::GeneratedRunStatus::checkpoint_reached);
    assert(result.completed_blocks == 18U);
    assert(cpu.processor == kss::ProcessorId::sa1 && cpu.pc == 0x8c20);
    assert(cpu.program_bank == 0 && !cpu.emulation && cpu.cycles == 96);
    assert(cpu.a == 0x07fe && cpu.x == 0x3000 && cpu.y == 0x3001);
    assert(cpu.status == 0x05);
}

void test_sa1_full_reset_block_checkpoint() {
    RecordingBus bus;
    kss::DeterministicScheduler scheduler;
    kss::CheckedDispatcher dispatcher;
    assert(kss::generated::register_reset_blocks(dispatcher));
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::sa1;
    cpu.pc = 0x8bf4;
    cpu.status = 0x34;
    cpu.emulation = true;
    const auto checkpoint = kss::BlockKey::make(
        kss::ProcessorId::sa1, 0x008c23, false, false, false);
    const auto result = kss::run_generated_until(
        cpu, bus, scheduler, dispatcher, checkpoint, 2065, 2065);
    assert(result.status == kss::GeneratedRunStatus::checkpoint_reached);
    assert(result.completed_blocks == 2065U);
    assert(cpu.address() == 0x008c23 && cpu.a == 0xffff);
    assert(cpu.x == 0x37ff && cpu.y == 0x3800 && cpu.data_bank == 0);
    assert(cpu.cycles == 20557U && bus.writes.size() == 2055U);
}

void test_wrong_identity_and_bounds_fail_closed() {
    RecordingBus bus;
    kss::DeterministicScheduler scheduler;
    kss::CheckedDispatcher dispatcher;
    assert(kss::generated::register_reset_blocks(dispatcher));
    kss::CpuContext wrong_processor;
    wrong_processor.processor = kss::ProcessorId::sa1;
    wrong_processor.pc = 0x8004;
    assert(dispatcher.dispatch(wrong_processor, bus, scheduler) == kss::DispatchStatus::unknown_block);

    kss::CpuContext cpu;
    cpu.pc = 0x8004;
    cpu.status = 0x34;
    const auto distant = kss::BlockKey::make(
        kss::ProcessorId::snes_cpu, 0x008020, false, true, false);
    const auto bounded = kss::run_generated_until(cpu, bus, scheduler, dispatcher, distant, 3);
    assert(bounded.status == kss::GeneratedRunStatus::step_limit);
    assert(bounded.completed_blocks == 3U && cpu.pc == 0x8007);
}

void test_first_frame_frontier_registers_and_fails_closed() {
    RecordingBus bus;
    kss::DeterministicScheduler scheduler;
    kss::CheckedDispatcher dispatcher;
    assert(kss::generated::first_frame_block_count() == 254U);
    assert(kss::generated::register_first_frame_blocks(dispatcher));
    assert(dispatcher.size() == 254U);

    kss::CpuContext scpu;
    scpu.processor = kss::ProcessorId::snes_cpu;
    scpu.pc = 0x8172;
    scpu.emulation = false;
    scpu.status = 0;
    const auto scpu_result = kss::run_generated_until(
        scpu, bus, scheduler, dispatcher,
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d559, false, false, false), 1);
    assert(scpu_result.status == kss::GeneratedRunStatus::generated_block_failed_closed);
    assert(scpu.address() == 0x008172 && scpu.cycles == 0);

    kss::CpuContext sa1;
    sa1.processor = kss::ProcessorId::sa1;
    sa1.pc = 0x8c36;
    sa1.emulation = false;
    sa1.status = 0;
    const auto sa1_result = kss::run_generated_until(
        sa1, bus, scheduler, dispatcher,
        kss::BlockKey::make(kss::ProcessorId::sa1, 0x008c37, false, false, false), 1);
    assert(sa1_result.status == kss::GeneratedRunStatus::generated_block_failed_closed);
    assert(sa1.address() == 0x008c36 && sa1.cycles == 0);
}

} // namespace

int main() {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    test_registration_is_exact_and_duplicate_safe();
    test_scpu_complete_prefix_checkpoint();
    test_scpu_full_reset_block_checkpoint();
    test_sa1_complete_prefix_checkpoint();
    test_sa1_full_reset_block_checkpoint();
    test_wrong_identity_and_bounds_fail_closed();
    test_first_frame_frontier_registers_and_fails_closed();
}
