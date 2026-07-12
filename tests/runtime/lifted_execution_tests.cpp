#include "kss/lifted_execution.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <vector>

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

kss::LiftedInstruction instruction(std::uint8_t opcode,
    std::initializer_list<std::uint8_t> operands = {}) {
    kss::LiftedInstruction result{};
    result.opcode = opcode;
    result.operand_count = static_cast<std::uint8_t>(operands.size());
    auto index = std::size_t{0};
    for (const auto value : operands) result.operands[index++] = value;
    return result;
}

void test_implied_flag_semantics() {
    RecordingBus bus;
    struct Vector { std::uint8_t opcode; kss::StatusFlag flag; bool value; };
    constexpr std::array vectors{
        Vector{0x18, kss::StatusFlag::carry, false},
        Vector{0x38, kss::StatusFlag::carry, true},
        Vector{0x58, kss::StatusFlag::irq_disable, false},
        Vector{0x78, kss::StatusFlag::irq_disable, true},
        Vector{0xb8, kss::StatusFlag::overflow, false},
        Vector{0xd8, kss::StatusFlag::decimal, false},
        Vector{0xf8, kss::StatusFlag::decimal, true},
    };
    for (const auto& vector : vectors) {
        kss::CpuContext cpu;
        cpu.pc = 0xffff;
        cpu.cycles = 9;
        cpu.set_flag(vector.flag, !vector.value);
        const auto result = kss::execute_lifted(cpu, bus, instruction(vector.opcode));
        assert(result.status == kss::LiftStatus::executed);
        assert(result.instruction_bytes == 1 && result.instruction_cycles == 2);
        assert(cpu.pc == 0 && cpu.cycles == 11);
        assert(cpu.flag(vector.flag) == vector.value);
    }
    kss::CpuContext nop;
    nop.pc = 0x1234;
    nop.status = 0xa5;
    assert(kss::execute_lifted(nop, bus, instruction(0xea)).status == kss::LiftStatus::executed);
    assert(nop.pc == 0x1235 && nop.status == 0xa5 && nop.cycles == 2);
}

void test_mode_change_semantics() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = false;
    cpu.status = 0xff;
    cpu.x = 0xabcd;
    cpu.y = 0x9876;
    auto result = kss::execute_lifted(cpu, bus, instruction(0xc2, {0x30}));
    assert(result.instruction_bytes == 2 && result.instruction_cycles == 3);
    assert(!cpu.flag(kss::StatusFlag::accumulator_width));
    assert(!cpu.flag(kss::StatusFlag::index_width));
    assert(cpu.x == 0xabcd && cpu.y == 0x9876);

    result = kss::execute_lifted(cpu, bus, instruction(0xe2, {0x10}));
    assert(cpu.flag(kss::StatusFlag::index_width));
    assert(cpu.x == 0x00cd && cpu.y == 0x0076);

    cpu.emulation = false;
    cpu.status = 0; // C=0: XCE enters native mode's opposite, emulation remains false.
    result = kss::execute_lifted(cpu, bus, instruction(0xfb));
    assert(!cpu.emulation);
    assert(!cpu.flag(kss::StatusFlag::carry));

    cpu.emulation = true;
    cpu.status = 0; // C=0: leave emulation; old E becomes carry.
    result = kss::execute_lifted(cpu, bus, instruction(0xfb));
    assert(!cpu.emulation && cpu.flag(kss::StatusFlag::carry));

    cpu.emulation = false;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::carry);
    cpu.stack_pointer = 0xbeef;
    result = kss::execute_lifted(cpu, bus, instruction(0xfb));
    assert(cpu.emulation && !cpu.flag(kss::StatusFlag::carry));
    assert(cpu.flag(kss::StatusFlag::accumulator_width));
    assert(cpu.flag(kss::StatusFlag::index_width));
    assert(cpu.stack_pointer == 0x01ef);
}

void test_immediate_loads_both_widths() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = true;
    cpu.a = 0xab00;
    assert(kss::execute_lifted(cpu, bus, instruction(0xa9, {0x80})).status
        == kss::LiftStatus::executed);
    assert(cpu.a == 0xab80 && cpu.flag(kss::StatusFlag::negative));
    assert(!cpu.flag(kss::StatusFlag::zero) && cpu.pc == 2 && cpu.cycles == 2);
    assert(kss::execute_lifted(cpu, bus, instruction(0xa2, {0x00})).status
        == kss::LiftStatus::executed);
    assert(cpu.x == 0 && cpu.flag(kss::StatusFlag::zero));
    assert(kss::execute_lifted(cpu, bus, instruction(0xa0, {0x7f})).status
        == kss::LiftStatus::executed);
    assert(cpu.y == 0x7f && !cpu.flag(kss::StatusFlag::negative));

    cpu = {};
    cpu.emulation = false;
    cpu.status = 0;
    assert(kss::execute_lifted(cpu, bus, instruction(0xa9, {0x00, 0x80})).instruction_cycles == 3);
    assert(cpu.a == 0x8000 && cpu.flag(kss::StatusFlag::negative));
    assert(kss::execute_lifted(cpu, bus, instruction(0xa2, {0x00, 0x00})).instruction_bytes == 3);
    assert(cpu.x == 0 && cpu.flag(kss::StatusFlag::zero));
    assert(kss::execute_lifted(cpu, bus, instruction(0xa0, {0x34, 0x12})).status
        == kss::LiftStatus::executed);
    assert(cpu.y == 0x1234);
}

void test_absolute_stores_and_bus_writes() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::sa1;
    cpu.data_bank = 0x7e;
    cpu.a = 0xabcd;
    assert(kss::execute_lifted(cpu, bus, instruction(0x8d, {0xff, 0xff})).status
        == kss::LiftStatus::executed);
    assert(bus.writes.size() == 1);
    assert(bus.writes[0].processor == kss::ProcessorId::sa1);
    assert(bus.writes[0].address == 0x7effff && bus.writes[0].value == 0xcd);
    assert(bus.writes[0].kind == kss::BusAccessKind::data);

    cpu.emulation = false;
    cpu.status = 0;
    cpu.x = 0x1234;
    cpu.y = 0x5678;
    assert(kss::execute_lifted(cpu, bus, instruction(0x8e, {0xff, 0xff})).instruction_cycles == 5);
    assert(bus.writes[1].address == 0x7effff && bus.writes[1].value == 0x34);
    assert(bus.writes[2].address == 0x7e0000 && bus.writes[2].value == 0x12);
    assert(kss::execute_lifted(cpu, bus, instruction(0x8c, {0x10, 0x20})).status
        == kss::LiftStatus::executed);
    assert(bus.writes[3].value == 0x78 && bus.writes[4].value == 0x56);
    assert(kss::execute_lifted(cpu, bus, instruction(0x9c, {0x20, 0x20})).status
        == kss::LiftStatus::executed);
    assert(bus.writes[5].value == 0 && bus.writes[6].value == 0);
}

void test_control_flow_and_rejection_are_atomic() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.program_bank = 0x80;
    cpu.pc = 0xfffe;
    cpu.cycles = 4;
    assert(kss::execute_lifted(cpu, bus, instruction(0x80, {0xfe})).status
        == kss::LiftStatus::executed);
    assert(cpu.pc == 0xfffe && cpu.program_bank == 0x80 && cpu.cycles == 7);
    assert(kss::execute_lifted(cpu, bus, instruction(0x4c, {0x34, 0x12})).status
        == kss::LiftStatus::executed);
    assert(cpu.pc == 0x1234 && cpu.program_bank == 0x80 && cpu.cycles == 10);

    const auto before = cpu;
    auto result = kss::execute_lifted(cpu, bus, instruction(0xa9, {0x12, 0x34}));
    assert(result.status == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles && cpu.a == before.a);
    result = kss::execute_lifted(cpu, bus, instruction(0x00));
    assert(result.status == kss::LiftStatus::unsupported_opcode);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles);
}

void test_stack_and_direct_page_semantics() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = true;
    cpu.x = 0x12ab;
    cpu.stack_pointer = 0x0100;
    assert(kss::execute_lifted(cpu, bus, instruction(0x9a)).instruction_cycles == 2);
    assert(cpu.stack_pointer == 0x01ab);
    cpu.program_bank = 0x80;
    assert(kss::execute_lifted(cpu, bus, instruction(0x4b)).instruction_cycles == 3);
    assert(bus.writes.back().address == 0x01ab && bus.writes.back().value == 0x80);
    assert(bus.writes.back().kind == kss::BusAccessKind::stack);
    assert(cpu.stack_pointer == 0x01aa);
    assert(kss::execute_lifted(cpu, bus, instruction(0xab)).instruction_cycles == 4);
    assert(cpu.data_bank == 0x80 && cpu.flag(kss::StatusFlag::negative));
    assert(cpu.stack_pointer == 0x01ab);

    cpu.emulation = false;
    cpu.stack_pointer = 0x0000;
    assert(kss::execute_lifted(cpu, bus, instruction(0xf4, {0x34, 0x12})).instruction_cycles == 5);
    assert(bus.writes[1].address == 0x0000 && bus.writes[1].value == 0x12);
    assert(bus.writes[2].address == 0xffff && bus.writes[2].value == 0x34);
    assert(cpu.stack_pointer == 0xfffe);
    assert(kss::execute_lifted(cpu, bus, instruction(0x2b)).instruction_cycles == 5);
    assert(cpu.direct_page == 0x1234 && cpu.stack_pointer == 0x0000);
    assert(!cpu.flag(kss::StatusFlag::zero) && !cpu.flag(kss::StatusFlag::negative));

    cpu.direct_page = 0x12ff;
    cpu.a = 0xabcd;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
    assert(kss::execute_lifted(cpu, bus, instruction(0x85, {0x02})).instruction_cycles == 4);
    assert(bus.writes[3].address == 0x1301 && bus.writes[3].value == 0xcd);
    cpu.status = 0;
    assert(kss::execute_lifted(cpu, bus, instruction(0x64, {0xff})).instruction_cycles == 5);
    assert(bus.writes[4].address == 0x13fe && bus.writes[4].value == 0);
    assert(bus.writes[5].address == 0x13ff && bus.writes[5].value == 0);

    const auto before = cpu;
    const auto write_count = bus.writes.size();
    assert(kss::execute_lifted(cpu, bus, instruction(0xf4, {0x12})).status
        == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles && cpu.stack_pointer == before.stack_pointer);
    assert(bus.writes.size() == write_count);
    assert(kss::execute_lifted(cpu, bus, instruction(0x85)).status
        == kss::LiftStatus::invalid_encoding);
    assert(bus.writes.size() == write_count);
}

void test_kss_scpu_reset_prefix_against_mesen_reference() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::snes_cpu;
    cpu.pc = 0x8004;
    cpu.status = 0x34;
    cpu.emulation = true;

    const auto check = [&](std::uint16_t pc, std::uint8_t status, bool emulation,
                           std::uint16_t x, std::uint64_t cycles) {
        assert(cpu.pc == pc && cpu.status == status && cpu.emulation == emulation);
        assert(cpu.x == x && cpu.cycles == cycles && bus.writes.empty());
    };
    assert(kss::execute_lifted(cpu, bus, instruction(0x78)).status == kss::LiftStatus::executed);
    check(0x8005, 0x34, true, 0, 2);
    assert(kss::execute_lifted(cpu, bus, instruction(0x18)).status == kss::LiftStatus::executed);
    check(0x8006, 0x34, true, 0, 4);
    assert(kss::execute_lifted(cpu, bus, instruction(0xfb)).status == kss::LiftStatus::executed);
    check(0x8007, 0x35, false, 0, 6);
    assert(kss::execute_lifted(cpu, bus, instruction(0xe2, {0x20})).status == kss::LiftStatus::executed);
    check(0x8009, 0x35, false, 0, 9);
    assert(kss::execute_lifted(cpu, bus, instruction(0xc2, {0x10})).status == kss::LiftStatus::executed);
    check(0x800b, 0x25, false, 0, 12);
    assert(kss::execute_lifted(cpu, bus, instruction(0xa2, {0xff, 0x1f})).status == kss::LiftStatus::executed);
    check(0x800e, 0x25, false, 0x1fff, 15);
    assert(kss::execute_lifted(cpu, bus, instruction(0x9a)).status == kss::LiftStatus::executed);
    assert(cpu.pc == 0x800f && cpu.stack_pointer == 0x1fff && cpu.cycles == 17);
    assert(kss::execute_lifted(cpu, bus, instruction(0x4b)).status == kss::LiftStatus::executed);
    assert(cpu.pc == 0x8010 && cpu.stack_pointer == 0x1ffe && cpu.cycles == 20);
    assert(bus.writes.size() == 1 && bus.writes[0].address == 0x001fff
        && bus.writes[0].value == 0 && bus.writes[0].kind == kss::BusAccessKind::stack);
    assert(kss::execute_lifted(cpu, bus, instruction(0xab)).status == kss::LiftStatus::executed);
    assert(cpu.pc == 0x8011 && cpu.stack_pointer == 0x1fff && cpu.data_bank == 0);
    assert(cpu.status == 0x27 && cpu.cycles == 24);
    assert(kss::execute_lifted(cpu, bus, instruction(0x9c, {0x00, 0x42})).status
        == kss::LiftStatus::executed);
    assert(cpu.pc == 0x8014 && cpu.cycles == 28);
    assert(bus.writes[1].address == 0x004200 && bus.writes[1].value == 0);
    assert(kss::execute_lifted(cpu, bus, instruction(0xf4, {0x00, 0x21})).status
        == kss::LiftStatus::executed);
    assert(cpu.pc == 0x8017 && cpu.stack_pointer == 0x1ffd && cpu.cycles == 33);
    assert(bus.writes[2].address == 0x001fff && bus.writes[2].value == 0x21);
    assert(bus.writes[3].address == 0x001ffe && bus.writes[3].value == 0x00);
    assert(kss::execute_lifted(cpu, bus, instruction(0x2b)).status == kss::LiftStatus::executed);
    assert(cpu.pc == 0x8018 && cpu.direct_page == 0x2100 && cpu.stack_pointer == 0x1fff);
    assert(cpu.status == 0x25 && cpu.cycles == 38);
    assert(kss::execute_lifted(cpu, bus, instruction(0xa9, {0x8f})).status
        == kss::LiftStatus::executed);
    assert(cpu.pc == 0x801a && cpu.a == 0x008f && cpu.status == 0xa5 && cpu.cycles == 40);
    assert(kss::execute_lifted(cpu, bus, instruction(0x85, {0x00})).status
        == kss::LiftStatus::executed);
    assert(cpu.pc == 0x801c && cpu.cycles == 43);
    assert(bus.writes[4].address == 0x002100 && bus.writes[4].value == 0x8f);
    assert(kss::execute_lifted(cpu, bus, instruction(0xa9, {0x63})).status
        == kss::LiftStatus::executed);
    assert(cpu.pc == 0x801e && cpu.a == 0x0063 && cpu.status == 0x25 && cpu.cycles == 45);
    assert(kss::execute_lifted(cpu, bus, instruction(0x85, {0x01})).status
        == kss::LiftStatus::executed);
    assert(cpu.pc == 0x8020 && cpu.cycles == 48);
    assert(bus.writes[5].address == 0x002101 && bus.writes[5].value == 0x63);
}

} // namespace

int main() {
    test_implied_flag_semantics();
    test_mode_change_semantics();
    test_immediate_loads_both_widths();
    test_absolute_stores_and_bus_writes();
    test_control_flow_and_rejection_are_atomic();
    test_stack_and_direct_page_semantics();
    test_kss_scpu_reset_prefix_against_mesen_reference();
    return 0;
}
