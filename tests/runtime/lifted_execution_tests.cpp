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

struct Read {
    kss::ProcessorId processor;
    std::uint32_t address;
    kss::BusAccessKind kind;
};

class RecordingBus final : public kss::Bus {
public:
    std::array<std::uint8_t, 0x10000> bytes{};
    std::vector<Read> reads;
    std::vector<Write> writes;

    std::uint8_t read8(kss::ProcessorId processor, std::uint32_t address,
        kss::BusAccessKind kind) override {
        reads.push_back({processor, address, kind});
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
    assert(bus.writes[2].address == 0x7f0000 && bus.writes[2].value == 0x12);
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
    assert(cpu.pc == 0xfffe && cpu.program_bank == 0x80 && cpu.cycles == 8);
    assert(kss::execute_lifted(cpu, bus, instruction(0x4c, {0x34, 0x12})).status
        == kss::LiftStatus::executed);
    assert(cpu.pc == 0x1234 && cpu.program_bank == 0x80 && cpu.cycles == 11);

    const auto before = cpu;
    auto result = kss::execute_lifted(cpu, bus, instruction(0xa9, {0x12, 0x34}));
    assert(result.status == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles && cpu.a == before.a);
    result = kss::execute_lifted(cpu, bus, instruction(0x00));
    assert(result.status == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles);
}

void test_absolute_load_and_bpl_semantics() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::sa1;
    cpu.emulation = false;
    cpu.status = 0;
    cpu.data_bank = 0x7e;
    cpu.pc = 0x2000;
    bus.bytes[0xffff] = 0x34;
    bus.bytes[0x0000] = 0x92;
    auto result = kss::execute_lifted(cpu, bus, instruction(0xad, {0xff, 0xff}));
    assert(result.status == kss::LiftStatus::executed);
    assert(result.instruction_bytes == 3 && result.instruction_cycles == 5);
    assert(cpu.a == 0x9234 && cpu.pc == 0x2003 && cpu.cycles == 5);
    assert(cpu.flag(kss::StatusFlag::negative) && !cpu.flag(kss::StatusFlag::zero));
    assert(bus.reads.size() == 2);
    assert(bus.reads[0].processor == kss::ProcessorId::sa1
        && bus.reads[0].address == 0x7effff);
    assert(bus.reads[1].address == 0x7f0000);
    assert(bus.reads[0].kind == kss::BusAccessKind::data);

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
    cpu.a = 0xab00;
    cpu.data_bank = 0x12;
    bus.bytes[0x3456] = 0;
    result = kss::execute_lifted(cpu, bus, instruction(0xad, {0x56, 0x34}));
    assert(result.instruction_cycles == 4 && cpu.a == 0xab00);
    assert(cpu.flag(kss::StatusFlag::zero) && !cpu.flag(kss::StatusFlag::negative));
    assert(bus.reads.back().address == 0x123456);

    cpu.program_bank = 0x81;
    cpu.pc = 0x8170;
    cpu.cycles = 0;
    cpu.status = 0;
    result = kss::execute_lifted(cpu, bus, instruction(0x10, {0xfb}));
    assert(result.instruction_cycles == 3 && cpu.pc == 0x816d && cpu.program_bank == 0x81);

    cpu.pc = 0x8170;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::negative);
    result = kss::execute_lifted(cpu, bus, instruction(0x10, {0xfb}));
    assert(result.instruction_cycles == 2 && cpu.pc == 0x8172);

    cpu.emulation = true;
    cpu.status = 0;
    cpu.pc = 0x00fd;
    result = kss::execute_lifted(cpu, bus, instruction(0x10, {0x01}));
    assert(result.instruction_cycles == 4 && cpu.pc == 0x0100);
    cpu.emulation = false;
    cpu.pc = 0xfffe;
    result = kss::execute_lifted(cpu, bus, instruction(0x10, {0x01}));
    assert(result.instruction_cycles == 3 && cpu.pc == 0x0001
        && cpu.program_bank == 0x81);

    const auto before = cpu;
    result = kss::execute_lifted(cpu, bus, instruction(0xad, {0x00}));
    assert(result.status == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles && cpu.a == before.a);
    result = kss::execute_lifted(cpu, bus, instruction(0x10));
    assert(result.status == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles);
}

void test_call_stack_compare_and_transfer_semantics() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::snes_cpu;
    cpu.emulation = false;
    cpu.status = 0x85;
    cpu.program_bank = 0;
    cpu.pc = 0x8172;
    cpu.stack_pointer = 0x1fff;
    cpu.direct_page = 0x3700;
    cpu.cycles = 21083;
    auto result = kss::execute_lifted(cpu, bus, instruction(0x22, {0x59, 0xd5, 0x00}));
    assert(result.instruction_bytes == 4 && result.instruction_cycles == 8);
    assert(cpu.address() == 0x00d559 && cpu.stack_pointer == 0x1ffc && cpu.cycles == 21091);
    assert(bus.writes.size() == 3);
    assert(bus.writes[0].address == 0x001fff && bus.writes[0].value == 0x00);
    assert(bus.writes[1].address == 0x001ffe && bus.writes[1].value == 0x81);
    assert(bus.writes[2].address == 0x001ffd && bus.writes[2].value == 0x75);

    result = kss::execute_lifted(cpu, bus, instruction(0x0b));
    assert(result.instruction_cycles == 4 && cpu.pc == 0xd55a);
    assert(cpu.stack_pointer == 0x1ffa && cpu.cycles == 21095);
    assert(bus.writes[3].address == 0x001ffc && bus.writes[3].value == 0x37);
    assert(bus.writes[4].address == 0x001ffb && bus.writes[4].value == 0x00);

    cpu.a = 0;
    result = kss::execute_lifted(cpu, bus, instruction(0x5b));
    assert(result.instruction_cycles == 2 && cpu.direct_page == 0);
    assert(cpu.flag(kss::StatusFlag::zero) && !cpu.flag(kss::StatusFlag::negative));

    cpu.a = 0xbbaa;
    cpu.status = 0x85;
    result = kss::execute_lifted(cpu, bus, instruction(0xc9, {0xaa, 0xbb}));
    assert(result.instruction_cycles == 3);
    assert(cpu.flag(kss::StatusFlag::carry) && cpu.flag(kss::StatusFlag::zero));
    assert(!cpu.flag(kss::StatusFlag::negative));
    cpu.pc = 0xd564;
    result = kss::execute_lifted(cpu, bus, instruction(0xd0, {0x03}));
    assert(result.instruction_cycles == 2 && cpu.pc == 0xd566);

    cpu.status = static_cast<std::uint8_t>(cpu.status & ~static_cast<std::uint8_t>(kss::StatusFlag::zero));
    cpu.pc = 0xd564;
    result = kss::execute_lifted(cpu, bus, instruction(0xd0, {0x03}));
    assert(result.instruction_cycles == 3 && cpu.pc == 0xd569);

    cpu.pc = 0xd566;
    cpu.stack_pointer = 0x1ffa;
    result = kss::execute_lifted(cpu, bus, instruction(0x20, {0x35, 0xd6}));
    assert(result.instruction_cycles == 6 && cpu.pc == 0xd635 && cpu.program_bank == 0);
    assert(cpu.stack_pointer == 0x1ff8);
    assert(bus.writes[5].address == 0x001ffa && bus.writes[5].value == 0xd5);
    assert(bus.writes[6].address == 0x001ff9 && bus.writes[6].value == 0x68);

    const auto before = cpu;
    const auto write_count = bus.writes.size();
    result = kss::execute_lifted(cpu, bus, instruction(0x22, {0x59, 0xd5}));
    assert(result.status == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.stack_pointer == before.stack_pointer);
    assert(bus.writes.size() == write_count);
}

void test_architecture_control_flow_family() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = false;
    cpu.program_bank = 0x12;
    cpu.pc = 0x8000;
    auto result = kss::execute_lifted(cpu, bus, instruction(0x5c, {0x78, 0x56, 0x34}));
    assert(result.instruction_bytes == 4 && result.instruction_cycles == 4);
    assert(cpu.address() == 0x345678 && cpu.cycles == 4);

    bus.bytes[0xffff] = 0x34;
    bus.bytes[0x0000] = 0x12;
    cpu.program_bank = 0x56;
    cpu.pc = 0x9000;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x6c, {0xff, 0xff}));
    assert(result.instruction_cycles == 5 && cpu.pc == 0x1234 && cpu.program_bank == 0x56);
    assert(bus.reads.size() == 2 && bus.reads[0].address == 0x00ffff
        && bus.reads[1].address == 0x000000);

    bus.bytes[0x0000] = 0x78;
    bus.bytes[0x0001] = 0x56;
    cpu.program_bank = 0x7e;
    cpu.x = 2;
    cpu.pc = 0x9000;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x7c, {0xfe, 0xff}));
    assert(result.instruction_cycles == 6 && cpu.pc == 0x5678 && cpu.program_bank == 0x7e);
    assert(bus.reads[0].address == 0x7e0000 && bus.reads[1].address == 0x7e0001);

    bus.bytes[0xfffe] = 0x78;
    bus.bytes[0xffff] = 0x56;
    bus.bytes[0x0000] = 0x34;
    cpu.pc = 0x9000;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0xdc, {0xfe, 0xff}));
    assert(result.instruction_cycles == 6 && cpu.address() == 0x345678);
    assert(bus.reads.size() == 3 && bus.reads[2].address == 0x000000);

    bus.bytes[0xfffe] = 0xbc;
    bus.bytes[0xffff] = 0x9a;
    cpu.program_bank = 0x12;
    cpu.pc = 0x8000;
    cpu.x = 2;
    cpu.stack_pointer = 0x1fff;
    bus.reads.clear();
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0xfc, {0xfc, 0xff}));
    assert(result.instruction_cycles == 8 && cpu.pc == 0x9abc && cpu.program_bank == 0x12);
    assert(cpu.stack_pointer == 0x1ffd && bus.reads[0].address == 0x12fffe);
    assert(bus.writes.size() == 2 && bus.writes[0].value == 0x80
        && bus.writes[1].value == 0x02);

    cpu = {};
    cpu.emulation = false;
    cpu.program_bank = 0x44;
    cpu.stack_pointer = 0x1ffd;
    bus.bytes[0x1ffe] = 0x34;
    bus.bytes[0x1fff] = 0x12;
    result = kss::execute_lifted(cpu, bus, instruction(0x60));
    assert(result.instruction_cycles == 6 && cpu.pc == 0x1235
        && cpu.program_bank == 0x44 && cpu.stack_pointer == 0x1fff);

    cpu.stack_pointer = 0x1ffc;
    bus.bytes[0x1ffd] = 0xff;
    bus.bytes[0x1ffe] = 0xff;
    bus.bytes[0x1fff] = 0xab;
    result = kss::execute_lifted(cpu, bus, instruction(0x6b));
    assert(result.instruction_cycles == 6 && cpu.pc == 0 && cpu.program_bank == 0xab);

    cpu.emulation = false;
    cpu.stack_pointer = 0x1ffb;
    cpu.x = 0xabcd;
    bus.bytes[0x1ffc] = 0x10;
    bus.bytes[0x1ffd] = 0x34;
    bus.bytes[0x1ffe] = 0x12;
    bus.bytes[0x1fff] = 0x7e;
    result = kss::execute_lifted(cpu, bus, instruction(0x40));
    assert(result.instruction_cycles == 7 && cpu.address() == 0x7e1234);
    assert(cpu.status == 0x10 && cpu.x == 0x00cd && cpu.stack_pointer == 0x1fff);

    cpu = {};
    cpu.emulation = true;
    cpu.program_bank = 0x66;
    cpu.stack_pointer = 0x01fc;
    cpu.x = 0xabcd;
    cpu.y = 0x9876;
    bus.bytes[0x01fd] = 0;
    bus.bytes[0x01fe] = 0x78;
    bus.bytes[0x01ff] = 0x56;
    result = kss::execute_lifted(cpu, bus, instruction(0x40));
    assert(result.instruction_cycles == 6 && cpu.pc == 0x5678 && cpu.program_bank == 0x66);
    assert(cpu.status == 0x30 && cpu.x == 0x00cd && cpu.y == 0x0076);

    cpu.emulation = false;
    cpu.program_bank = 0x55;
    cpu.pc = 0xfffe;
    result = kss::execute_lifted(cpu, bus, instruction(0x82, {0xff, 0xff}));
    assert(result.instruction_cycles == 4 && cpu.pc == 0x0000 && cpu.program_bank == 0x55);

    const auto before = cpu;
    const auto read_count = bus.reads.size();
    const auto write_count = bus.writes.size();
    assert(kss::execute_lifted(cpu, bus, instruction(0xdc, {0x00})).status
        == kss::LiftStatus::invalid_encoding);
    assert(kss::execute_lifted(cpu, bus, instruction(0x40, {0x00})).status
        == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles);
    assert(bus.reads.size() == read_count && bus.writes.size() == write_count);
}

void test_remaining_conditional_branches() {
    RecordingBus bus;
    struct BranchVector { std::uint8_t opcode; kss::StatusFlag flag; bool set; };
    constexpr std::array vectors{
        BranchVector{0x30, kss::StatusFlag::negative, true},
        BranchVector{0x50, kss::StatusFlag::overflow, false},
        BranchVector{0x90, kss::StatusFlag::carry, false},
        BranchVector{0xb0, kss::StatusFlag::carry, true},
        BranchVector{0xf0, kss::StatusFlag::zero, true},
    };
    for (const auto& vector : vectors) {
        kss::CpuContext cpu;
        cpu.emulation = true;
        cpu.program_bank = 0x88;
        cpu.pc = 0x00fd;
        cpu.set_flag(vector.flag, vector.set);
        auto result = kss::execute_lifted(cpu, bus, instruction(vector.opcode, {0x01}));
        assert(result.instruction_cycles == 4 && cpu.pc == 0x0100 && cpu.program_bank == 0x88);
        cpu.pc = 0x00fd;
        cpu.cycles = 0;
        cpu.set_flag(vector.flag, !vector.set);
        result = kss::execute_lifted(cpu, bus, instruction(vector.opcode, {0x01}));
        assert(result.instruction_cycles == 2 && cpu.pc == 0x00ff);
    }
}

void test_accumulator_alu_families_and_addressing() {
    RecordingBus bus;
    struct ImmediateVector {
        std::uint8_t opcode;
        std::uint8_t left;
        std::uint8_t right;
        std::uint8_t result;
    };
    constexpr std::array immediate_vectors{
        ImmediateVector{0x09, 0x50, 0x0f, 0x5f}, // ORA
        ImmediateVector{0x29, 0xf3, 0x0f, 0x03}, // AND
        ImmediateVector{0x49, 0xf0, 0xff, 0x0f}, // EOR
        ImmediateVector{0x69, 0x7f, 0x01, 0x80}, // ADC
        ImmediateVector{0xc9, 0x10, 0x20, 0x10}, // CMP does not store
        ImmediateVector{0xe9, 0x00, 0x01, 0xff}, // SBC, with C set below
    };
    for (const auto& vector : immediate_vectors) {
        kss::CpuContext cpu;
        cpu.emulation = false;
        cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
        cpu.set_flag(kss::StatusFlag::carry, vector.opcode == 0xe9);
        cpu.a = static_cast<std::uint16_t>(0xab00U | vector.left);
        const auto result = kss::execute_lifted(
            cpu, bus, instruction(vector.opcode, {vector.right}));
        assert(result.status == kss::LiftStatus::executed);
        assert(result.instruction_bytes == 2 && result.instruction_cycles == 2);
        assert(cpu.a == static_cast<std::uint16_t>(0xab00U | vector.result));
    }

    kss::CpuContext cpu;
    cpu.emulation = false;
    cpu.status = 0;
    cpu.a = 0x7fff;
    auto result = kss::execute_lifted(cpu, bus, instruction(0x69, {0x01, 0x00}));
    assert(result.instruction_bytes == 3 && result.instruction_cycles == 3);
    assert(cpu.a == 0x8000 && cpu.flag(kss::StatusFlag::overflow));
    assert(cpu.flag(kss::StatusFlag::negative));

    // In emulation mode with D.low=0, direct indexed addressing wraps within
    // the direct-page page before the data read.
    cpu = {};
    cpu.emulation = true;
    cpu.direct_page = 0x1200;
    cpu.x = 0x00ff;
    cpu.a = 0xaa00;
    bus.bytes[0x1201] = 0x0f;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x15, {0x02})); // ORA dp,X
    assert(result.instruction_cycles == 4 && cpu.a == 0xaa0f);
    assert(bus.reads.size() == 1 && bus.reads[0].address == 0x001201);

    cpu.emulation = false;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
    cpu.direct_page = 0x12f0;
    cpu.a = 0xaaff;
    bus.bytes[0x1310] = 0x0f;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x25, {0x20})); // AND dp
    assert(result.instruction_cycles == 4 && cpu.a == 0xaa0f);
    assert(bus.reads[0].address == 0x001310);

    // Non-direct word reads advance linearly across the 24-bit bus, including
    // an absolute DB boundary and the top-of-address-space long boundary.
    cpu.status = 0;
    cpu.data_bank = 0x34;
    cpu.a = 0xffff;
    bus.bytes[0xffff] = 0x0f;
    bus.bytes[0x0000] = 0xf0;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x4d, {0xff, 0xff})); // EOR abs
    assert(result.instruction_cycles == 5 && cpu.a == 0x0ff0);
    assert(bus.reads[0].address == 0x34ffff && bus.reads[1].address == 0x350000);

    cpu.a = 0;
    cpu.set_flag(kss::StatusFlag::carry, false);
    bus.bytes[0xffff] = 0x01;
    bus.bytes[0x0000] = 0x00;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x6f, {0xff, 0xff, 0xff})); // ADC long
    assert(result.instruction_cycles == 6 && cpu.a == 1);
    assert(bus.reads[0].address == 0xffffff && bus.reads[1].address == 0x000000);

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width)
        | static_cast<std::uint8_t>(kss::StatusFlag::index_width);
    cpu.data_bank = 0x7e;
    cpu.x = 1;
    cpu.a = 0x5510;
    bus.bytes[0x1300] = 0x20;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0xdd, {0xff, 0x12})); // CMP abs,X
    assert(result.instruction_cycles == 5 && cpu.a == 0x5510);
    assert(bus.reads[0].address == 0x7e1300 && !cpu.flag(kss::StatusFlag::carry));

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
    cpu.y = 1; // 16-bit index mode adds a cycle even without a page crossing.
    cpu.a = 0x5503;
    bus.bytes[0x1201] = 0x01;
    result = kss::execute_lifted(cpu, bus, instruction(0xf9, {0x00, 0x12})); // SBC abs,Y
    assert(result.instruction_cycles == 5 && static_cast<std::uint8_t>(cpu.a) == 0x01);

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width)
        | static_cast<std::uint8_t>(kss::StatusFlag::index_width);
    cpu.x = 2;
    cpu.a = 0x55f0;
    bus.bytes[0x0001] = 0x0f;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x3f, {0xff, 0xff, 0xff})); // AND long,X
    assert(result.instruction_cycles == 5 && cpu.a == 0x5500);
    assert(bus.reads[0].address == 0x000001);

    const auto before = cpu;
    const auto read_count = bus.reads.size();
    assert(kss::execute_lifted(cpu, bus, instruction(0x0f, {0x00, 0x00})).status
        == kss::LiftStatus::invalid_encoding);
    assert(kss::execute_lifted(cpu, bus, instruction(0x09, {0x01, 0x02})).status
        == kss::LiftStatus::invalid_encoding);
    assert(cpu.a == before.a && cpu.pc == before.pc && cpu.cycles == before.cycles
        && cpu.status == before.status && bus.reads.size() == read_count);
}

void test_load_store_index_families_and_addressing() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = false;
    cpu.status = 0; // 16-bit accumulator and index registers.
    cpu.x = 2;
    bus.bytes[0xffff] = 0x34;
    bus.bytes[0x0000] = 0x12;
    auto result = kss::execute_lifted(
        cpu, bus, instruction(0xbf, {0xfd, 0xff, 0xff})); // LDA long,X
    assert(result.instruction_bytes == 4 && result.instruction_cycles == 6);
    assert(cpu.a == 0x1234 && bus.reads[0].address == 0xffffff
        && bus.reads[1].address == 0x000000);

    // Emulation direct-index addressing wraps the effective offset within D's page.
    cpu = {};
    cpu.emulation = true;
    cpu.direct_page = 0x1200;
    cpu.y = 0x00ff;
    bus.bytes[0x1201] = 0x80;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0xb6, {0x02})); // LDX dp,Y
    assert(result.instruction_cycles == 4 && cpu.x == 0x80);
    assert(cpu.flag(kss::StatusFlag::negative) && bus.reads[0].address == 0x001201);

    // A 16-bit index mode incurs both the indexed-address idle and word-data cycle.
    cpu = {};
    cpu.emulation = false;
    cpu.status = 0;
    cpu.data_bank = 0x7e;
    cpu.x = 1;
    bus.bytes[0x1201] = 0x78;
    bus.bytes[0x1202] = 0x56;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0xbc, {0x00, 0x12})); // LDY abs,X
    assert(result.instruction_cycles == 6 && cpu.y == 0x5678);
    assert(bus.reads[0].address == 0x7e1201 && bus.reads[1].address == 0x7e1202);

    // Indexed stores always take the write idle, even without a page crossing.
    cpu.status = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width)
        | static_cast<std::uint8_t>(kss::StatusFlag::index_width));
    cpu.a = 0xabcd;
    cpu.x = 1;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x9d, {0x00, 0x12})); // STA abs,X
    assert(result.instruction_cycles == 5 && bus.writes.size() == 1);
    assert(bus.writes[0].address == 0x7e1201 && bus.writes[0].value == 0xcd);

    cpu.status = 0;
    cpu.a = 0xbeef;
    bus.writes.clear();
    result = kss::execute_lifted(
        cpu, bus, instruction(0x8f, {0xff, 0xff, 0xff})); // STA long
    assert(result.instruction_cycles == 6 && bus.writes.size() == 2);
    assert(bus.writes[0].address == 0xffffff && bus.writes[0].value == 0xef);
    assert(bus.writes[1].address == 0x000000 && bus.writes[1].value == 0xbe);

    cpu.direct_page = 0x12f0;
    cpu.y = 0x20;
    cpu.x = 0x1234;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x96, {0x00})); // STX dp,Y
    assert(result.instruction_cycles == 6 && bus.writes.size() == 2);
    assert(bus.writes[0].address == 0x001310 && bus.writes[0].value == 0x34);
    assert(bus.writes[1].address == 0x001311 && bus.writes[1].value == 0x12);

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::index_width);
    cpu.data_bank = 0x34;
    cpu.y = 0xab78;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x8c, {0x00, 0x20})); // STY abs
    assert(result.instruction_cycles == 4 && bus.writes.size() == 1);
    assert(bus.writes[0].address == 0x342000 && bus.writes[0].value == 0x78);

    cpu.status = 0;
    cpu.direct_page = 0x1201;
    cpu.x = 1;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x74, {0xfe})); // STZ dp,X
    assert(result.instruction_cycles == 6 && bus.writes.size() == 2);
    assert(bus.writes[0].address == 0x001300 && bus.writes[0].value == 0);
    assert(bus.writes[1].address == 0x001301 && bus.writes[1].value == 0);

    const auto before = cpu;
    const auto read_count = bus.reads.size();
    const auto write_count = bus.writes.size();
    assert(kss::execute_lifted(cpu, bus, instruction(0xaf, {0x00, 0x00})).status
        == kss::LiftStatus::invalid_encoding);
    assert(kss::execute_lifted(cpu, bus, instruction(0x86, {0x00, 0x01})).status
        == kss::LiftStatus::invalid_encoding);
    assert(cpu.a == before.a && cpu.x == before.x && cpu.y == before.y
        && cpu.pc == before.pc && cpu.cycles == before.cycles
        && cpu.status == before.status && bus.reads.size() == read_count
        && bus.writes.size() == write_count);
}

void test_rmw_bit_and_index_compare_families() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = true;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
    cpu.direct_page = 0x1201;
    bus.bytes[0x1221] = 0x81;
    auto result = kss::execute_lifted(cpu, bus, instruction(0x06, {0x20})); // ASL dp
    assert(result.instruction_cycles == 6 && cpu.flag(kss::StatusFlag::carry));
    assert(bus.writes.size() == 2 && bus.writes[0].value == 0x81
        && bus.writes[1].value == 0x02); // old-byte dummy write, then result

    cpu.emulation = false;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
    bus.bytes[0x1221] = 0x02;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x46, {0x20})); // LSR dp
    assert(result.instruction_cycles == 6 && bus.writes.size() == 1
        && bus.writes[0].value == 0x01); // native M=1 idles instead of dummy-writing

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::carry);
    cpu.data_bank = 0xff;
    cpu.x = 2;
    bus.bytes[0xffff] = 0x01;
    bus.bytes[0x0000] = 0x00;
    bus.reads.clear();
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x7e, {0xfd, 0xff})); // ROR abs,X
    assert(result.instruction_cycles == 9 && cpu.flag(kss::StatusFlag::carry));
    assert(bus.reads[0].address == 0xffffff && bus.reads[1].address == 0x000000);
    assert(bus.writes[0].address == 0x000000 && bus.writes[0].value == 0x80);
    assert(bus.writes[1].address == 0xffffff && bus.writes[1].value == 0x00);

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width)
        | static_cast<std::uint8_t>(kss::StatusFlag::negative)
        | static_cast<std::uint8_t>(kss::StatusFlag::overflow);
    cpu.a = 0xab0f;
    result = kss::execute_lifted(cpu, bus, instruction(0x89, {0xf0})); // BIT immediate
    assert(result.instruction_cycles == 2 && cpu.flag(kss::StatusFlag::zero));
    assert(cpu.flag(kss::StatusFlag::negative) && cpu.flag(kss::StatusFlag::overflow));

    cpu.status = 0;
    cpu.a = 0x0fff;
    cpu.data_bank = 0x34;
    bus.bytes[0xffff] = 0x00;
    bus.bytes[0x0000] = 0xc0;
    result = kss::execute_lifted(cpu, bus, instruction(0x2c, {0xff, 0xff})); // BIT abs
    assert(result.instruction_cycles == 5 && cpu.flag(kss::StatusFlag::zero));
    assert(cpu.flag(kss::StatusFlag::negative) && cpu.flag(kss::StatusFlag::overflow));

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
    cpu.emulation = true;
    cpu.direct_page = 0;
    cpu.a = 0xab0f;
    bus.bytes[0x0020] = 0xf3;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x14, {0x20})); // TRB dp
    assert(result.instruction_cycles == 5 && !cpu.flag(kss::StatusFlag::zero));
    assert(bus.writes[0].value == 0xf3 && bus.writes[1].value == 0xf0);

    cpu.emulation = false;
    cpu.status = 0;
    cpu.a = 0x0f0f;
    cpu.data_bank = 0x12;
    bus.bytes[0x2000] = 0x00;
    bus.bytes[0x2001] = 0xf0;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x0c, {0x00, 0x20})); // TSB abs
    assert(result.instruction_cycles == 8 && bus.writes.size() == 2);
    assert(bus.writes[0].address == 0x122001 && bus.writes[0].value == 0xff);
    assert(bus.writes[1].address == 0x122000 && bus.writes[1].value == 0x0f);

    cpu.status = 0;
    cpu.direct_page = 0x1201;
    cpu.y = 0x1234;
    bus.bytes[0x1221] = 0x34;
    bus.bytes[0x1222] = 0x12;
    result = kss::execute_lifted(cpu, bus, instruction(0xc4, {0x20})); // CPY dp
    assert(result.instruction_cycles == 5 && cpu.flag(kss::StatusFlag::zero)
        && cpu.flag(kss::StatusFlag::carry) && cpu.y == 0x1234);

    const auto before = cpu;
    const auto reads = bus.reads.size();
    const auto writes = bus.writes.size();
    assert(kss::execute_lifted(cpu, bus, instruction(0x1e, {0x00})).status
        == kss::LiftStatus::invalid_encoding);
    assert(kss::execute_lifted(cpu, bus, instruction(0xec, {0x00})).status
        == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles && cpu.status == before.status
        && bus.reads.size() == reads && bus.writes.size() == writes);
}

void test_transfer_and_stack_operation_families() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = false;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::index_width);
    cpu.a = 0xab80;
    auto result = kss::execute_lifted(cpu, bus, instruction(0xa8)); // TAY
    assert(result.instruction_cycles == 2 && cpu.y == 0x0080
        && cpu.flag(kss::StatusFlag::negative));
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
    cpu.x = 0x127f;
    cpu.a = 0xab00;
    result = kss::execute_lifted(cpu, bus, instruction(0x8a)); // TXA
    assert(cpu.a == 0xab7f && !cpu.flag(kss::StatusFlag::negative));

    cpu.emulation = true;
    cpu.a = 0xabcd;
    cpu.status = 0x31;
    result = kss::execute_lifted(cpu, bus, instruction(0x1b)); // TCS, no flags
    assert(cpu.stack_pointer == 0x01cd && cpu.status == 0x31);
    cpu.direct_page = 0x8000;
    result = kss::execute_lifted(cpu, bus, instruction(0x7b)); // TDC always 16-bit
    assert(cpu.a == 0x8000 && cpu.flag(kss::StatusFlag::negative));

    cpu = {};
    cpu.emulation = false;
    cpu.status = 0;
    cpu.x = 0x1234;
    cpu.stack_pointer = 0x1fff;
    result = kss::execute_lifted(cpu, bus, instruction(0xda)); // PHX 16
    assert(result.instruction_cycles == 4 && cpu.stack_pointer == 0x1ffd);
    assert(bus.writes[bus.writes.size() - 2].value == 0x12
        && bus.writes.back().value == 0x34);
    cpu.x = 0;
    result = kss::execute_lifted(cpu, bus, instruction(0xfa)); // PLX 16
    assert(result.instruction_cycles == 5 && cpu.x == 0x1234
        && cpu.stack_pointer == 0x1fff);

    cpu.emulation = true;
    cpu.status = 0x30;
    cpu.stack_pointer = 0x0100;
    result = kss::execute_lifted(cpu, bus, instruction(0x08)); // PHP wraps E stack
    assert(result.instruction_cycles == 3 && cpu.stack_pointer == 0x01ff);
    cpu.status = 0xff;
    result = kss::execute_lifted(cpu, bus, instruction(0x28)); // PLP forces M/X
    assert(result.instruction_cycles == 4 && cpu.status == 0x30
        && cpu.stack_pointer == 0x0100);

    cpu.emulation = false;
    cpu.direct_page = 0x1201;
    cpu.stack_pointer = 0x1fff;
    bus.bytes[0x1221] = 0x78;
    bus.bytes[0x1222] = 0x56;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0xd4, {0x20})); // PEI
    assert(result.instruction_cycles == 7 && bus.writes[0].value == 0x56
        && bus.writes[1].value == 0x78);

    cpu.pc = 0x1000;
    cpu.stack_pointer = 0x1fff;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x62, {0xfd, 0xff})); // PER -3
    assert(result.instruction_cycles == 6 && cpu.pc == 0x1003);
    assert(bus.writes[0].value == 0x10 && bus.writes[1].value == 0x00);

    const auto before = cpu;
    const auto writes = bus.writes.size();
    assert(kss::execute_lifted(cpu, bus, instruction(0xda, {0x00})).status
        == kss::LiftStatus::invalid_encoding);
    assert(kss::execute_lifted(cpu, bus, instruction(0xd4)).status
        == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.stack_pointer == before.stack_pointer
        && cpu.cycles == before.cycles && bus.writes.size() == writes);
}

void test_remaining_indirect_accumulator_addressing() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = true;
    cpu.status = 0x30;
    cpu.direct_page = 0x1201;
    cpu.data_bank = 0x7e;
    cpu.a = 0xabf0;
    bus.bytes[0x12ff] = 0x34;
    bus.bytes[0x1200] = 0x12;
    bus.bytes[0x1234] = 0x0f;
    auto result = kss::execute_lifted(cpu, bus, instruction(0x01, {0xfe})); // ORA (dp,X)
    assert(result.instruction_cycles == 7 && cpu.a == 0xabff);
    assert(bus.reads[0].address == 0x0012ff && bus.reads[1].address == 0x001200
        && bus.reads[2].address == 0x7e1234); // E-mode indexed-pointer page bug

    cpu.emulation = false;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
    cpu.stack_pointer = 0x1fff;
    cpu.a = 0xabf3;
    bus.bytes[0x2000] = 0x0f;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x23, {0x01})); // AND sr
    assert(result.instruction_cycles == 4 && cpu.a == 0xab03
        && bus.reads[0].address == 0x002000);

    cpu.direct_page = 0x1200;
    cpu.a = 0xabf0;
    bus.bytes[0x12ff] = 0x00;
    bus.bytes[0x1300] = 0x20;
    bus.bytes[0x1301] = 0x34;
    bus.bytes[0x2000] = 0x0f;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x47, {0xff})); // EOR [dp]
    assert(result.instruction_cycles == 6 && cpu.a == 0xabff);
    assert(bus.reads[0].address == 0x0012ff && bus.reads[1].address == 0x001300
        && bus.reads[2].address == 0x001301 && bus.reads[3].address == 0x342000);

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width)
        | static_cast<std::uint8_t>(kss::StatusFlag::index_width);
    cpu.direct_page = 0;
    cpu.data_bank = 0x7e;
    cpu.y = 1;
    cpu.a = 0xab01;
    bus.bytes[0x0020] = 0xff;
    bus.bytes[0x0021] = 0x12;
    bus.bytes[0x1300] = 0x02;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x71, {0x20})); // ADC (dp),Y
    assert(result.instruction_cycles == 6 && cpu.a == 0xab03);
    assert(bus.reads.back().address == 0x7e1300);

    cpu.a = 0xab55;
    bus.bytes[0x0020] = 0x00;
    bus.bytes[0x0021] = 0x20;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x92, {0x20})); // STA (dp)
    assert(result.instruction_cycles == 5 && bus.writes.size() == 1);
    assert(bus.writes[0].address == 0x7e2000 && bus.writes[0].value == 0x55);

    cpu.stack_pointer = 0xfffe;
    cpu.y = 1;
    bus.bytes[0xffff] = 0xff;
    bus.bytes[0x0000] = 0x1f;
    bus.bytes[0x2000] = 0x80;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0xb3, {0x01})); // LDA (sr),Y
    assert(result.instruction_cycles == 7 && cpu.a == 0xab80);
    assert(bus.reads[0].address == 0x00ffff && bus.reads[1].address == 0x000000
        && bus.reads[2].address == 0x7e2000);

    cpu.direct_page = 0;
    cpu.a = 0xab80;
    bus.bytes[0x0030] = 0x00;
    bus.bytes[0x0031] = 0x20;
    bus.bytes[0x2000] = 0x80;
    result = kss::execute_lifted(cpu, bus, instruction(0xd2, {0x30})); // CMP (dp)
    assert(result.instruction_cycles == 5 && cpu.flag(kss::StatusFlag::zero)
        && cpu.flag(kss::StatusFlag::carry) && cpu.a == 0xab80);

    cpu.a = 0xab05;
    cpu.y = 1;
    cpu.set_flag(kss::StatusFlag::carry, true);
    bus.bytes[0x0040] = 0xff;
    bus.bytes[0x0041] = 0xff;
    bus.bytes[0x0042] = 0xff;
    bus.bytes[0x0000] = 0x01;
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0xf7, {0x40})); // SBC [dp],Y
    assert(result.instruction_cycles == 6 && cpu.a == 0xab04);
    assert(bus.reads.back().address == 0x000000);

    const auto before = cpu;
    const auto reads = bus.reads.size();
    const auto writes = bus.writes.size();
    assert(kss::execute_lifted(cpu, bus, instruction(0xa1, {0x00, 0x01})).status
        == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles && cpu.a == before.a
        && bus.reads.size() == reads && bus.writes.size() == writes);
}

void test_interrupt_stop_and_final_implied_operations() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = false;
    cpu.program_bank = 0x80;
    cpu.pc = 0x1234;
    cpu.stack_pointer = 0x1fff;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::decimal);
    bus.bytes[0xffe6] = 0x78;
    bus.bytes[0xffe7] = 0x56;
    auto result = kss::execute_lifted(cpu, bus, instruction(0x00, {0x99})); // BRK
    assert(result.instruction_bytes == 2 && result.instruction_cycles == 8);
    assert(cpu.address() == 0x005678 && cpu.stack_pointer == 0x1ffb);
    assert(bus.writes[0].value == 0x80 && bus.writes[1].value == 0x12
        && bus.writes[2].value == 0x36);
    assert(bus.reads[0].kind == kss::BusAccessKind::vector
        && bus.reads[0].address == 0x00ffe6);
    assert(cpu.flag(kss::StatusFlag::irq_disable)
        && !cpu.flag(kss::StatusFlag::decimal));

    cpu = {};
    cpu.emulation = true;
    cpu.pc = 0xffff;
    cpu.stack_pointer = 0x0100;
    cpu.status = 0x30;
    bus.bytes[0xfff4] = 0x34;
    bus.bytes[0xfff5] = 0x12;
    bus.writes.clear();
    bus.reads.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x02, {0x00})); // COP
    assert(result.instruction_cycles == 7 && cpu.pc == 0x1234
        && cpu.stack_pointer == 0x01fd);
    assert(bus.writes.size() == 3 && bus.writes[0].value == 0x00
        && bus.writes[1].value == 0x01 && bus.writes[2].value == 0x30);
    assert(bus.reads[0].address == 0x00fff4);

    cpu.pc = 0x2000;
    result = kss::execute_lifted(cpu, bus, instruction(0x42, {0xaa})); // WDM
    assert(result.instruction_cycles == 2 && cpu.pc == 0x2002);
    result = kss::execute_lifted(cpu, bus, instruction(0xcb)); // WAI
    assert(result.instruction_cycles == 3 && cpu.waiting);
    result = kss::execute_lifted(cpu, bus, instruction(0xdb)); // STP
    assert(result.instruction_cycles == 3 && cpu.stopped);

    cpu.emulation = false;
    cpu.status = 0;
    cpu.x = 0xffff;
    cpu.y = 0;
    result = kss::execute_lifted(cpu, bus, instruction(0xe8)); // INX
    assert(cpu.x == 0 && cpu.flag(kss::StatusFlag::zero));
    result = kss::execute_lifted(cpu, bus, instruction(0x88)); // DEY
    assert(cpu.y == 0xffff && cpu.flag(kss::StatusFlag::negative));

    cpu = {};
    cpu.emulation = false;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::index_width);
    cpu.pc = 0x3000;
    cpu.a = 0;
    cpu.x = 0;
    cpu.y = 0;
    bus.bytes[0x0000] = 0xaa;
    bus.writes.clear();
    result = kss::execute_lifted(cpu, bus, instruction(0x44, {0x34, 0x12})); // MVP
    assert(result.instruction_cycles == 7 && cpu.pc == 0x3003 && cpu.a == 0xffff);
    assert(cpu.x == 0x00ff && cpu.y == 0x00ff && cpu.data_bank == 0x34);
    assert(bus.writes.size() == 1 && bus.writes[0].address == 0x340000
        && bus.writes[0].value == 0xaa);

    const auto before = cpu;
    const auto reads = bus.reads.size();
    const auto writes = bus.writes.size();
    assert(kss::execute_lifted(cpu, bus, instruction(0x00)).status
        == kss::LiftStatus::invalid_encoding);
    assert(kss::execute_lifted(cpu, bus, instruction(0xcb, {0x00})).status
        == kss::LiftStatus::invalid_encoding);
    assert(cpu.pc == before.pc && cpu.cycles == before.cycles
        && cpu.stack_pointer == before.stack_pointer && bus.reads.size() == reads
        && bus.writes.size() == writes);
}

void test_asynchronous_irq_nmi_wake_and_reset_boundaries() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = true;
    cpu.status = 0x34; // I set: IRQ is masked.
    cpu.pc = 0x2001;
    cpu.stack_pointer = 0x01ff;
    cpu.waiting = true;
    auto async = kss::service_lifted_async_signal(
        cpu, bus, kss::CpuAsyncSignal::irq);
    assert(async.status == kss::CpuAsyncStatus::woke_masked && async.entry_cycles == 0);
    assert(!cpu.waiting && cpu.irq_pending && cpu.pc == 0x2001
        && cpu.stack_pointer == 0x01ff && bus.reads.empty() && bus.writes.empty());

    cpu.status = 0x38; // I clear, D set; hardware entry must set I and clear D.
    bus.bytes[0xfffe] = 0x67;
    bus.bytes[0xffff] = 0x45;
    async = kss::service_lifted_async_signal(cpu, bus, kss::CpuAsyncSignal::irq);
    assert(async.status == kss::CpuAsyncStatus::serviced && async.entry_cycles == 7);
    assert(cpu.pc == 0x4567 && cpu.program_bank == 0 && cpu.stack_pointer == 0x01fc);
    assert(bus.writes.size() == 3 && bus.writes[0].address == 0x0001ff
        && bus.writes[0].value == 0x20 && bus.writes[1].value == 0x01
        && bus.writes[2].value == 0x38);
    assert(bus.reads.size() == 2 && bus.reads[0].address == 0x00fffe
        && bus.reads[0].kind == kss::BusAccessKind::vector);
    assert(cpu.flag(kss::StatusFlag::irq_disable)
        && !cpu.flag(kss::StatusFlag::decimal) && !cpu.irq_pending);

    cpu = {};
    cpu.emulation = false;
    cpu.program_bank = 0x80;
    cpu.pc = 0x1234;
    cpu.stack_pointer = 0x1fff;
    cpu.status = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(kss::StatusFlag::decimal)
        | static_cast<std::uint8_t>(kss::StatusFlag::carry));
    cpu.waiting = true;
    bus.bytes[0xffea] = 0x78;
    bus.bytes[0xffeb] = 0x56;
    bus.reads.clear();
    bus.writes.clear();
    async = kss::service_lifted_async_signal(cpu, bus, kss::CpuAsyncSignal::nmi);
    assert(async.status == kss::CpuAsyncStatus::serviced && async.entry_cycles == 8);
    assert(cpu.address() == 0x005678 && cpu.stack_pointer == 0x1ffb && !cpu.waiting);
    assert(bus.writes.size() == 4 && bus.writes[0].value == 0x80
        && bus.writes[1].value == 0x12 && bus.writes[2].value == 0x34
        && bus.writes[3].value == 0x09);
    assert(bus.reads[0].address == 0x00ffea
        && bus.reads[0].kind == kss::BusAccessKind::vector);
    assert(cpu.flag(kss::StatusFlag::irq_disable)
        && !cpu.flag(kss::StatusFlag::decimal) && !cpu.nmi_pending);

    cpu = {};
    cpu.emulation = false;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::decimal);
    cpu.a = 0xbeef;
    cpu.x = 0xabcd;
    cpu.y = 0x9876;
    cpu.direct_page = 0x4321;
    cpu.data_bank = 0x7e;
    cpu.program_bank = 0x80;
    cpu.stack_pointer = 0xbeef;
    cpu.cycles = 10;
    assert(kss::execute_lifted(cpu, bus, instruction(0xdb)).status
        == kss::LiftStatus::executed);
    const auto stopped = cpu;
    bus.reads.clear();
    bus.writes.clear();
    async = kss::service_lifted_async_signal(cpu, bus, kss::CpuAsyncSignal::nmi);
    assert(async.status == kss::CpuAsyncStatus::ignored_stopped
        && cpu.pc == stopped.pc && cpu.stack_pointer == stopped.stack_pointer
        && bus.reads.empty() && bus.writes.empty());

    bus.bytes[0xfffc] = 0x34;
    bus.bytes[0xfffd] = 0x12;
    async = kss::service_lifted_async_signal(cpu, bus, kss::CpuAsyncSignal::reset);
    assert(async.status == kss::CpuAsyncStatus::serviced && async.entry_cycles == 0);
    assert(!cpu.stopped && !cpu.waiting && cpu.emulation && cpu.pc == 0x1234
        && cpu.program_bank == 0 && cpu.data_bank == 0 && cpu.direct_page == 0);
    assert(cpu.a == 0xbeef && cpu.x == 0x00cd && cpu.y == 0x0076
        && cpu.stack_pointer == 0x01ef && cpu.cycles == 13);
    assert(cpu.flag(kss::StatusFlag::irq_disable)
        && cpu.flag(kss::StatusFlag::accumulator_width)
        && cpu.flag(kss::StatusFlag::index_width)
        && !cpu.flag(kss::StatusFlag::decimal));
    assert(bus.reads.size() == 2 && bus.reads[0].address == 0x00fffc
        && bus.reads[1].address == 0x00fffd
        && bus.reads[0].kind == kss::BusAccessKind::vector);
}

void test_increment_accumulator_widths() {
    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.emulation = false;
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::accumulator_width);
    cpu.a = 0xabff;
    auto result = kss::execute_lifted(cpu, bus, instruction(0x1a));
    assert(result.status == kss::LiftStatus::executed);
    assert(result.instruction_bytes == 1 && result.instruction_cycles == 2);
    assert(cpu.a == 0xab00 && cpu.flag(kss::StatusFlag::zero));
    assert(!cpu.flag(kss::StatusFlag::negative));

    cpu.status = 0;
    cpu.a = 0x7fff;
    result = kss::execute_lifted(cpu, bus, instruction(0x1a));
    assert(cpu.a == 0x8000 && !cpu.flag(kss::StatusFlag::zero));
    assert(cpu.flag(kss::StatusFlag::negative));

    const auto before = cpu;
    result = kss::execute_lifted(cpu, bus, instruction(0x1a, {0x00}));
    assert(result.status == kss::LiftStatus::invalid_encoding);
    assert(cpu.a == before.a && cpu.pc == before.pc && cpu.cycles == before.cycles);
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

void test_kss_scpu_first_reset_block_against_mesen_reference() {
    // Observed instruction bytes from the ROM-free lifted-reset artifact.  The
    // reference capture enters at $00:8004 and first reaches a control-flow
    // boundary at BPL $00:8170 -> $00:816D.
    constexpr std::array<std::uint8_t, 366> reset_bytes{
        0x78, 0x18, 0xFB, 0xE2, 0x20, 0xC2, 0x10, 0xA2, 0xFF, 0x1F, 0x9A, 0x4B, 0xAB, 0x9C, 0x00, 0x42,
        0xF4, 0x00, 0x21, 0x2B, 0xA9, 0x8F, 0x85, 0x00, 0xA9, 0x63, 0x85, 0x01, 0x64, 0x02, 0x64, 0x03,
        0xA9, 0x04, 0x85, 0x05, 0x64, 0x06, 0x64, 0x0D, 0x64, 0x0D, 0x64, 0x0E, 0x64, 0x0E, 0x64, 0x0F,
        0x64, 0x0F, 0x64, 0x10, 0x64, 0x10, 0x64, 0x11, 0x64, 0x11, 0x64, 0x12, 0x64, 0x12, 0x64, 0x13,
        0x64, 0x13, 0x64, 0x14, 0x64, 0x14, 0xA9, 0x80, 0x85, 0x15, 0x64, 0x16, 0x64, 0x17, 0x64, 0x1A,
        0x64, 0x1B, 0xA9, 0x01, 0x85, 0x1B, 0x64, 0x1C, 0x64, 0x1C, 0x64, 0x1D, 0x64, 0x1D, 0x64, 0x1E,
        0x85, 0x1E, 0x64, 0x1F, 0x64, 0x1F, 0x64, 0x20, 0x64, 0x20, 0x64, 0x21, 0x64, 0x23, 0x64, 0x24,
        0x64, 0x25, 0x64, 0x26, 0x64, 0x27, 0x64, 0x28, 0x64, 0x29, 0x64, 0x2A, 0x64, 0x2B, 0x64, 0x2E,
        0x64, 0x2F, 0xA9, 0x30, 0x85, 0x30, 0x64, 0x31, 0xA9, 0xE0, 0x85, 0x32, 0x64, 0x33, 0xF4, 0x00,
        0x42, 0x2B, 0xA9, 0xFF, 0x85, 0x01, 0x64, 0x02, 0x64, 0x03, 0x64, 0x04, 0x64, 0x05, 0x64, 0x06,
        0x64, 0x07, 0x64, 0x08, 0x64, 0x09, 0x64, 0x0A, 0x64, 0x0B, 0x64, 0x0C, 0x64, 0x0D, 0xF4, 0x00,
        0x37, 0x2B, 0xA9, 0x20, 0x8D, 0x00, 0x22, 0x9C, 0x01, 0x22, 0xA9, 0xA0, 0x8D, 0x02, 0x22, 0x9C,
        0x20, 0x22, 0xA9, 0x01, 0x8D, 0x21, 0x22, 0xA9, 0x02, 0x8D, 0x22, 0x22, 0xA9, 0x03, 0x8D, 0x23,
        0x22, 0x9C, 0x24, 0x22, 0xA9, 0x05, 0x8D, 0x28, 0x22, 0xA9, 0x80, 0x8D, 0x26, 0x22, 0xA9, 0xFF,
        0x8D, 0x29, 0x22, 0x9C, 0x00, 0x30, 0x9C, 0x01, 0x30, 0xA2, 0xF4, 0x8B, 0x8E, 0x03, 0x22, 0x9C,
        0x00, 0x22, 0xA2, 0x00, 0x00, 0x8E, 0x81, 0x21, 0x9C, 0x83, 0x21, 0xA9, 0x08, 0x8D, 0x10, 0x43,
        0xA2, 0xFE, 0xFF, 0x8E, 0x12, 0x43, 0x9C, 0x14, 0x43, 0xA9, 0x80, 0x8D, 0x11, 0x43, 0xA2, 0x00,
        0x20, 0x8E, 0x15, 0x43, 0xA9, 0x02, 0x8D, 0x0B, 0x42, 0xA2, 0x0E, 0x00, 0x8E, 0x81, 0x21, 0x9C,
        0x83, 0x21, 0x9C, 0x10, 0x43, 0xA2, 0x8C, 0x81, 0x8E, 0x12, 0x43, 0x9C, 0x14, 0x43, 0xA9, 0x80,
        0x8D, 0x11, 0x43, 0xA2, 0x17, 0x00, 0x8E, 0x15, 0x43, 0xA9, 0x02, 0x8D, 0x0B, 0x42, 0xA2, 0x26,
        0x00, 0x8E, 0x81, 0x21, 0xA2, 0xA3, 0x81, 0x8E, 0x12, 0x43, 0xA2, 0x0E, 0x00, 0x8E, 0x15, 0x43,
        0xA9, 0x02, 0x8D, 0x0B, 0x42, 0xA9, 0x7E, 0x8D, 0x14, 0x30, 0xA9, 0x80, 0x8D, 0x5F, 0x30, 0x8D,
        0xA2, 0x30, 0xA9, 0xFF, 0x8D, 0x93, 0x30, 0xC2, 0x20, 0xAD, 0x00, 0x30, 0x10, 0xFB,
    };

    RecordingBus bus;
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::snes_cpu;
    cpu.program_bank = 0;
    cpu.pc = 0x8004;
    cpu.status = 0x34;
    cpu.emulation = true;

    std::size_t offset = 0;
    std::size_t executed = 0;
    while (offset < reset_bytes.size()) {
        assert(cpu.pc == static_cast<std::uint16_t>(0x8004U + offset));
        kss::LiftedInstruction lifted{};
        lifted.opcode = reset_bytes[offset];
        switch (lifted.opcode) {
        case 0xe2: case 0xc2: case 0xa9: case 0x85: case 0x64: case 0x10:
            lifted.operand_count = 1;
            break;
        case 0xa2: case 0xf4: case 0x8d: case 0x8e: case 0x9c: case 0xad:
            lifted.operand_count = 2;
            break;
        default:
            lifted.operand_count = 0;
            break;
        }
        for (std::uint8_t index = 0; index < lifted.operand_count; ++index) {
            lifted.operands[index] = reset_bytes[offset + 1U + index];
        }
        const auto result = kss::execute_lifted(cpu, bus, lifted);
        assert(result.status == kss::LiftStatus::executed);
        ++executed;
        offset += result.instruction_bytes;
        if (lifted.opcode == 0x10) break;
    }

    // State 417 of the private MesenCE differential capture, immediately
    // after the taken BPL. The CPU-cycle counter excludes interleaved SA-1
    // scheduler time and therefore compares directly with this executor.
    assert(executed == 160 && offset == reset_bytes.size());
    assert(cpu.pc == 0x816d && cpu.program_bank == 0 && cpu.data_bank == 0);
    assert(cpu.a == 0 && cpu.x == 14 && cpu.y == 0);
    assert(cpu.direct_page == 0x3700 && cpu.stack_pointer == 0x1fff);
    assert(cpu.status == 0x07 && !cpu.emulation && cpu.cycles == 516);
    assert(bus.reads.size() == 9);
    assert(bus.reads[bus.reads.size() - 2].address == 0x003000
        && bus.reads.back().address == 0x003001);
    assert(bus.writes.size() == 124);
    const auto write_count = bus.writes.size();
    assert(bus.writes[write_count - 4].address == 0x003014
        && bus.writes[write_count - 4].value == 0x7e);
    assert(bus.writes[write_count - 3].address == 0x00305f
        && bus.writes[write_count - 3].value == 0x80);
    assert(bus.writes[write_count - 2].address == 0x0030a2
        && bus.writes[write_count - 2].value == 0x80);
    assert(bus.writes[write_count - 1].address == 0x003093
        && bus.writes[write_count - 1].value == 0xff);
}

} // namespace

int main() {
    test_implied_flag_semantics();
    test_mode_change_semantics();
    test_immediate_loads_both_widths();
    test_absolute_stores_and_bus_writes();
    test_control_flow_and_rejection_are_atomic();
    test_absolute_load_and_bpl_semantics();
    test_call_stack_compare_and_transfer_semantics();
    test_architecture_control_flow_family();
    test_remaining_conditional_branches();
    test_accumulator_alu_families_and_addressing();
    test_load_store_index_families_and_addressing();
    test_rmw_bit_and_index_compare_families();
    test_transfer_and_stack_operation_families();
    test_remaining_indirect_accumulator_addressing();
    test_interrupt_stop_and_final_implied_operations();
    test_asynchronous_irq_nmi_wake_and_reset_boundaries();
    test_increment_accumulator_widths();
    test_stack_and_direct_page_semantics();
    test_kss_scpu_reset_prefix_against_mesen_reference();
    test_kss_scpu_first_reset_block_against_mesen_reference();
    return 0;
}
