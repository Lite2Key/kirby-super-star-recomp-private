#include "kss/dual_bus.hpp"

#include <algorithm>

namespace kss {
namespace {

constexpr std::size_t snes_register_index(std::uint16_t offset) noexcept {
    return static_cast<std::size_t>(offset - 0x2100U);
}

constexpr std::array<std::array<std::uint8_t, 4>, 8> kDmaBbusOffsets{{
    {{0, 0, 0, 0}}, // mode 0: p
    {{0, 1, 0, 1}}, // mode 1: p, p+1
    {{0, 0, 0, 0}}, // mode 2: p, p
    {{0, 0, 1, 1}}, // mode 3: p, p, p+1, p+1
    {{0, 1, 2, 3}}, // mode 4: p, p+1, p+2, p+3
    {{0, 1, 0, 1}}, // mode 5: p, p+1, p, p+1
    {{0, 0, 0, 0}}, // mode 6: p, p
    {{0, 0, 1, 1}}, // mode 7: p, p, p+1, p+1
}};

constexpr std::array<std::uint8_t, 8> kDmaModePeriods{{1, 2, 2, 4, 4, 4, 2, 4}};

} // namespace

RomBackedDualBus::RomBackedDualBus(std::span<const std::uint8_t> rom)
    : rom_(rom.begin(), rom.end()) {}

std::uint8_t RomBackedDualBus::read8(
    ProcessorId processor,
    std::uint32_t address,
    BusAccessKind kind) {
    const auto mapping = map_address(processor, address);
    if (kind == BusAccessKind::opcode
        && !has_permission(mapping.permissions, MemoryPermission::execute)) {
        return open_bus(processor);
    }
    if (!has_permission(mapping.permissions, MemoryPermission::read)) {
        return open_bus(processor);
    }

    auto value = open_bus(processor);
    switch (mapping.region) {
    case MemoryRegion::rom:
        if (!rom_.empty()) {
            value = rom_[mapping.canonical_offset % rom_.size()];
        }
        break;
    case MemoryRegion::wram:
        value = wram_[mapping.canonical_offset];
        break;
    case MemoryRegion::bwram:
        value = bwram_[mapping.canonical_offset];
        break;
    case MemoryRegion::sa1_iram:
        value = mapping.canonical_offset < sa1_iram_.size()
            ? sa1_iram_[mapping.canonical_offset] : 0U;
        break;
    case MemoryRegion::hardware_register:
        if (mapping.canonical_offset >= 0x2200U && mapping.canonical_offset <= 0x23ffU) {
            value = sa1_io_.read(processor,
                static_cast<std::uint16_t>(mapping.canonical_offset), open_bus(processor));
        } else if (processor == ProcessorId::snes_cpu) {
            value = read_snes_register(
                static_cast<std::uint16_t>(mapping.canonical_offset), kind);
        }
        break;
    case MemoryRegion::unmapped:
        break;
    }
    open_bus_[processor_index(processor)] = value;
    return value;
}

void RomBackedDualBus::write8(
    ProcessorId processor,
    std::uint32_t address,
    std::uint8_t value,
    BusAccessKind kind) {
    open_bus_[processor_index(processor)] = value;
    const auto mapping = map_address(processor, address);
    if (!has_permission(mapping.permissions, MemoryPermission::write)) {
        return;
    }

    switch (mapping.region) {
    case MemoryRegion::wram:
        wram_[mapping.canonical_offset] = value;
        break;
    case MemoryRegion::bwram:
        bwram_[mapping.canonical_offset] = value;
        break;
    case MemoryRegion::sa1_iram:
        if (mapping.canonical_offset < sa1_iram_.size()
            && sa1_io_.iram_write_enabled(
                processor, static_cast<std::uint16_t>(mapping.canonical_offset))) {
            sa1_iram_[mapping.canonical_offset] = value;
        }
        break;
    case MemoryRegion::hardware_register:
        if (mapping.canonical_offset >= 0x2200U && mapping.canonical_offset <= 0x23ffU) {
            sa1_io_.write(processor,
                static_cast<std::uint16_t>(mapping.canonical_offset), value);
        } else if (processor == ProcessorId::snes_cpu) {
            write_snes_register(
                static_cast<std::uint16_t>(mapping.canonical_offset), value, kind);
        }
        break;
    case MemoryRegion::rom:
    case MemoryRegion::unmapped:
        break;
    }
}

std::uint8_t RomBackedDualBus::read_snes_register(
    std::uint16_t offset,
    BusAccessKind) {
    if (offset == 0x2180U) {
        const auto value = wram_[wram_port_address_ & 0x1ffffU];
        wram_port_address_ = (wram_port_address_ + 1U) & 0x1ffffU;
        return value;
    }
    return snes_io_.read(offset, open_bus(ProcessorId::snes_cpu));
}

void RomBackedDualBus::write_snes_register(
    std::uint16_t offset,
    std::uint8_t value,
    BusAccessKind kind) {
    snes_registers_[snes_register_index(offset)] = value;
    snes_io_.write(offset, value);
    switch (offset) {
    case 0x2180: // WMDATA
        wram_[wram_port_address_ & 0x1ffffU] = value;
        wram_port_address_ = (wram_port_address_ + 1U) & 0x1ffffU;
        break;
    case 0x2181: // WMADDL
        wram_port_address_ = (wram_port_address_ & 0x1ff00U) | value;
        break;
    case 0x2182: // WMADDM
        wram_port_address_ = (wram_port_address_ & 0x100ffU)
            | (static_cast<std::uint32_t>(value) << 8U);
        break;
    case 0x2183: // WMADDH (only bit 0 is connected)
        wram_port_address_ = (wram_port_address_ & 0x0ffffU)
            | (static_cast<std::uint32_t>(value & 1U) << 16U);
        break;
    case 0x420b: // MDMAEN
        // A CPU write starts general DMA immediately and stalls instruction
        // fetch until all selected channels finish. DMA-origin writes retain
        // normal register semantics but cannot recursively start a transfer.
        if (kind != BusAccessKind::dma) {
            run_dma(value);
            snes_registers_[snes_register_index(0x420bU)] = 0;
        }
        break;
    default:
        break;
    }
}

void RomBackedDualBus::run_dma(std::uint8_t enabled_channels) {
    // SNES general-DMA priority is fixed: lower numbered channels complete
    // before higher numbered channels, independent of enable-bit write order.
    for (std::uint8_t channel = 0; channel < 8U; ++channel) {
        if ((enabled_channels & static_cast<std::uint8_t>(1U << channel)) != 0U) {
            run_dma_channel(channel);
        }
    }
}

void RomBackedDualBus::run_dma_channel(std::uint8_t channel) {
    const auto base = static_cast<std::uint16_t>(0x4300U + channel * 0x10U);
    const auto control = snes_registers_[snes_register_index(base)];
    const auto bbad = snes_registers_[snes_register_index(static_cast<std::uint16_t>(base + 1U))];
    auto source_offset = static_cast<std::uint16_t>(
        snes_registers_[snes_register_index(static_cast<std::uint16_t>(base + 2U))]
        | (static_cast<std::uint16_t>(
            snes_registers_[snes_register_index(static_cast<std::uint16_t>(base + 3U))]) << 8U));
    const auto source_bank =
        snes_registers_[snes_register_index(static_cast<std::uint16_t>(base + 4U))];
    const auto encoded_count = static_cast<std::uint16_t>(
        snes_registers_[snes_register_index(static_cast<std::uint16_t>(base + 5U))]
        | (static_cast<std::uint16_t>(
            snes_registers_[snes_register_index(static_cast<std::uint16_t>(base + 6U))]) << 8U));
    const auto count = encoded_count == 0U ? 0x10000U
                                           : static_cast<std::uint32_t>(encoded_count);
    const auto source_start = (static_cast<std::uint32_t>(source_bank) << 16U) | source_offset;
    dma_transfers_.push_back({channel, control, source_start, count,
        static_cast<std::uint16_t>(0x2100U | bbad)});

    const auto mode = static_cast<std::uint8_t>(control & 7U);
    const auto fixed_source = (control & 0x08U) != 0U;
    const auto decrement_source = !fixed_source && (control & 0x10U) != 0U;
    const auto b_to_a = (control & 0x80U) != 0U;
    for (std::uint32_t ordinal = 0; ordinal < count; ++ordinal) {
        const auto b_offset = kDmaBbusOffsets[mode][ordinal % kDmaModePeriods[mode]];
        const auto b_address = static_cast<std::uint16_t>(
            0x2100U | static_cast<std::uint8_t>(bbad + b_offset));
        const auto a_address = (static_cast<std::uint32_t>(source_bank) << 16U) | source_offset;
        if (b_to_a) {
            const auto value = read8(ProcessorId::snes_cpu, b_address, BusAccessKind::dma);
            write8(ProcessorId::snes_cpu, a_address, value, BusAccessKind::dma);
        } else {
            const auto value = read8(ProcessorId::snes_cpu, a_address, BusAccessKind::dma);
            dma_port_writes_.push_back({channel, ordinal, b_address, value});
            write8(ProcessorId::snes_cpu, b_address, value, BusAccessKind::dma);
        }
        if (!fixed_source) {
            source_offset = static_cast<std::uint16_t>(
                decrement_source ? source_offset - 1U : source_offset + 1U);
        }
    }

    snes_registers_[snes_register_index(static_cast<std::uint16_t>(base + 2U))]
        = static_cast<std::uint8_t>(source_offset);
    snes_registers_[snes_register_index(static_cast<std::uint16_t>(base + 3U))]
        = static_cast<std::uint8_t>(source_offset >> 8U);
    snes_registers_[snes_register_index(static_cast<std::uint16_t>(base + 5U))] = 0;
    snes_registers_[snes_register_index(static_cast<std::uint16_t>(base + 6U))] = 0;
}

std::uint8_t RomBackedDualBus::open_bus(ProcessorId processor) const noexcept {
    return open_bus_[processor_index(processor)];
}

std::span<std::uint8_t> RomBackedDualBus::wram() noexcept {
    return wram_;
}

std::span<std::uint8_t> RomBackedDualBus::bwram() noexcept {
    return bwram_;
}

std::span<std::uint8_t, kKssSaveRamSize> RomBackedDualBus::persistent_bwram() noexcept {
    return std::span<std::uint8_t, kKssSaveRamSize>{bwram_.data(), kKssSaveRamSize};
}

std::span<const std::uint8_t, kKssSaveRamSize>
RomBackedDualBus::persistent_bwram() const noexcept {
    return std::span<const std::uint8_t, kKssSaveRamSize>{
        bwram_.data(), kKssSaveRamSize};
}

std::span<std::uint8_t> RomBackedDualBus::sa1_iram() noexcept {
    return sa1_iram_;
}

std::span<const std::uint8_t> RomBackedDualBus::rom() const noexcept {
    return rom_;
}

std::span<const DmaTransferRecord> RomBackedDualBus::dma_transfers() const noexcept {
    return dma_transfers_;
}

std::span<const DmaPortWrite> RomBackedDualBus::dma_port_writes() const noexcept {
    return dma_port_writes_;
}

std::uint32_t RomBackedDualBus::wram_port_address() const noexcept {
    return wram_port_address_;
}

const Sa1ControlState& RomBackedDualBus::sa1_control_state() const noexcept {
    return sa1_io_.state();
}

const PpuFunctionalState& RomBackedDualBus::ppu_state() const noexcept {
    return snes_io_.ppu_state();
}

std::span<const std::uint8_t> RomBackedDualBus::apu_input_ports() const noexcept {
    return snes_io_.apu_input_ports();
}

std::span<const std::uint8_t> RomBackedDualBus::apu_output_ports() const noexcept {
    return snes_io_.apu_output_ports();
}

std::span<const std::uint8_t> RomBackedDualBus::vram() const noexcept { return snes_io_.vram(); }
std::span<const std::uint8_t> RomBackedDualBus::cgram() const noexcept { return snes_io_.cgram(); }
std::span<const std::uint8_t> RomBackedDualBus::ppu_register_latches() const noexcept {
    return snes_io_.ppu_register_latches();
}

void RomBackedDualBus::set_controller_buttons(
    std::size_t port, std::uint16_t buttons) noexcept {
    snes_io_.set_controller_buttons(port,buttons);
}

std::uint16_t RomBackedDualBus::controller_buttons(std::size_t port) const noexcept {
    return snes_io_.controller_buttons(port);
}

bool RomBackedDualBus::provision_spc_ipl(std::span<const std::uint8_t> bytes) noexcept {
    return snes_io_.provision_spc_ipl(bytes);
}

apu::SpcStepResult RomBackedDualBus::step_spc() noexcept { return snes_io_.step_spc(); }
bool RomBackedDualBus::spc_provisioned() const noexcept { return snes_io_.spc_provisioned(); }
apu::Spc700Core* RomBackedDualBus::spc_core() noexcept { return snes_io_.spc_core(); }
const apu::Spc700Core* RomBackedDualBus::spc_core() const noexcept {
    return snes_io_.spc_core();
}

} // namespace kss
