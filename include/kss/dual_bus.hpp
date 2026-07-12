#pragma once

#include "kss/address_space.hpp"
#include "kss/bus.hpp"

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
    [[nodiscard]] std::span<std::uint8_t> sa1_iram() noexcept;
    [[nodiscard]] std::span<const std::uint8_t> rom() const noexcept;
    [[nodiscard]] std::span<const DmaTransferRecord> dma_transfers() const noexcept;
    [[nodiscard]] std::span<const DmaPortWrite> dma_port_writes() const noexcept;
    [[nodiscard]] std::uint32_t wram_port_address() const noexcept;

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
    std::array<std::uint8_t, 2> open_bus_{};
    std::uint32_t wram_port_address_{};
    std::vector<DmaTransferRecord> dma_transfers_;
    std::vector<DmaPortWrite> dma_port_writes_;
};

} // namespace kss
