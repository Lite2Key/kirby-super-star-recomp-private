#pragma once

#include "kss/address_space.hpp"
#include "kss/bus.hpp"
#include "kss/sa1_registers.hpp"
#include "kss/save_ram.hpp"
#include "kss/snes_registers.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kss {

struct DmaTransferRecord {
    std::uint8_t channel{};
    std::uint8_t control{};
    std::uint32_t source_start{};
    std::uint32_t byte_count{};
    std::uint16_t b_bus_address{};

    friend constexpr bool operator==(const DmaTransferRecord&, const DmaTransferRecord&) = default;
};

struct DmaPortWrite {
    std::uint8_t channel{};
    std::uint32_t ordinal{};
    std::uint16_t address{};
    std::uint8_t value{};

    friend constexpr bool operator==(const DmaPortWrite&, const DmaPortWrite&) = default;
};

class RomBackedDualBus final : public Bus {
public:
    using CpuWriteSink = void (*)(void* context, ProcessorId processor,
        std::uint32_t address, std::uint8_t value) noexcept;

    explicit RomBackedDualBus(std::span<const std::uint8_t> rom);

    [[nodiscard]] std::uint8_t read8(
        ProcessorId processor,
        std::uint32_t address,
        BusAccessKind kind = BusAccessKind::data) override;

    void write8(
        ProcessorId processor,
        std::uint32_t address,
        std::uint8_t value,
        BusAccessKind kind = BusAccessKind::data) override;

    [[nodiscard]] std::uint8_t open_bus(ProcessorId processor) const noexcept;
    [[nodiscard]] std::span<std::uint8_t> wram() noexcept;
    [[nodiscard]] std::span<std::uint8_t> bwram() noexcept;
    // The physical cartridge persists only canonical BW-RAM $0000-$1FFF.
    // The remainder of the 256 KiB SA-1 BW-RAM model is volatile.
    [[nodiscard]] std::span<std::uint8_t, kKssSaveRamSize> persistent_bwram() noexcept;
    [[nodiscard]] std::span<const std::uint8_t, kKssSaveRamSize> persistent_bwram() const noexcept;
    [[nodiscard]] std::span<std::uint8_t> sa1_iram() noexcept;
    [[nodiscard]] std::span<const std::uint8_t> rom() const noexcept;
    [[nodiscard]] std::span<const DmaTransferRecord> dma_transfers() const noexcept;
    [[nodiscard]] std::span<const DmaPortWrite> dma_port_writes() const noexcept;
    [[nodiscard]] std::uint32_t wram_port_address() const noexcept;
    [[nodiscard]] const Sa1ControlState& sa1_control_state() const noexcept;
    [[nodiscard]] const PpuFunctionalState& ppu_state() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> apu_input_ports() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> apu_output_ports() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> vram() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> cgram() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> ppu_register_latches() const noexcept;
    // Host-facing controller injection. Button bits use SnesButton's serial
    // order and are masked to the standard 12-button gamepad surface.
    void set_controller_buttons(std::size_t port, std::uint16_t buttons) noexcept;
    [[nodiscard]] std::uint16_t controller_buttons(std::size_t port) const noexcept;
    [[nodiscard]] bool provision_spc_ipl(std::span<const std::uint8_t> bytes) noexcept;
    // Caller-driven single-instruction step; no bus access auto-steps SPC700.
    [[nodiscard]] apu::SpcStepResult step_spc() noexcept;
    [[nodiscard]] bool spc_provisioned() const noexcept;
    [[nodiscard]] apu::Spc700Core* spc_core() noexcept;
    [[nodiscard]] const apu::Spc700Core* spc_core() const noexcept;
    // Optional memory-only observation, including DMA-origin recursive writes.
    void set_cpu_write_sink(void* context, CpuWriteSink sink) noexcept {
        cpu_write_context_ = context;
        cpu_write_sink_ = sink;
    }

private:
    [[nodiscard]] static constexpr std::size_t processor_index(ProcessorId processor) noexcept {
        return processor == ProcessorId::sa1 ? 1U : 0U;
    }

    [[nodiscard]] std::uint8_t read_snes_register(std::uint16_t offset, BusAccessKind kind);
    void write_snes_register(std::uint16_t offset, std::uint8_t value, BusAccessKind kind);
    void run_dma(std::uint8_t enabled_channels);
    void run_dma_channel(std::uint8_t channel);

    std::vector<std::uint8_t> rom_;
    std::array<std::uint8_t, kSnesWramSize> wram_{};
    std::array<std::uint8_t, kSa1BwramSize> bwram_{};
    std::array<std::uint8_t, kSa1IramSize> sa1_iram_{};
    std::array<std::uint8_t, 0x2300> snes_registers_{};
    SnesRegisterFile snes_io_{};
    Sa1RegisterFile sa1_io_{};
    std::array<std::uint8_t, 2> open_bus_{};
    std::uint32_t wram_port_address_{};
    std::vector<DmaTransferRecord> dma_transfers_;
    std::vector<DmaPortWrite> dma_port_writes_;
    void* cpu_write_context_{};
    CpuWriteSink cpu_write_sink_{};
};

} // namespace kss
