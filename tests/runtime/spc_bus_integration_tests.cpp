#include "kss/dual_bus.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

using kss::apu::Spc700Core;
using kss::apu::SpcStepStatus;

std::array<std::uint8_t, Spc700Core::kIplSize> program_ipl(
    std::initializer_list<std::uint8_t> program) {
    std::array<std::uint8_t, Spc700Core::kIplSize> ipl{};
    std::size_t index = 0;
    for (const auto value : program) ipl[index++] = value;
    ipl[62] = 0xc0;
    ipl[63] = 0xff;
    return ipl;
}

void cpu_write(kss::RomBackedDualBus& bus, std::uint16_t address, std::uint8_t value) {
    bus.write8(kss::ProcessorId::snes_cpu, address, value, kss::BusAccessKind::data);
}

void test_unprovisioned_diagnostic_state_is_resumable() {
    std::array<std::uint8_t, 1> rom{};
    kss::RomBackedDualBus bus(rom);
    assert(!bus.spc_provisioned() && bus.spc_core() == nullptr);
    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x002140) == 0xaa);
    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x002141) == 0xbb);
    cpu_write(bus, 0x2140, 0x77);
    assert(bus.apu_input_ports()[0] == 0x77);
    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x002140) == 0xaa);
    assert(bus.step_spc().status == SpcStepStatus::missing_ipl);
}

void test_provision_reset_ports_and_explicit_step_only() {
    std::array<std::uint8_t, 1> rom{};
    kss::RomBackedDualBus bus(rom);
    cpu_write(bus, 0x2141, 0x39); // retained if IPL is provisioned later
    const auto ipl = program_ipl({
        0xe8, 0x5a, // MOV A,#$5A
        0xc4, 0xf4, // MOV $F4,A -> SPC-to-SCPU port 0
    });
    assert(bus.provision_spc_ipl(ipl));
    assert(bus.spc_provisioned() && bus.spc_core() != nullptr);
    const auto& reset = bus.spc_core()->registers();
    assert(reset.pc == 0xffc0 && reset.sp == 0xef && reset.ps == 0x02);
    assert(reset.cycles == 0);
    assert(bus.spc_core()->read8(0x00f5) == 0x39);

    // Port access only exchanges latches; it never runs an SPC instruction.
    cpu_write(bus, 0x2142, 0x44);
    assert(bus.spc_core()->registers().pc == 0xffc0);
    assert(bus.spc_core()->registers().cycles == 0);
    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x002140) == 0);
    assert(bus.spc_core()->registers().cycles == 0);

    const auto load = bus.step_spc();
    assert(load.status == SpcStepStatus::executed && load.instruction_cycles == 2);
    assert(bus.spc_core()->registers().pc == 0xffc2);
    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x002140) == 0);
    const auto publish = bus.step_spc();
    assert(publish.status == SpcStepStatus::executed && publish.instruction_cycles == 4);
    assert(bus.read8(kss::ProcessorId::snes_cpu, 0x002140) == 0x5a);
    assert(bus.apu_output_ports()[0] == 0x5a);
}

void test_invalid_provision_and_unsupported_io_fail_closed() {
    std::array<std::uint8_t, 1> rom{};
    kss::RomBackedDualBus bus(rom);
    std::array<std::uint8_t, Spc700Core::kIplSize - 1> short_ipl{};
    assert(!bus.provision_spc_ipl(short_ipl));
    assert(!bus.spc_provisioned());
    assert(bus.step_spc().status == SpcStepStatus::missing_ipl);

    // MOV $00F0,A requests TEST RAM write protection with reset A=0. The
    // ordinary timer/DSP register surface is modeled, so this invalid TEST
    // mode is the explicit fail-closed integration boundary.
    const auto unsupported = program_ipl({0xc5, 0xf0, 0x00});
    assert(bus.provision_spc_ipl(unsupported));
    const auto before = bus.spc_core()->registers();
    const auto result = bus.step_spc();
    assert(result.status == SpcStepStatus::unsupported_io && result.opcode == 0xc5);
    assert(bus.spc_core()->stopped());
    assert(bus.spc_core()->registers().pc == static_cast<std::uint16_t>(before.pc + 3U));
    assert(bus.spc_core()->registers().cycles == before.cycles);
    assert(bus.step_spc().status == SpcStepStatus::stopped);
}

} // namespace

int main() {
    test_unprovisioned_diagnostic_state_is_resumable();
    test_provision_reset_ports_and_explicit_step_only();
    test_invalid_provision_and_unsupported_io_fail_closed();
}
