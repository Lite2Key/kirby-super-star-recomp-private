#pragma once

#include <cstdint>
#include <functional>

namespace kss {

enum class ProcessorId : std::uint8_t {
    snes_cpu = 0,
    sa1 = 1,
};

enum class StatusFlag : std::uint8_t {
    carry = 0x01,
    zero = 0x02,
    irq_disable = 0x04,
    decimal = 0x08,
    index_width = 0x10,
    accumulator_width = 0x20,
    overflow = 0x40,
    negative = 0x80,
};

struct CpuMode {
    bool emulation{true};
    bool accumulator_8bit{true};
    bool index_8bit{true};

    [[nodiscard]] static constexpr CpuMode normalized(bool e, bool m, bool x) noexcept {
        return CpuMode{e, e || m, e || x};
    }

    friend constexpr bool operator==(const CpuMode&, const CpuMode&) = default;
};

struct BlockKey {
    ProcessorId processor{ProcessorId::snes_cpu};
    std::uint32_t address{};
    CpuMode mode{};

    [[nodiscard]] static constexpr BlockKey make(
        ProcessorId processor_id,
        std::uint32_t pc24,
        bool e,
        bool m,
        bool x) noexcept {
        return BlockKey{processor_id, pc24 & 0x00ff'ffffU, CpuMode::normalized(e, m, x)};
    }

    friend constexpr bool operator==(const BlockKey&, const BlockKey&) = default;
};

struct BlockKeyHash {
    [[nodiscard]] std::size_t operator()(const BlockKey& key) const noexcept {
        const auto mode = static_cast<std::size_t>(key.mode.emulation)
            | (static_cast<std::size_t>(key.mode.accumulator_8bit) << 1U)
            | (static_cast<std::size_t>(key.mode.index_8bit) << 2U);
        return (static_cast<std::size_t>(key.address) << 5U)
            ^ (static_cast<std::size_t>(key.processor) << 3U) ^ mode;
    }
};

struct CpuContext {
    ProcessorId processor{ProcessorId::snes_cpu};
    std::uint16_t a{};
    std::uint16_t x{};
    std::uint16_t y{};
    std::uint16_t direct_page{};
    std::uint16_t stack_pointer{0x01ff};
    std::uint16_t pc{};
    std::uint8_t program_bank{};
    std::uint8_t data_bank{};
    std::uint8_t status{0x34};
    bool emulation{true};
    bool waiting{};
    bool stopped{};
    bool irq_pending{};
    bool nmi_pending{};
    std::uint64_t cycles{};

    [[nodiscard]] constexpr bool flag(StatusFlag value) const noexcept {
        return (status & static_cast<std::uint8_t>(value)) != 0;
    }

    constexpr void set_flag(StatusFlag value, bool enabled) noexcept {
        const auto mask = static_cast<std::uint8_t>(value);
        status = enabled ? static_cast<std::uint8_t>(status | mask)
                         : static_cast<std::uint8_t>(status & static_cast<std::uint8_t>(~mask));
    }

    [[nodiscard]] constexpr CpuMode mode() const noexcept {
        return CpuMode::normalized(
            emulation,
            flag(StatusFlag::accumulator_width),
            flag(StatusFlag::index_width));
    }

    [[nodiscard]] constexpr std::uint32_t address() const noexcept {
        return (static_cast<std::uint32_t>(program_bank) << 16U) | pc;
    }

    [[nodiscard]] constexpr BlockKey block_key() const noexcept {
        const auto current_mode = mode();
        return BlockKey{processor, address(), current_mode};
    }

    constexpr void normalize_after_mode_change() noexcept {
        if (emulation) {
            set_flag(StatusFlag::accumulator_width, true);
            set_flag(StatusFlag::index_width, true);
            stack_pointer = static_cast<std::uint16_t>(0x0100U | (stack_pointer & 0x00ffU));
        }
        if (mode().index_8bit) {
            x &= 0x00ffU;
            y &= 0x00ffU;
        }
    }
};

} // namespace kss
