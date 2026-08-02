#include "kss/snes_registers.hpp"

#include <utility>

namespace kss {

SnesRegisterFile::SnesRegisterFile() noexcept {
    // SPC700 IPL-ROM ready signature, observed before the first frame.
    apu_output_[0] = 0xaaU;
    apu_output_[1] = 0xbbU;
}

std::uint8_t SnesRegisterFile::read(std::uint16_t address, std::uint8_t open_bus) noexcept {
    if (address == 0x4016U) {
        return static_cast<std::uint8_t>((open_bus & 0xfcU) | controllers_.read_serial(0));
    }
    if (address == 0x4017U) {
        return static_cast<std::uint8_t>((open_bus & 0xe0U) | 0x1cU | controllers_.read_serial(1));
    }
    if (address >= 0x4218U && address <= 0x421bU) {
        const auto port = static_cast<std::size_t>((address - 0x4218U) >> 1U);
        const auto value = controllers_.auto_joypad(port);
        return static_cast<std::uint8_t>((value >> ((address & 1U) * 8U)) & 0xffU);
    }
    if (address >= 0x421cU && address <= 0x421fU) return 0; // No multitap data lines.
    if (address >= 0x2140U && address <= 0x2143U) {
        const auto port = static_cast<std::uint8_t>(address - 0x2140U);
        return spc_ ? spc_->cpu_read_port(port) : apu_output_[port];
    }
    // The observed PPU registers here are write-only. Preserve open-bus reads
    // instead of exposing implementation latches as guest-visible state.
    return open_bus;
}

void SnesRegisterFile::increment_vram() noexcept {
    constexpr std::array<std::uint16_t, 4> increments{1, 32, 128, 128};
    ppu_.vram_word_address = static_cast<std::uint16_t>(
        ppu_.vram_word_address + increments[registers_[0x15U] & 3U]);
}

void SnesRegisterFile::write(std::uint16_t address, std::uint8_t value) noexcept {
    if (!((address >= 0x2100U && address <= 0x21ffU)
        || (address >= 0x4000U && address <= 0x43ffU))) return;
    registers_[address - 0x2100U] = value;
    if (address <= 0x213fU) ppu_.registers[address - 0x2100U] = value;
    switch (address) {
    case 0x4016:
        controllers_.write_strobe((value & 1U) != 0U);
        break;
    case 0x2100:
        ppu_.forced_blank = (value & 0x80U) != 0U;
        ppu_.brightness = static_cast<std::uint8_t>(value & 0x0fU);
        break;
    case 0x2102:
        oam_address_ = static_cast<std::uint16_t>((oam_address_ & 0x0200U)
            | (static_cast<std::uint16_t>(value) << 1U));
        break;
    case 0x2103:
        oam_address_ = static_cast<std::uint16_t>((oam_address_ & 0x01ffU)
            | ((static_cast<std::uint16_t>(value) & 1U) << 9U));
        break;
    case 0x2104: {
        const auto oam_address = static_cast<std::uint16_t>(oam_address_++ & 0x03ffU);
        if ((oam_address & 0x0200U) != 0U) {
            ppu_.oam[0x0200U | (oam_address & 0x001fU)] = value;
        } else if ((oam_address & 1U) == 0U) {
            oam_write_latch_ = value;
        } else {
            ppu_.oam[oam_address - 1U] = oam_write_latch_;
            ppu_.oam[oam_address] = value;
        }
        break;
    }
    case 0x2116:
        ppu_.vram_word_address = static_cast<std::uint16_t>(
            (ppu_.vram_word_address & 0xff00U) | value);
        break;
    case 0x2117:
        ppu_.vram_word_address = static_cast<std::uint16_t>(
            (ppu_.vram_word_address & 0x00ffU) | (static_cast<std::uint16_t>(value) << 8U));
        break;
    case 0x2118:
        ppu_.vram[(static_cast<std::uint32_t>(ppu_.vram_word_address) * 2U) & 0xffffU] = value;
        if ((registers_[0x15U] & 0x80U) == 0U) increment_vram();
        break;
    case 0x2119:
        ppu_.vram[(static_cast<std::uint32_t>(ppu_.vram_word_address) * 2U + 1U) & 0xffffU] = value;
        if ((registers_[0x15U] & 0x80U) != 0U) increment_vram();
        break;
    case 0x2121:
        ppu_.cgram_address = value;
        cgram_high_ = false;
        break;
    case 0x2122: {
        const auto byte_address = static_cast<std::uint16_t>(ppu_.cgram_address) * 2U
            + static_cast<std::uint16_t>(cgram_high_);
        ppu_.cgram[byte_address & 0x01ffU] = value;
        if (cgram_high_) ++ppu_.cgram_address;
        cgram_high_ = !cgram_high_;
        break;
    }
    case 0x2132: {
        const auto component = static_cast<std::uint16_t>(value & 0x1fU);
        if ((value & 0x20U) != 0U)
            ppu_.fixed_color = static_cast<std::uint16_t>((ppu_.fixed_color & ~0x001fU) | component);
        if ((value & 0x40U) != 0U)
            ppu_.fixed_color = static_cast<std::uint16_t>((ppu_.fixed_color & ~0x03e0U) | (component << 5U));
        if ((value & 0x80U) != 0U)
            ppu_.fixed_color = static_cast<std::uint16_t>((ppu_.fixed_color & ~0x7c00U) | (component << 10U));
        break;
    }
    case 0x211b:
        ppu_.mode7_a = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | mode7_latch_);
        mode7_latch_ = value;
        break;
    case 0x211c:
        ppu_.mode7_b = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | mode7_latch_);
        mode7_latch_ = value;
        break;
    case 0x211d:
        ppu_.mode7_c = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | mode7_latch_);
        mode7_latch_ = value;
        break;
    case 0x211e:
        ppu_.mode7_d = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | mode7_latch_);
        mode7_latch_ = value;
        break;
    case 0x211f:
        ppu_.mode7_center_x = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | mode7_latch_);
        mode7_latch_ = value;
        break;
    case 0x2120:
        ppu_.mode7_center_y = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | mode7_latch_);
        mode7_latch_ = value;
        break;
    case 0x210d: // BG1HOFS: shared latch, including the three retained fine-scroll bits.
        ppu_.bg1_hscroll = static_cast<std::uint16_t>((static_cast<std::uint16_t>(value) << 8U)
            | (bg_scroll_latch_ & 0xf8U) | ((ppu_.bg1_hscroll >> 8U) & 0x07U));
        ppu_.mode7_hofs = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | mode7_latch_);
        bg_scroll_latch_ = value;
        mode7_latch_ = value;
        break;
    case 0x210e: // BG1VOFS
        ppu_.bg1_vscroll = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | bg_scroll_latch_);
        ppu_.mode7_vofs = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | mode7_latch_);
        bg_scroll_latch_ = value;
        mode7_latch_ = value;
        break;
    case 0x210f: // BG2HOFS
        ppu_.bg2_hscroll = static_cast<std::uint16_t>((static_cast<std::uint16_t>(value) << 8U)
            | (bg_scroll_latch_ & 0xf8U) | ((ppu_.bg2_hscroll >> 8U) & 0x07U));
        bg_scroll_latch_ = value;
        break;
    case 0x2110: // BG2VOFS
        ppu_.bg2_vscroll = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | bg_scroll_latch_);
        bg_scroll_latch_ = value;
        break;
    case 0x2111: // BG3HOFS
        ppu_.bg3_hscroll = static_cast<std::uint16_t>((static_cast<std::uint16_t>(value) << 8U)
            | (bg_scroll_latch_ & 0xf8U) | ((ppu_.bg3_hscroll >> 8U) & 0x07U));
        bg_scroll_latch_ = value;
        break;
    case 0x2112: // BG3VOFS
        ppu_.bg3_vscroll = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value) << 8U) | bg_scroll_latch_);
        bg_scroll_latch_ = value;
        break;
    case 0x2140: case 0x2141: case 0x2142: case 0x2143: {
        const auto port = static_cast<std::size_t>(address - 0x2140U);
        apu_input_[port] = value;
        if (spc_) spc_->cpu_write_port(static_cast<std::uint8_t>(port), value);
        // The SPC700 owns the output latch. Without an SPC execution model an
        // S-CPU write must not be echoed: the first-frame oracle proves later
        // acknowledgements, but not zero-latency behavior.
        break;
    }
    default: break;
    }
}

bool SnesRegisterFile::provision_spc_ipl(std::span<const std::uint8_t> bytes) noexcept {
    apu::Spc700Core candidate;
    if (!candidate.load_ipl(bytes) || !candidate.reset()) return false;
    for (std::uint8_t port = 0; port < apu_input_.size(); ++port) {
        candidate.cpu_write_port(port, apu_input_[port]);
    }
    spc_ = std::move(candidate);
    return true;
}

apu::SpcStepResult SnesRegisterFile::step_spc() noexcept {
    if (!spc_) return {apu::SpcStepStatus::missing_ipl, 0, 0, 0};
    return spc_->step();
}

bool SnesRegisterFile::spc_provisioned() const noexcept { return spc_.has_value(); }
apu::Spc700Core* SnesRegisterFile::spc_core() noexcept {
    return spc_ ? &*spc_ : nullptr;
}
const apu::Spc700Core* SnesRegisterFile::spc_core() const noexcept {
    return spc_ ? &*spc_ : nullptr;
}

const PpuFunctionalState& SnesRegisterFile::ppu_state() const noexcept { return ppu_; }
std::span<const std::uint8_t> SnesRegisterFile::apu_input_ports() const noexcept {
    return spc_ ? spc_->cpu_input_ports() : std::span<const std::uint8_t>(apu_input_);
}
std::span<const std::uint8_t> SnesRegisterFile::apu_output_ports() const noexcept {
    return spc_ ? spc_->cpu_output_ports() : std::span<const std::uint8_t>(apu_output_);
}
std::span<const std::uint8_t> SnesRegisterFile::vram() const noexcept { return ppu_.vram; }
std::span<const std::uint8_t> SnesRegisterFile::cgram() const noexcept { return ppu_.cgram; }
std::span<const std::uint8_t> SnesRegisterFile::ppu_register_latches() const noexcept {
    return std::span<const std::uint8_t>(registers_.data(), 0x40U);
}

void SnesRegisterFile::set_controller_buttons(
    std::size_t port, std::uint16_t buttons) noexcept {
    controllers_.set_buttons(port,buttons);
}

std::uint16_t SnesRegisterFile::controller_buttons(std::size_t port) const noexcept {
    return controllers_.buttons(port);
}

} // namespace kss
