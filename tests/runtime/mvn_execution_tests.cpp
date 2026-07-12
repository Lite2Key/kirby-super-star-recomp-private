#include "kss/lifted_execution.hpp"

#include <cassert>
#include <cstdint>
#include <map>
#include <vector>

namespace {

struct Access {
    bool write;
    kss::ProcessorId processor;
    std::uint32_t address;
    std::uint8_t value;
    kss::BusAccessKind kind;
};

class RecordingBus final : public kss::Bus {
public:
    std::map<std::uint32_t, std::uint8_t> bytes;
    std::vector<Access> accesses;

    std::uint8_t read8(kss::ProcessorId processor, std::uint32_t address,
        kss::BusAccessKind kind) override {
        const auto value = bytes[address & 0x00ff'ffffU];
        accesses.push_back({false, processor, address & 0x00ff'ffffU, value, kind});
        return value;
    }

    void write8(kss::ProcessorId processor, std::uint32_t address,
        std::uint8_t value, kss::BusAccessKind kind) override {
        address &= 0x00ff'ffffU;
        accesses.push_back({true, processor, address, value, kind});
        bytes[address] = value;
    }
};

kss::LiftedInstruction mvn(std::uint8_t destination, std::uint8_t source) {
    return kss::LiftedInstruction{0x54, {destination, source, 0}, 2};
}

void test_native_mvn_reenters_then_exits() {
    RecordingBus bus;
    bus.bytes[0x34fffe] = 0xa5;
    bus.bytes[0x34ffff] = 0x5a;

    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::sa1;
    cpu.program_bank = 0;
    cpu.pc = 0x8c20;
    cpu.a = 1; // A + 1 bytes are moved.
    cpu.x = 0xfffe;
    cpu.y = 0xffff;
    cpu.data_bank = 0x77;
    cpu.status = 0xc5;
    cpu.emulation = false;
    cpu.cycles = 100;

    const auto first = kss::execute_lifted(cpu, bus, mvn(0x12, 0x34));
    assert(first.status == kss::LiftStatus::executed);
    assert(first.instruction_bytes == 3 && first.instruction_cycles == 7);
    assert(cpu.pc == 0x8c20 && cpu.program_bank == 0);
    assert(cpu.a == 0 && cpu.x == 0xffff && cpu.y == 0);
    assert(cpu.data_bank == 0x12 && cpu.status == 0xc5 && !cpu.emulation);
    assert(cpu.cycles == 107);
    assert(bus.accesses.size() == 2);
    assert(!bus.accesses[0].write && bus.accesses[0].address == 0x34fffe
        && bus.accesses[0].processor == kss::ProcessorId::sa1
        && bus.accesses[0].kind == kss::BusAccessKind::data);
    assert(bus.accesses[1].write && bus.accesses[1].address == 0x12ffff
        && bus.accesses[1].value == 0xa5
        && bus.accesses[1].kind == kss::BusAccessKind::data);

    const auto second = kss::execute_lifted(cpu, bus, mvn(0x12, 0x34));
    assert(second.status == kss::LiftStatus::executed);
    assert(second.instruction_bytes == 3 && second.instruction_cycles == 7);
    assert(cpu.pc == 0x8c23 && cpu.a == 0xffff);
    assert(cpu.x == 0 && cpu.y == 1 && cpu.data_bank == 0x12);
    assert(cpu.status == 0xc5 && cpu.cycles == 114);
    assert(bus.accesses.size() == 4);
    assert(!bus.accesses[2].write && bus.accesses[2].address == 0x34ffff);
    assert(bus.accesses[3].write && bus.accesses[3].address == 0x120000
        && bus.accesses[3].value == 0x5a);
}

void test_mvn_honors_eight_bit_index_mode() {
    RecordingBus bus;
    bus.bytes[0xab00ff] = 0x42;
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::sa1;
    cpu.pc = 0x4000;
    cpu.a = 0;
    cpu.x = 0x00ff;
    cpu.y = 0x00ff;
    cpu.status = 0xf5; // X=1 plus deliberately populated unaffected flags.
    cpu.emulation = false;

    const auto result = kss::execute_lifted(cpu, bus, mvn(0xcd, 0xab));
    assert(result.status == kss::LiftStatus::executed);
    assert(cpu.pc == 0x4003 && cpu.a == 0xffff);
    assert(cpu.x == 0 && cpu.y == 0 && cpu.data_bank == 0xcd);
    assert(cpu.status == 0xf5 && !cpu.emulation && cpu.cycles == 7);
    assert(bus.accesses[0].address == 0xab00ff);
    assert(bus.accesses[1].address == 0xcd00ff && bus.accesses[1].value == 0x42);
}

void test_kss_sa1_reset_mvn_reaches_private_oracle_exit_state() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::sa1;
    cpu.pc = 0x8c20;
    cpu.a = 0x07fe;
    cpu.x = 0x3000;
    cpu.y = 0x3001;
    cpu.data_bank = 0;
    cpu.status = 0x05;
    cpu.emulation = false;

    for (std::uint32_t iteration = 0; iteration < 2047; ++iteration) {
        const auto result = kss::execute_lifted(cpu, bus, mvn(0x00, 0x00));
        assert(result.status == kss::LiftStatus::executed);
        assert(result.instruction_cycles == 7);
        assert(cpu.pc == (iteration == 2046 ? 0x8c23 : 0x8c20));
    }

    // These are instruction-boundary values from the ignored Mesen oracle.
    // The runtime counts architectural cycles; capture-specific SA-1 waits are
    // represented separately by lookup_sa1_reset_block_move_timing().
    assert(cpu.a == 0xffff && cpu.x == 0x37ff && cpu.y == 0x3800);
    assert(cpu.data_bank == 0 && cpu.status == 0x05 && !cpu.emulation);
    assert(cpu.cycles == 14329);
    assert(bus.accesses.size() == 4094);
    assert(bus.accesses.front().address == 0x003000);
    assert(bus.accesses[1].address == 0x003001);
    assert(bus.accesses[4092].address == 0x0037fe);
    assert(bus.accesses.back().address == 0x0037ff);
}

void test_mvn_invalid_encoding_is_atomic() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.pc = 0x8c20;
    cpu.a = 7;
    cpu.x = 8;
    cpu.y = 9;
    cpu.data_bank = 10;
    cpu.status = 0xa5;
    cpu.cycles = 11;
    const auto before = cpu;

    const auto result = kss::execute_lifted(
        cpu, bus, kss::LiftedInstruction{0x54, {0, 0, 0}, 1});
    assert(result.status == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.a == before.a && cpu.x == before.x
        && cpu.y == before.y && cpu.data_bank == before.data_bank
        && cpu.status == before.status && cpu.cycles == before.cycles);
    assert(bus.accesses.empty());
}

} // namespace

int main() {
    test_native_mvn_reenters_then_exits();
    test_mvn_honors_eight_bit_index_mode();
    test_kss_sa1_reset_mvn_reaches_private_oracle_exit_state();
    test_mvn_invalid_encoding_is_atomic();
}
