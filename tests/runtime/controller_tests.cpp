#include "kss/controller.hpp"
#include "kss/dual_bus.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

constexpr std::uint16_t button(kss::SnesButton value) {
    return static_cast<std::uint16_t>(value);
}

void test_serial_order_padding_and_high_tail() {
    kss::SnesControllerPorts ports;
    const auto pressed = static_cast<std::uint16_t>(button(kss::SnesButton::b)
        | button(kss::SnesButton::start) | button(kss::SnesButton::right)
        | button(kss::SnesButton::l));
    ports.set_buttons(0, pressed);
    ports.write_strobe(true);
    ports.write_strobe(false);
    constexpr std::array<std::uint8_t, 12> expected{1,0,0,1,0,0,0,1,0,0,1,0};
    for (const auto bit_value : expected) assert(ports.read_serial(0) == bit_value);
    for (int index = 0; index < 4; ++index) assert(ports.read_serial(0) == 0);
    assert(ports.read_serial(0) == 1);
    assert(ports.read_serial(0) == 1);
}

void test_latched_snapshot_and_high_strobe_live_b() {
    kss::SnesControllerPorts ports;
    ports.set_buttons(1, button(kss::SnesButton::y));
    ports.write_strobe(true);
    assert(ports.read_serial(1) == 0);
    ports.set_buttons(1, button(kss::SnesButton::b));
    assert(ports.read_serial(1) == 1);
    ports.write_strobe(false);
    ports.set_buttons(1, button(kss::SnesButton::r));
    assert(ports.read_serial(1) == 1);
    assert(ports.read_serial(1) == 0);
}

void test_auto_joypad_and_invalid_ports() {
    kss::SnesControllerPorts ports;
    ports.set_buttons(0, 0xffffU);
    assert(ports.auto_joypad(0) == 0xfff0U);
    assert(ports.buttons(2) == 0 && ports.auto_joypad(2) == 0);
    assert(ports.read_serial(2) == 1);
}

void test_runtime_bus_serial_open_bus_auto_results_and_host_api() {
    std::array<std::uint8_t,1> rom{};
    kss::RomBackedDualBus bus(rom);
    const auto p1 = static_cast<std::uint16_t>(button(kss::SnesButton::b)
        | button(kss::SnesButton::start));
    const auto p2 = static_cast<std::uint16_t>(button(kss::SnesButton::y)
        | button(kss::SnesButton::r));
    bus.set_controller_buttons(0,p1); bus.set_controller_buttons(1,p2);
    bus.set_controller_buttons(2,0xffffU);
    assert(bus.controller_buttons(0)==p1 && bus.controller_buttons(1)==p2);
    assert(bus.controller_buttons(2)==0);

    bus.write8(kss::ProcessorId::snes_cpu,0x004016,1);
    bus.write8(kss::ProcessorId::snes_cpu,0x004016,0);
    bus.write8(kss::ProcessorId::snes_cpu,0x004201,0xa8); // Establish CPU open bus.
    assert(bus.read8(kss::ProcessorId::snes_cpu,0x004016)==0xa9); // P1 B + upper open bus.
    assert(bus.read8(kss::ProcessorId::snes_cpu,0x004017)==0xbc); // P2 B=0, d2-d4 grounded high.
    assert(bus.read8(kss::ProcessorId::snes_cpu,0x004016)==0xbc); // P1 Y=0.
    assert(bus.read8(kss::ProcessorId::snes_cpu,0x004017)==0xbd); // P2 Y=1.

    // Auto-joypad layout is B..R in bits 15..4, not the host serial-bit mask.
    assert(bus.read8(kss::ProcessorId::snes_cpu,0x004218)==0x00);
    assert(bus.read8(kss::ProcessorId::snes_cpu,0x004219)==0x90);
    assert(bus.read8(kss::ProcessorId::snes_cpu,0x00421a)==0x10);
    assert(bus.read8(kss::ProcessorId::snes_cpu,0x00421b)==0x40);
    for(std::uint32_t address=0x00421c;address<=0x00421f;++address)
        assert(bus.read8(kss::ProcessorId::snes_cpu,address)==0);

    // $4017 writes cannot strobe either controller; high $4016 strobe exposes live B.
    bus.write8(kss::ProcessorId::snes_cpu,0x004016,1);
    bus.set_controller_buttons(0,0);
    assert((bus.read8(kss::ProcessorId::snes_cpu,0x004016)&1U)==0);
    bus.write8(kss::ProcessorId::snes_cpu,0x004017,0);
    bus.set_controller_buttons(0,button(kss::SnesButton::b));
    assert((bus.read8(kss::ProcessorId::snes_cpu,0x004016)&1U)==1);
}

} // namespace

int main() {
    test_serial_order_padding_and_high_tail();
    test_latched_snapshot_and_high_strobe_live_b();
    test_auto_joypad_and_invalid_ports();
    test_runtime_bus_serial_open_bus_auto_results_and_host_api();
}
