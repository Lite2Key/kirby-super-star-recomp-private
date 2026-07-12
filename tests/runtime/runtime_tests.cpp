#include "kss/alu.hpp"
#include "kss/bootstrap.hpp"
#include "kss/bus.hpp"
#include "kss/deterministic_scheduler.hpp"
#include "kss/dispatcher.hpp"
#include "kss/dual_bus.hpp"
#include "kss/rom_validation.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>

namespace {

class TestBus final : public kss::Bus {
public:
    std::array<std::uint8_t, 0x10000> bytes{};

    std::uint8_t read8(kss::ProcessorId, std::uint32_t address, kss::BusAccessKind) override {
        return bytes[address & 0xffffU];
    }

    void write8(kss::ProcessorId, std::uint32_t address, std::uint8_t value, kss::BusAccessKind) override {
        bytes[address & 0xffffU] = value;
    }
};

void increment_accumulator(
    kss::CpuContext& cpu,
    kss::Bus&,
    kss::Scheduler&) {
    ++cpu.a;
    ++cpu.pc;
}

void test_cpu_mode_and_key() {
    kss::CpuContext cpu;
    cpu.processor = kss::ProcessorId::sa1;
    cpu.program_bank = 0xc0;
    cpu.pc = 0x8123;
    cpu.status = 0;
    cpu.emulation = true;
    assert(cpu.mode() == kss::CpuMode::normalized(true, false, false));
    assert(cpu.block_key() == kss::BlockKey::make(kss::ProcessorId::sa1, 0xc08123, true, false, false));

    cpu.x = 0xabcd;
    cpu.y = 0x9876;
    cpu.stack_pointer = 0xbeef;
    cpu.normalize_after_mode_change();
    assert(cpu.x == 0x00cd);
    assert(cpu.y == 0x0076);
    assert(cpu.stack_pointer == 0x01ef);
}

void test_bus_bank_wrap() {
    TestBus bus;
    kss::write16_bank_wrapped(bus, kss::ProcessorId::snes_cpu, 0x12ffff, 0xbeef);
    assert(bus.bytes[0xffff] == 0xef);
    assert(bus.bytes[0] == 0xbe);
    assert(kss::read16_bank_wrapped(bus, kss::ProcessorId::snes_cpu, 0x12ffff) == 0xbeef);
}

void test_address_space_contracts() {
    const auto low_wram = kss::map_address(kss::ProcessorId::snes_cpu, 0x801234);
    assert(low_wram.region == kss::MemoryRegion::wram);
    assert(low_wram.canonical_offset == 0x1234);
    assert(low_wram.mirrored);
    assert(kss::has_permission(low_wram.permissions, kss::MemoryPermission::execute));

    const auto full_wram = kss::map_address(kss::ProcessorId::snes_cpu, 0x7f1234);
    assert(full_wram.region == kss::MemoryRegion::wram);
    assert(full_wram.canonical_offset == 0x11234);

    const auto sa1_private = kss::map_address(kss::ProcessorId::sa1, 0x800123);
    assert(sa1_private.region == kss::MemoryRegion::sa1_iram);
    assert(sa1_private.canonical_offset == 0x123);
    assert(kss::map_address(kss::ProcessorId::sa1, 0x7e1234).region == kss::MemoryRegion::unmapped);

    const auto bwram_window = kss::map_address(kss::ProcessorId::sa1, 0x006123);
    assert(bwram_window.region == kss::MemoryRegion::bwram);
    assert(bwram_window.canonical_offset == 0x123);
    assert(bwram_window.placeholder);

    const auto rom = kss::map_address(kss::ProcessorId::snes_cpu, 0x808010);
    assert(rom.region == kss::MemoryRegion::rom);
    assert(rom.canonical_offset == 0x10);
    assert(rom.placeholder);
    assert(kss::has_permission(rom.permissions, kss::MemoryPermission::execute));
    assert(!kss::has_permission(rom.permissions, kss::MemoryPermission::write));

    const auto ppu_register = kss::map_address(kss::ProcessorId::snes_cpu, 0x002100);
    assert(ppu_register.region == kss::MemoryRegion::hardware_register);
    assert(ppu_register.placeholder);
    assert(!kss::has_permission(ppu_register.permissions, kss::MemoryPermission::execute));
}

void test_rom_backed_dual_bus() {
    std::array<std::uint8_t, 0x10000> synthetic_rom{};
    synthetic_rom[0] = 0x42;
    synthetic_rom[0x10] = 0x99;
    kss::RomBackedDualBus bus(synthetic_rom);

    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x008000, kss::BusAccessKind::opcode) == 0x42);
    assert(bus.read8(kss::ProcessorId::sa1, 0x808010, kss::BusAccessKind::opcode) == 0x99);
    bus.write8(kss::ProcessorId::snes_cpu, 0x008000, 0xff);
    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x008000) == 0x42);

    bus.write8(kss::ProcessorId::snes_cpu, 0x7e0123, 0xa5);
    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x000123) == 0xa5);
    assert(bus.read8(kss::ProcessorId::sa1, 0x000123) == 0x00);

    bus.write8(kss::ProcessorId::snes_cpu, 0x006123, 0x5a);
    assert(bus.read8(kss::ProcessorId::sa1, 0x006123) == 0x5a);

    bus.write8(kss::ProcessorId::snes_cpu, 0x002000, 0x11);
    bus.write8(kss::ProcessorId::sa1, 0x002000, 0x22);
    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x002001) == 0x11);
    assert(bus.read8(kss::ProcessorId::sa1, 0x002001) == 0x22);
    assert(bus.open_bus(kss::ProcessorId::snes_cpu) == 0x11);
    assert(bus.open_bus(kss::ProcessorId::sa1) == 0x22);
}

void test_scheduler_order() {
    kss::DeterministicScheduler scheduler;
    scheduler.schedule({5, 999, kss::EventKind::frame, kss::ProcessorId::snes_cpu, 1});
    scheduler.schedule({5, 999, kss::EventKind::interrupt, kss::ProcessorId::sa1, 2});
    assert(!scheduler.pop_next_due().has_value());
    scheduler.advance_to(5);
    const auto first = scheduler.pop_next_due();
    const auto second = scheduler.pop_next_due();
    assert(first && second);
    assert(first->payload == 1);
    assert(second->payload == 2);
    assert(first->sequence < second->sequence);
}

void test_checked_dispatch() {
    TestBus bus;
    kss::DeterministicScheduler scheduler;
    kss::CheckedDispatcher dispatcher;
    kss::CpuContext cpu;
    const auto key = cpu.block_key();
    assert(dispatcher.dispatch(cpu, bus, scheduler) == kss::DispatchStatus::unknown_block);
    assert(dispatcher.register_block(key, increment_accumulator) == kss::RegistrationStatus::registered);
    assert(dispatcher.register_block(key, increment_accumulator) == kss::RegistrationStatus::duplicate_key);
    assert(dispatcher.register_block(key, nullptr) == kss::RegistrationStatus::null_function);
    assert(dispatcher.dispatch(cpu, bus, scheduler) == kss::DispatchStatus::executed);
    assert(cpu.a == 1);
    assert(cpu.pc == 1);
}

std::uint8_t pack_bcd(unsigned value) {
    return static_cast<std::uint8_t>(((value / 10U) << 4U) | (value % 10U));
}

void test_alu_binary_arithmetic_exhaustive() {
    kss::CpuContext cpu;
    cpu.set_flag(kss::StatusFlag::decimal, false);
    for (unsigned left = 0; left <= 0xffU; ++left) {
        for (unsigned right = 0; right <= 0xffU; ++right) {
            for (unsigned carry = 0; carry <= 1U; ++carry) {
                cpu.status = static_cast<std::uint8_t>(carry);
                const auto adc_result = kss::alu::adc8(
                    cpu, static_cast<std::uint8_t>(left), static_cast<std::uint8_t>(right));
                const auto sum = left + right + carry;
                const auto expected_adc = static_cast<std::uint8_t>(sum);
                assert(adc_result == expected_adc);
                assert(cpu.flag(kss::StatusFlag::carry) == (sum > 0xffU));
                assert(cpu.flag(kss::StatusFlag::zero) == (expected_adc == 0));
                assert(cpu.flag(kss::StatusFlag::negative) == ((expected_adc & 0x80U) != 0));
                const auto adc_overflow = ((~(left ^ right) & (left ^ expected_adc) & 0x80U) != 0);
                assert(cpu.flag(kss::StatusFlag::overflow) == adc_overflow);

                cpu.status = static_cast<std::uint8_t>(carry);
                const auto sbc_result = kss::alu::sbc8(
                    cpu, static_cast<std::uint8_t>(left), static_cast<std::uint8_t>(right));
                const auto borrow = 1U - carry;
                const auto expected_sbc = static_cast<std::uint8_t>(left - right - borrow);
                assert(sbc_result == expected_sbc);
                assert(cpu.flag(kss::StatusFlag::carry) == (left >= right + borrow));
                assert(cpu.flag(kss::StatusFlag::zero) == (expected_sbc == 0));
                assert(cpu.flag(kss::StatusFlag::negative) == ((expected_sbc & 0x80U) != 0));
                const auto sbc_overflow = (((left ^ right) & (left ^ expected_sbc) & 0x80U) != 0);
                assert(cpu.flag(kss::StatusFlag::overflow) == sbc_overflow);
            }
        }
    }
}

void test_alu_decimal_valid_values_exhaustive() {
    kss::CpuContext cpu;
    for (unsigned left = 0; left < 100U; ++left) {
        for (unsigned right = 0; right < 100U; ++right) {
            for (unsigned carry = 0; carry <= 1U; ++carry) {
                const auto packed_left = pack_bcd(left);
                const auto packed_right = pack_bcd(right);
                cpu.status = static_cast<std::uint8_t>(
                    static_cast<std::uint8_t>(kss::StatusFlag::decimal) | carry);
                const auto adc_result = kss::alu::adc8(cpu, packed_left, packed_right);
                const auto sum = left + right + carry;
                assert(adc_result == pack_bcd(sum % 100U));
                assert(cpu.flag(kss::StatusFlag::carry) == (sum >= 100U));
                assert(cpu.flag(kss::StatusFlag::zero) == (adc_result == 0));
                assert(cpu.flag(kss::StatusFlag::negative) == ((adc_result & 0x80U) != 0));
                const auto adc_overflow = ((~(packed_left ^ packed_right)
                    & (packed_left ^ static_cast<std::uint8_t>(packed_left + packed_right + carry))
                    & 0x80U) != 0);
                assert(cpu.flag(kss::StatusFlag::overflow) == adc_overflow);

                cpu.status = static_cast<std::uint8_t>(
                    static_cast<std::uint8_t>(kss::StatusFlag::decimal) | carry);
                const auto sbc_result = kss::alu::sbc8(cpu, packed_left, packed_right);
                const auto difference = static_cast<int>(left) - static_cast<int>(right)
                    - static_cast<int>(1U - carry);
                const auto wrapped = difference < 0 ? difference + 100 : difference;
                assert(sbc_result == pack_bcd(static_cast<unsigned>(wrapped)));
                assert(cpu.flag(kss::StatusFlag::carry) == (difference >= 0));
                assert(cpu.flag(kss::StatusFlag::zero) == (sbc_result == 0));
                assert(cpu.flag(kss::StatusFlag::negative) == ((sbc_result & 0x80U) != 0));
                const auto binary_result = static_cast<std::uint8_t>(
                    packed_left - packed_right - (1U - carry));
                const auto sbc_overflow = (((packed_left ^ packed_right)
                    & (packed_left ^ binary_result) & 0x80U) != 0);
                assert(cpu.flag(kss::StatusFlag::overflow) == sbc_overflow);
            }
        }
    }
}

void test_alu_16bit_boundaries() {
    kss::CpuContext cpu;
    cpu.status = 0;
    assert(kss::alu::adc16(cpu, 0xffff, 0x0000) == 0xffff);
    assert(!cpu.flag(kss::StatusFlag::carry));
    cpu.set_flag(kss::StatusFlag::carry, true);
    assert(kss::alu::adc16(cpu, 0xffff, 0x0000) == 0x0000);
    assert(cpu.flag(kss::StatusFlag::carry));
    assert(cpu.flag(kss::StatusFlag::zero));

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::carry);
    assert(kss::alu::sbc16(cpu, 0x8000, 0x0001) == 0x7fff);
    assert(cpu.flag(kss::StatusFlag::overflow));

    cpu.status = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(kss::StatusFlag::decimal)
        | static_cast<std::uint8_t>(kss::StatusFlag::carry));
    assert(kss::alu::adc16(cpu, 0x9999, 0x0000) == 0x0000);
    assert(cpu.flag(kss::StatusFlag::carry));
    cpu.status = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(kss::StatusFlag::decimal)
        | static_cast<std::uint8_t>(kss::StatusFlag::carry));
    assert(kss::alu::sbc16(cpu, 0x1000, 0x0001) == 0x0999);
    assert(cpu.flag(kss::StatusFlag::carry));
}

void test_alu_logical_shift_and_memory_ops() {
    kss::CpuContext cpu;
    cpu.status = 0;
    assert(kss::alu::bit_and8(cpu, 0xf0, 0x0f) == 0x00);
    assert(cpu.flag(kss::StatusFlag::zero));
    assert(kss::alu::bit_or16(cpu, 0x8000, 0x0001) == 0x8001);
    assert(cpu.flag(kss::StatusFlag::negative));
    assert(kss::alu::bit_xor8(cpu, 0xff, 0x7f) == 0x80);

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::overflow);
    kss::alu::compare8(cpu, 0x10, 0x20);
    assert(!cpu.flag(kss::StatusFlag::carry));
    assert(cpu.flag(kss::StatusFlag::negative));
    assert(cpu.flag(kss::StatusFlag::overflow));
    kss::alu::compare16(cpu, 0xabcd, 0xabcd);
    assert(cpu.flag(kss::StatusFlag::carry));
    assert(cpu.flag(kss::StatusFlag::zero));

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::overflow);
    assert(kss::alu::asl8(cpu, 0x80) == 0x00);
    assert(cpu.flag(kss::StatusFlag::carry));
    assert(cpu.flag(kss::StatusFlag::zero));
    assert(cpu.flag(kss::StatusFlag::overflow));
    assert(kss::alu::lsr8(cpu, 0x01) == 0x00);
    assert(cpu.flag(kss::StatusFlag::carry));
    assert(!cpu.flag(kss::StatusFlag::negative));
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::carry);
    assert(kss::alu::rol16(cpu, 0x8000) == 0x0001);
    assert(cpu.flag(kss::StatusFlag::carry));
    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::carry);
    assert(kss::alu::ror16(cpu, 0x0001) == 0x8000);
    assert(cpu.flag(kss::StatusFlag::carry));
    assert(cpu.flag(kss::StatusFlag::negative));

    for (unsigned value = 0; value <= 0xffU; ++value) {
        for (unsigned carry = 0; carry <= 1U; ++carry) {
            cpu.status = static_cast<std::uint8_t>(carry);
            const auto rolled = kss::alu::rol8(cpu, static_cast<std::uint8_t>(value));
            assert(rolled == static_cast<std::uint8_t>((value << 1U) | carry));
            assert(cpu.flag(kss::StatusFlag::carry) == ((value & 0x80U) != 0));
            cpu.status = static_cast<std::uint8_t>(carry);
            const auto rotated = kss::alu::ror8(cpu, static_cast<std::uint8_t>(value));
            assert(rotated == static_cast<std::uint8_t>((value >> 1U) | (carry << 7U)));
            assert(cpu.flag(kss::StatusFlag::carry) == ((value & 1U) != 0));
        }
    }

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::carry);
    assert(kss::alu::increment8(cpu, 0xff) == 0x00);
    assert(cpu.flag(kss::StatusFlag::carry));
    assert(kss::alu::decrement16(cpu, 0x0000) == 0xffff);
    assert(kss::alu::increment16(cpu, 0x7fff) == 0x8000);
    assert(kss::alu::decrement8(cpu, 0x01) == 0x00);

    cpu.status = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(kss::StatusFlag::negative)
        | static_cast<std::uint8_t>(kss::StatusFlag::overflow));
    kss::alu::bit_test8(cpu, 0x0f, 0xf0, false);
    assert(cpu.flag(kss::StatusFlag::zero));
    assert(cpu.flag(kss::StatusFlag::negative));
    assert(cpu.flag(kss::StatusFlag::overflow));
    kss::alu::bit_test16(cpu, 0xffff, 0x8000, true);
    assert(cpu.flag(kss::StatusFlag::negative));
    assert(!cpu.flag(kss::StatusFlag::overflow));
    kss::alu::bit_test8(cpu, 0xff, 0x40, true);
    assert(!cpu.flag(kss::StatusFlag::negative));
    assert(cpu.flag(kss::StatusFlag::overflow));

    cpu.status = static_cast<std::uint8_t>(kss::StatusFlag::carry);
    assert(kss::alu::trb8(cpu, 0x0f, 0x55) == 0x50);
    assert(!cpu.flag(kss::StatusFlag::zero));
    assert(cpu.flag(kss::StatusFlag::carry));
    assert(kss::alu::tsb16(cpu, 0x0f00, 0x00f0) == 0x0ff0);
    assert(cpu.flag(kss::StatusFlag::zero));
}

void test_sha256_vectors() {
    const std::array<std::uint8_t, 0> empty{};
    assert(kss::sha256_hex(empty) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string abc = "abc";
    const auto bytes = std::span{
        reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()};
    assert(kss::sha256_hex(bytes) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void test_cli_usage() {
    const char* arguments[] = {"kss-runtime", "--bogus"};
    std::ostringstream output;
    std::ostringstream errors;
    const auto result = kss::runtime_cli(2, arguments, output, errors);
    assert(result == kss::BootstrapExitCode::usage_error);
    assert(errors.str().find("unknown argument") != std::string::npos);
}

} // namespace

int main() {
    test_cpu_mode_and_key();
    test_bus_bank_wrap();
    test_address_space_contracts();
    test_rom_backed_dual_bus();
    test_scheduler_order();
    test_checked_dispatch();
    test_alu_binary_arithmetic_exhaustive();
    test_alu_decimal_valid_values_exhaustive();
    test_alu_16bit_boundaries();
    test_alu_logical_shift_and_memory_ops();
    test_sha256_vectors();
    test_cli_usage();
    return 0;
}
