#include "kss/sa1_registers.hpp"

namespace kss {

std::uint8_t Sa1RegisterFile::read(
    ProcessorId processor, std::uint16_t address, std::uint8_t open_bus) const noexcept {
    if (processor == ProcessorId::snes_cpu && address == 0x2300U) {
        return state_.snes_message;
    }
    if (processor == ProcessorId::sa1 && address == 0x2301U) {
        return static_cast<std::uint8_t>(state_.sa1_message
            | (state_.nmi_requested ? 0x10U : 0U)
            | (state_.irq_requested ? 0x80U : 0U));
    }
    // Other registers in this bounded subset are write-only. Returning their
    // stored value would fabricate readable behavior not present in evidence.
    return open_bus;
}

void Sa1RegisterFile::write(
    ProcessorId processor, std::uint16_t address, std::uint8_t value) noexcept {
    if (address < 0x2200U || address > 0x23ffU) return;
    registers_[address - 0x2200U] = value;
    if (processor == ProcessorId::snes_cpu) {
        switch (address) {
        case 0x2200: {
            const auto prior_reset = state_.reset;
            state_.sa1_message = static_cast<std::uint8_t>(value & 0x0fU);
            state_.nmi_requested = (value & 0x10U) != 0U;
            state_.reset = (value & 0x20U) != 0U;
            state_.wait = (value & 0x40U) != 0U;
            state_.irq_requested = (value & 0x80U) != 0U;
            if (!state_.reset && prior_reset) state_.sa1_iram_write_mask = 0;
            break;
        }
        case 0x2203:
            state_.reset_vector = static_cast<std::uint16_t>(
                (state_.reset_vector & 0xff00U) | value);
            break;
        case 0x2204:
            state_.reset_vector = static_cast<std::uint16_t>(
                (state_.reset_vector & 0x00ffU) | (static_cast<std::uint16_t>(value) << 8U));
            break;
        case 0x2229: state_.snes_iram_write_mask = value; break;
        default: break;
        }
    } else {
        switch (address) {
        case 0x2209: state_.snes_message = static_cast<std::uint8_t>(value & 0x0fU); break;
        case 0x220a: break; // enables are latched, but timing/interrupt delivery is not modeled here
        case 0x220b:
            if ((value & 0x80U) != 0U) state_.irq_requested = false;
            if ((value & 0x10U) != 0U) state_.nmi_requested = false;
            break;
        case 0x222a: state_.sa1_iram_write_mask = value; break;
        default: break;
        }
    }
}

bool Sa1RegisterFile::iram_write_enabled(
    ProcessorId processor, std::uint16_t offset) const noexcept {
    if (offset >= 0x0800U) return false;
    const auto mask = processor == ProcessorId::sa1
        ? state_.sa1_iram_write_mask : state_.snes_iram_write_mask;
    return (mask & static_cast<std::uint8_t>(1U << ((offset >> 8U) & 7U))) != 0U;
}

const Sa1ControlState& Sa1RegisterFile::state() const noexcept { return state_; }

} // namespace kss
