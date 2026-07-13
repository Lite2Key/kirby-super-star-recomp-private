#include "kss/address_space.hpp"

namespace kss {
namespace {

constexpr auto kReadWrite = MemoryPermission::read | MemoryPermission::write;
constexpr auto kReadExecute = MemoryPermission::read | MemoryPermission::execute;
constexpr auto kReadWriteExecute = kReadWrite | MemoryPermission::execute;

AddressMapping map_common_cartridge(std::uint8_t bank, std::uint16_t offset) noexcept {
    if ((bank <= 0x3fU || (bank >= 0x80U && bank <= 0xbfU)) && offset >= 0x8000U) {
        const auto logical_bank = static_cast<std::uint32_t>(bank & 0x7fU);
        return {
            MemoryRegion::rom,
            logical_bank * 0x8000U + (offset & 0x7fffU),
            kReadExecute,
            bank >= 0x80U,
            true,
        };
    }
    if (bank >= 0xc0U) {
        return {
            MemoryRegion::rom,
            static_cast<std::uint32_t>(bank - 0xc0U) * 0x10000U + offset,
            kReadExecute,
            false,
            true,
        };
    }
    return {};
}

AddressMapping map_bwram(std::uint8_t bank, std::uint16_t offset) noexcept {
    if (bank >= 0x40U && bank <= 0x4fU) {
        return {
            MemoryRegion::bwram,
            (static_cast<std::uint32_t>(bank - 0x40U) * 0x10000U + offset)
                % static_cast<std::uint32_t>(kSa1BwramSize),
            kReadWriteExecute,
            bank >= 0x44U,
            false,
        };
    }
    if ((bank <= 0x3fU || (bank >= 0x80U && bank <= 0xbfU))
        && offset >= 0x6000U && offset <= 0x7fffU) {
        return {
            MemoryRegion::bwram,
            static_cast<std::uint32_t>(offset - 0x6000U),
            kReadWriteExecute,
            true,
            true,
        };
    }
    return {};
}

} // namespace

AddressMapping map_address(ProcessorId processor, std::uint32_t address) noexcept {
    address &= 0x00ff'ffffU;
    const auto bank = static_cast<std::uint8_t>(address >> 16U);
    const auto offset = static_cast<std::uint16_t>(address);

    if (processor == ProcessorId::snes_cpu) {
        if (bank == 0x7eU || bank == 0x7fU) {
            return {
                MemoryRegion::wram,
                static_cast<std::uint32_t>(bank - 0x7eU) * 0x10000U + offset,
                kReadWriteExecute,
                false,
                false,
            };
        }
        if ((bank <= 0x3fU || (bank >= 0x80U && bank <= 0xbfU)) && offset <= 0x1fffU) {
            return {MemoryRegion::wram, offset, kReadWriteExecute, true, false};
        }
    } else if ((bank <= 0x3fU || (bank >= 0x80U && bank <= 0xbfU)) && offset <= 0x07ffU) {
        return {MemoryRegion::sa1_iram, offset, kReadWriteExecute, bank != 0U, false};
    }

    // Both processors observe the cartridge I-RAM window at $3000-$3FFF in
    // low hardware banks. Only the lower 2 KiB is connected; the upper half
    // is represented by offsets >= kSa1IramSize so the bus can return zero and
    // ignore writes without aliasing it onto live RAM.
    const auto low_hardware_bank = bank <= 0x3fU || (bank >= 0x80U && bank <= 0xbfU);
    if (low_hardware_bank && offset >= 0x3000U && offset <= 0x3fffU) {
        return {MemoryRegion::sa1_iram, static_cast<std::uint32_t>(offset - 0x3000U),
            kReadWrite, bank != 0U, true};
    }

    const auto sa1_register = offset >= 0x2200U && offset <= 0x23ffU;
    const auto snes_register = processor == ProcessorId::snes_cpu
        && ((offset >= 0x2100U && offset <= 0x21ffU)
            || (offset >= 0x4000U && offset <= 0x43ffU));
    if (low_hardware_bank && (sa1_register || snes_register)) {
        return {
            MemoryRegion::hardware_register,
            offset,
            kReadWrite,
            bank != 0U,
            true,
        };
    }

    const auto bwram = map_bwram(bank, offset);
    if (bwram.mapped()) {
        return bwram;
    }
    return map_common_cartridge(bank, offset);
}

} // namespace kss
