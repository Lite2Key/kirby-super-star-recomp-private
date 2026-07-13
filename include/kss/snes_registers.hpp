#pragma once

#include "kss/controller.hpp"
#include "kss/spc700.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace kss {

struct PpuFunctionalState {
    bool forced_blank{};
    std::uint8_t brightness{};
    std::uint16_t vram_word_address{};
    std::uint8_t cgram_address{};
    std::uint16_t bg1_hscroll{};
    std::uint16_t bg1_vscroll{};
    std::uint16_t bg2_hscroll{};
    std::uint16_t bg2_vscroll{};
    std::uint16_t bg3_hscroll{};
    std::uint16_t bg3_vscroll{};
    std::uint16_t fixed_color{};
    // Renderer-visible state is owned here so a render depends only on one
    // self-contained functional snapshot, never on the register file or bus.
    std::array<std::uint8_t, 0x10000> vram{};
    std::array<std::uint8_t, 0x200> cgram{};
    std::array<std::uint8_t, 0x220> oam{};
    std::array<std::uint8_t, 0x40> registers{};
};

// Functional latches needed by the observed reset/first-frame register stream.
// SPC700 execution and PPU/master-clock timing remain separate future models.
class SnesRegisterFile {
public:
    SnesRegisterFile() noexcept;
    [[nodiscard]] std::uint8_t read(std::uint16_t address, std::uint8_t open_bus) noexcept;
    void write(std::uint16_t address, std::uint8_t value) noexcept;
    [[nodiscard]] bool provision_spc_ipl(std::span<const std::uint8_t> bytes) noexcept;
    // Executes exactly one instruction. The multi-clock coordinator is the
    // intended caller and must charge the returned instruction_cycles; port
    // reads/writes never invoke this method implicitly.
    [[nodiscard]] apu::SpcStepResult step_spc() noexcept;
    [[nodiscard]] bool spc_provisioned() const noexcept;
    [[nodiscard]] apu::Spc700Core* spc_core() noexcept;
    [[nodiscard]] const apu::Spc700Core* spc_core() const noexcept;
    [[nodiscard]] const PpuFunctionalState& ppu_state() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> apu_input_ports() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> apu_output_ports() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> vram() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> cgram() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> ppu_register_latches() const noexcept;
    void set_controller_buttons(std::size_t port, std::uint16_t buttons) noexcept;
    [[nodiscard]] std::uint16_t controller_buttons(std::size_t port) const noexcept;

private:
    void increment_vram() noexcept;
    std::array<std::uint8_t, 0x2300> registers_{};
    std::array<std::uint8_t, 4> apu_input_{};
    std::array<std::uint8_t, 4> apu_output_{};
    PpuFunctionalState ppu_{};
    bool cgram_high_{};
    std::uint8_t bg_scroll_latch_{};
    std::uint16_t oam_address_{};
    std::uint8_t oam_write_latch_{};
    SnesControllerPorts controllers_{};
    std::optional<apu::Spc700Core> spc_{};
};

} // namespace kss
