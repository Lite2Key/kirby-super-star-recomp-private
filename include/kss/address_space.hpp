#pragma once

#include "kss/cpu.hpp"

#include <cstddef>
#include <cstdint>

namespace kss {

inline constexpr std::size_t kSnesWramSize = 128U * 1024U;
inline constexpr std::size_t kSa1BwramSize = 256U * 1024U;
inline constexpr std::size_t kSa1IramSize = 2U * 1024U;

enum class MemoryRegion : std::uint8_t {
    unmapped,
    rom,
    wram,
    bwram,
    sa1_iram,
    hardware_register,
};

enum class MemoryPermission : std::uint8_t {
    none = 0,
    read = 1U << 0U,
    write = 1U << 1U,
    execute = 1U << 2U,
};

[[nodiscard]] constexpr MemoryPermission operator|(
    MemoryPermission left,
    MemoryPermission right) noexcept {
    return static_cast<MemoryPermission>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool has_permission(
    MemoryPermission permissions,
    MemoryPermission requested) noexcept {
    return (static_cast<std::uint8_t>(permissions) & static_cast<std::uint8_t>(requested))
        == static_cast<std::uint8_t>(requested);
}

struct AddressMapping {
    MemoryRegion region{MemoryRegion::unmapped};
    std::uint32_t canonical_offset{};
    MemoryPermission permissions{MemoryPermission::none};
    bool mirrored{};
    bool placeholder{};

    [[nodiscard]] constexpr bool mapped() const noexcept {
        return region != MemoryRegion::unmapped;
    }

    friend constexpr bool operator==(const AddressMapping&, const AddressMapping&) = default;
};

// Describes the fixed power-on map. Dynamic SA-1 bank-selection and BWRAM
// window registers deliberately remain marked as placeholders until the
// cartridge register model owns them.
[[nodiscard]] AddressMapping map_address(ProcessorId processor, std::uint32_t address) noexcept;

} // namespace kss
