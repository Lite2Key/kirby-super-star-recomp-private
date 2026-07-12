#pragma once

#include "kss/address_space.hpp"
#include "kss/bus.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kss {

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

private:
    [[nodiscard]] static constexpr std::size_t processor_index(ProcessorId processor) noexcept {
        return processor == ProcessorId::sa1 ? 1U : 0U;
    }

    std::vector<std::uint8_t> rom_;
    std::array<std::uint8_t, kSnesWramSize> wram_{};
    std::array<std::uint8_t, kSa1BwramSize> bwram_{};
    std::array<std::uint8_t, kSa1IramSize> sa1_iram_{};
    std::array<std::uint8_t, 2> open_bus_{};
};

} // namespace kss
