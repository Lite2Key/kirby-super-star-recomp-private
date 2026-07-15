#include "kss/deterministic_scheduler.hpp"
#include "kss/generated_block_runner.hpp"
#include "kss/generated_first_frame_blocks.hpp"
#include "kss/generated_reset_blocks.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    std::vector<std::uint8_t> apu_port0_reads;
    std::size_t apu_port0_index{};
    std::vector<Write> writes;
    std::uint8_t read8(kss::ProcessorId, std::uint32_t address, kss::BusAccessKind) override {
        if ((address & 0xffffU) == 0x2140U && !apu_port0_reads.empty()) {
            const auto index = apu_port0_index < apu_port0_reads.size()
                ? apu_port0_index++ : apu_port0_reads.size() - 1U;
            return apu_port0_reads[index];
        }
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
    assert(cpu.program_bank == 0 && !cpu.emulation && cpu.cycles == 1550U);
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
    assert(cpu.cycles == 22011U && bus.writes.size() == 2055U);
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
    bus.bytes[0x2140] = 0xaa;
    bus.bytes[0x2141] = 0xbb;
    kss::DeterministicScheduler scheduler;
    kss::CheckedDispatcher dispatcher;
    assert(kss::generated::first_frame_block_count() == 254U);
    assert(kss::generated::register_first_frame_blocks(dispatcher));
    assert(dispatcher.size() == 254U);

    kss::CpuContext scpu;
    scpu.processor = kss::ProcessorId::snes_cpu;
    scpu.pc = 0x8172;
    scpu.emulation = false;
    scpu.status = 0x85;
    scpu.a = 0xffff;
    scpu.x = 14;
    scpu.direct_page = 0x3700;
    scpu.stack_pointer = 0x1fff;
    scpu.cycles = 21083;
    const auto scpu_result = kss::run_generated_until(
        scpu, bus, scheduler, dispatcher,
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d66e, false, true, false), 16, 16);
    assert(scpu_result.status == kss::GeneratedRunStatus::checkpoint_reached);
    assert(scpu_result.completed_blocks == 16);
    assert(scpu.address() == 0x00d66e && scpu.cycles == 21141);
    assert(scpu.a == 0x00cc && scpu.x == 14 && scpu.y == 0);
    assert(scpu.direct_page == 0 && scpu.stack_pointer == 0x1ff8);
    assert(scpu.data_bank == 0 && scpu.status == 0xa5 && !scpu.emulation);
    assert(bus.writes.size() == 11);
    assert(bus.writes[0].address == 0x001fff && bus.writes[0].value == 0x00);
    assert(bus.writes[1].address == 0x001ffe && bus.writes[1].value == 0x81);
    assert(bus.writes[2].address == 0x001ffd && bus.writes[2].value == 0x75);
    assert(bus.writes[3].address == 0x001ffc && bus.writes[3].value == 0x37);
    assert(bus.writes[4].address == 0x001ffb && bus.writes[4].value == 0x00);
    assert(bus.writes[5].address == 0x001ffa && bus.writes[5].value == 0xd5);
    assert(bus.writes[6].address == 0x001ff9 && bus.writes[6].value == 0x68);
    assert(bus.writes[7].address == 0x000095 && bus.writes[7].value == 0x3e);
    assert(bus.writes[8].address == 0x000096 && bus.writes[8].value == 0x0a);
    assert(bus.writes[9].address == 0x000097 && bus.writes[9].value == 0xe4);
    assert(bus.writes[10].address == 0x000098 && bus.writes[10].value == 0x00);

    kss::CpuContext sa1;
    sa1.processor = kss::ProcessorId::sa1;
    sa1.pc = 0x8c36;
    sa1.emulation = false;
    sa1.status = 0;
    const auto sa1_result = kss::run_generated_until(
        sa1, bus, scheduler, dispatcher,
        kss::BlockKey::make(kss::ProcessorId::sa1, 0x008c37, false, false, false), 1);
    assert(sa1_result.status == kss::GeneratedRunStatus::checkpoint_reached);
    assert(sa1.address() == 0x008c37 && sa1.cycles == 2);
    assert(sa1.direct_page == 0 && sa1.status == 0x02 && !sa1.stopped);
}

void test_second_scpu_frontier_against_mesen_reference() {
    RecordingBus bus;
    // The direct-long pointer established by the preceding slice is
    // $E4:0A3E. Only the bounded bytes read by this slice are supplied.
    bus.bytes[0x0095] = 0x3e;
    bus.bytes[0x0096] = 0x0a;
    bus.bytes[0x0097] = 0xe4;
    bus.bytes[0x0a3e] = 0xfc;
    bus.bytes[0x0a3f] = 0x09;
    bus.bytes[0x0a40] = 0x00;
    bus.bytes[0x0a41] = 0x07;
    bus.bytes[0x0a42] = 0x20;
    bus.bytes[0x0a43] = 0xcd;
    bus.apu_port0_reads.assign(13, 0xaa);
    bus.apu_port0_reads.push_back(0xcc);
    bus.apu_port0_reads.insert(bus.apu_port0_reads.end(), 5, 0xaa);
    bus.apu_port0_reads.push_back(0x00);

    kss::DeterministicScheduler scheduler;
    kss::CheckedDispatcher dispatcher;
    assert(kss::generated::register_first_frame_blocks(dispatcher));
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::snes_cpu;
    cpu.program_bank = 0;
    cpu.pc = 0xd66e;
    cpu.a = 0x00cc;
    cpu.x = 0x000e;
    cpu.y = 0;
    cpu.direct_page = 0;
    cpu.stack_pointer = 0x1ff8;
    cpu.data_bank = 0;
    cpu.status = 0xa5;
    cpu.emulation = false;
    cpu.cycles = 21141;

    const auto result = kss::run_generated_until(
        cpu, bus, scheduler, dispatcher,
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d65a, false, true, false),
        73, 73);
    if (result.status != kss::GeneratedRunStatus::checkpoint_reached) {
        std::fprintf(stderr,
            "second frontier status=%u blocks=%zu pc=%06X cyc=%llu A=%04X X=%04X Y=%04X P=%02X reads=%zu writes=%zu\n",
            static_cast<unsigned>(result.status), result.completed_blocks,
            static_cast<unsigned>(cpu.address()), static_cast<unsigned long long>(cpu.cycles),
            cpu.a, cpu.x, cpu.y, cpu.status, bus.apu_port0_index, bus.writes.size());
        std::exit(1);
    }
    assert(result.completed_blocks == 73 && cpu.address() == 0x00d65a);
    assert(cpu.cycles == 21387 && cpu.a == 0xcd00 && cpu.x == 0x09fb && cpu.y == 6);
    assert(cpu.direct_page == 0 && cpu.stack_pointer == 0x1ff8);
    assert(cpu.data_bank == 0 && cpu.status == 0x67 && !cpu.emulation);
    assert(bus.apu_port0_index == 20 && bus.writes.size() == 7);
    assert(bus.writes[0].address == 0x001ff8 && bus.writes[0].value == 0xcc);
    assert(bus.writes[1].address == 0x002142 && bus.writes[1].value == 0x00);
    assert(bus.writes[2].address == 0x002143 && bus.writes[2].value == 0x07);
    assert(bus.writes[3].address == 0x002141 && bus.writes[3].value == 0x01);
    assert(bus.writes[4].address == 0x002140 && bus.writes[4].value == 0xcc);
    assert(bus.writes[5].address == 0x002140 && bus.writes[5].value == 0x00);
    assert(bus.writes[6].address == 0x002141 && bus.writes[6].value == 0x20);

    const auto increment = kss::run_generated_until(
        cpu, bus, scheduler, dispatcher,
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d65b, false, true, false), 1);
    assert(increment.status == kss::GeneratedRunStatus::checkpoint_reached);
    assert(cpu.address() == 0x00d65b && cpu.cycles == 21389 && !cpu.stopped);
    assert(cpu.a == 0xcd01 && cpu.status == 0x65);
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
    test_second_scpu_frontier_against_mesen_reference();
}
