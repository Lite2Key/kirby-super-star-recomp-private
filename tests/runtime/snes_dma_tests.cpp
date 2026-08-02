#include "kss/dual_bus.hpp"
#include "kss/deterministic_scheduler.hpp"
#include "kss/generated_block_runner.hpp"
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

struct ObservedWrite {
    kss::ProcessorId processor{};
    std::uint32_t address{};
    std::uint8_t value{};

    friend constexpr bool operator==(const ObservedWrite&, const ObservedWrite&) = default;
};

void capture_write(void* context, kss::ProcessorId processor,
    std::uint32_t address, std::uint8_t value) noexcept {
    static_cast<std::vector<ObservedWrite>*>(context)->push_back(
        {processor, address, value});
}

void write_register(kss::RomBackedDualBus& bus, std::uint16_t address, std::uint8_t value) {
    bus.write8(kss::ProcessorId::snes_cpu, address, value, kss::BusAccessKind::data);
}

void configure_channel(kss::RomBackedDualBus& bus, std::uint8_t channel,
    std::uint8_t control, std::uint8_t bbad, std::uint32_t source, std::uint16_t count) {
    const auto base = static_cast<std::uint16_t>(0x4300U + channel * 0x10U);
    write_register(bus, base, control);
    write_register(bus, static_cast<std::uint16_t>(base + 1U), bbad);
    write_register(bus, static_cast<std::uint16_t>(base + 2U), static_cast<std::uint8_t>(source));
    write_register(bus, static_cast<std::uint16_t>(base + 3U), static_cast<std::uint8_t>(source >> 8U));
    write_register(bus, static_cast<std::uint16_t>(base + 4U), static_cast<std::uint8_t>(source >> 16U));
    write_register(bus, static_cast<std::uint16_t>(base + 5U), static_cast<std::uint8_t>(count));
    write_register(bus, static_cast<std::uint16_t>(base + 6U), static_cast<std::uint8_t>(count >> 8U));
}

void set_wram_port(kss::RomBackedDualBus& bus, std::uint32_t address) {
    write_register(bus, 0x2181, static_cast<std::uint8_t>(address));
    write_register(bus, 0x2182, static_cast<std::uint8_t>(address >> 8U));
    write_register(bus, 0x2183, static_cast<std::uint8_t>(address >> 16U));
}

void test_wram_port_registers_and_wrap() {
    std::array<std::uint8_t, 0x10000> rom{};
    kss::RomBackedDualBus bus(rom);
    set_wram_port(bus, 0x1ffff);
    write_register(bus, 0x2180, 0xa5);
    write_register(bus, 0x2180, 0x5a);
    assert(bus.wram()[0x1ffff] == 0xa5 && bus.wram()[0] == 0x5a);
    assert(bus.wram_port_address() == 1);
}

void test_wmdata_observer_reports_port_then_physical_wram() {
    std::array<std::uint8_t, 0x10000> rom{};
    kss::RomBackedDualBus bus(rom);
    set_wram_port(bus, 0x1ffff);
    std::vector<ObservedWrite> writes;
    bus.set_cpu_write_sink(&writes, &capture_write);

    write_register(bus, 0x2180, 0xa5);
    write_register(bus, 0x2180, 0x5a);

    const std::vector<ObservedWrite> expected{
        {kss::ProcessorId::snes_cpu, 0x002180, 0xa5},
        {kss::ProcessorId::snes_cpu, 0x7fffff, 0xa5},
        {kss::ProcessorId::snes_cpu, 0x002180, 0x5a},
        {kss::ProcessorId::snes_cpu, 0x7e0000, 0x5a},
    };
    assert(writes == expected);
    assert(bus.wram()[0x1ffff] == 0xa5 && bus.wram()[0] == 0x5a);
    assert(bus.wram_port_address() == 1);
}

void test_mode_order_and_channel_priority() {
    std::array<std::uint8_t, 0x10000> rom{};
    rom[0] = 0x10;
    rom[1] = 0x11;
    rom[2] = 0x12;
    rom[3] = 0x13;
    rom[4] = 0x20;
    rom[5] = 0x21;
    kss::RomBackedDualBus bus(rom);

    // Enable order is deliberately reversed. Hardware must finish channel 0
    // before channel 1, and mode 1 alternates the two B-bus ports.
    configure_channel(bus, 1, 0x00, 0x20, 0x008004, 2);
    configure_channel(bus, 0, 0x01, 0x18, 0x008000, 4);
    write_register(bus, 0x420b, 0x03);

    const auto transfers = bus.dma_transfers();
    assert(transfers.size() == 2);
    assert(transfers[0].channel == 0 && transfers[0].control == 0x01);
    assert(transfers[0].source_start == 0x008000 && transfers[0].byte_count == 4);
    assert(transfers[1].channel == 1 && transfers[1].source_start == 0x008004);
    const auto writes = bus.dma_port_writes();
    assert(writes.size() == 6);
    constexpr std::array<std::uint16_t, 6> addresses{
        0x2118, 0x2119, 0x2118, 0x2119, 0x2120, 0x2120};
    constexpr std::array<std::uint8_t, 6> values{0x10, 0x11, 0x12, 0x13, 0x20, 0x21};
    for (std::size_t index = 0; index < writes.size(); ++index) {
        assert(writes[index].address == addresses[index]);
        assert(writes[index].value == values[index]);
    }
}

void test_reset_style_fixed_source_to_wram_port() {
    std::array<std::uint8_t, 0x10000> rom{};
    rom[0x7ffe] = 0x6b;
    kss::RomBackedDualBus bus(rom);
    set_wram_port(bus, 0);
    configure_channel(bus, 1, 0x08, 0x80, 0x00fffe, 8);
    std::vector<ObservedWrite> writes;
    bus.set_cpu_write_sink(&writes, &capture_write);
    write_register(bus, 0x420b, 0x02);

    assert(bus.dma_transfers().size() == 1);
    const auto transfer = bus.dma_transfers()[0];
    assert(transfer.channel == 1 && transfer.control == 0x08);
    assert(transfer.source_start == 0x00fffe && transfer.byte_count == 8);
    assert(transfer.b_bus_address == 0x2180);
    assert(bus.dma_port_writes().size() == 8 && bus.wram_port_address() == 8);
    for (std::size_t index = 0; index < 8; ++index) {
        assert(bus.dma_port_writes()[index].ordinal == index);
        assert(bus.dma_port_writes()[index].address == 0x2180);
        assert(bus.wram()[index] == 0x6b);
        const auto observed = 1U + index * 2U;
        assert((writes[observed] == ObservedWrite{
            kss::ProcessorId::snes_cpu, 0x002180, 0x6b}));
        assert((writes[observed + 1U] == ObservedWrite{
            kss::ProcessorId::snes_cpu,
            static_cast<std::uint32_t>(0x7e0000U + index), 0x6b}));
    }
    assert(writes.size() == 1U + 8U * 2U);
    assert((writes.front() == ObservedWrite{
        kss::ProcessorId::snes_cpu, 0x00420b, 0x02}));
}

void test_generated_scpu_reset_block_dma_summary() {
    std::array<std::uint8_t, 0x10000> rom{};
    kss::RomBackedDualBus bus(rom);
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
    // Stop on the first arrival at $816D. Reading $3000 and the subsequent
    // wait-loop branch belong to the SA-1 register model, not DMA behavior.
    const auto result = kss::run_generated_until(
        cpu, bus, scheduler, dispatcher, checkpoint, 158, 158);
    if (result.status != kss::GeneratedRunStatus::checkpoint_reached) {
        std::fprintf(stderr, "DMA reset runner status=%u blocks=%zu pc=%06X stopped=%u\n",
            static_cast<unsigned>(result.status), result.completed_blocks,
            static_cast<unsigned>(cpu.address()), static_cast<unsigned>(cpu.stopped));
        std::exit(1);
    }

    // These value-free transfer descriptors correspond to the three private
    // capture callback groups attributed to STA $420B at $811A/$813F/$8156.
    const auto transfers = bus.dma_transfers();
    if (transfers.size() != 3) {
        std::fprintf(stderr, "DMA reset transfer count=%zu port writes=%zu WMADD=%05X\n",
            transfers.size(), bus.dma_port_writes().size(),
            static_cast<unsigned>(bus.wram_port_address()));
        std::exit(1);
    }
    assert(transfers[0].channel == 1 && transfers[0].control == 0x08
        && transfers[0].source_start == 0x00fffe && transfers[0].byte_count == 8192);
    assert(transfers[1].channel == 1 && transfers[1].control == 0x00
        && transfers[1].source_start == 0x00818c && transfers[1].byte_count == 23);
    assert(transfers[2].channel == 1 && transfers[2].control == 0x00
        && transfers[2].source_start == 0x0081a3 && transfers[2].byte_count == 14);
    for (const auto& transfer : transfers) assert(transfer.b_bus_address == 0x2180);
    assert(bus.dma_port_writes().size() == 8192U + 23U + 14U);
    assert(bus.wram_port_address() == 0x34);
}

} // namespace

int main() {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    test_wram_port_registers_and_wrap();
    test_wmdata_observer_reports_port_then_physical_wram();
    test_mode_order_and_channel_priority();
    test_reset_style_fixed_source_to_wram_port();
    test_generated_scpu_reset_block_dma_summary();
    return 0;
}
