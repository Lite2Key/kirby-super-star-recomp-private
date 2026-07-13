#pragma once

#include "kss/dsp_core.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace kss::apu {

enum class SpcStepStatus : std::uint8_t {
    executed,
    sleeping,
    unsupported_opcode,
    unsupported_io,
    stopped,
    missing_ipl,
};

struct Spc700Registers {
    std::uint16_t pc{};
    std::uint8_t a{};
    std::uint8_t x{};
    std::uint8_t y{};
    std::uint8_t sp{};
    std::uint8_t ps{};
    std::uint64_t cycles{};
};

struct SpcStepResult {
    SpcStepStatus status{SpcStepStatus::unsupported_opcode};
    std::uint8_t opcode{};
    std::uint8_t instruction_bytes{};
    std::uint8_t instruction_cycles{};
};

struct SpcTimerState {
    bool enabled{};
    std::uint8_t target{};
    std::uint32_t divider{};
    std::uint8_t stage2{};
    std::uint8_t output{};
};

// Isolated SPC700 slice for the first-frame IPL/upload path. Instruction
// organization and flag behavior were informed by ares' ISC-licensed SPC700
// component; this is a dependency-free rewrite. See licenses/ares-ISC.txt.
class Spc700Core {
public:
    static constexpr std::size_t kRamSize = 65536;
    static constexpr std::size_t kIplSize = 64;

    [[nodiscard]] bool load_ipl(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] bool reset() noexcept;
    [[nodiscard]] SpcStepResult step() noexcept;
    [[nodiscard]] dsp::DspClockResult clock_dsp_sample() noexcept;

    [[nodiscard]] std::uint8_t read8(std::uint16_t address) noexcept;
    void write8(std::uint16_t address, std::uint8_t value) noexcept;

    void cpu_write_port(std::uint8_t port, std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t cpu_read_port(std::uint8_t port) const noexcept;

    [[nodiscard]] Spc700Registers& registers() noexcept { return registers_; }
    [[nodiscard]] const Spc700Registers& registers() const noexcept { return registers_; }
    [[nodiscard]] std::span<std::uint8_t> ram() noexcept { return ram_; }
    [[nodiscard]] std::span<const std::uint8_t> cpu_input_ports() const noexcept {
        return cpu_to_spc_;
    }
    [[nodiscard]] std::span<const std::uint8_t> cpu_output_ports() const noexcept {
        return spc_to_cpu_;
    }
    [[nodiscard]] std::span<const std::uint8_t> dsp_registers() const noexcept {
        return dsp_registers_;
    }
    [[nodiscard]] const dsp::DspCore& dsp_core() const noexcept { return dsp_core_; }
    [[nodiscard]] std::uint8_t dsp_address() const noexcept { return dsp_address_; }
    [[nodiscard]] std::span<const std::uint8_t> auxiliary_io() const noexcept {
        return auxiliary_io_;
    }
    [[nodiscard]] const std::array<SpcTimerState, 3>& timers() const noexcept {
        return timers_;
    }
    [[nodiscard]] bool ipl_enabled() const noexcept { return ipl_enabled_; }
    [[nodiscard]] bool stopped() const noexcept { return stopped_; }
    [[nodiscard]] bool sleeping() const noexcept { return sleeping_; }

private:
    static constexpr std::uint8_t kCarry = 0x01;
    static constexpr std::uint8_t kZero = 0x02;
    static constexpr std::uint8_t kInterrupt = 0x04;
    static constexpr std::uint8_t kHalfCarry = 0x08;
    static constexpr std::uint8_t kBreak = 0x10;
    static constexpr std::uint8_t kDirectPage = 0x20;
    static constexpr std::uint8_t kOverflow = 0x40;
    static constexpr std::uint8_t kNegative = 0x80;

    [[nodiscard]] std::uint8_t fetch8() noexcept;
    [[nodiscard]] std::uint16_t direct_address(std::uint8_t offset) const noexcept;
    [[nodiscard]] std::uint8_t read_direct(std::uint8_t offset) noexcept;
    [[nodiscard]] std::uint16_t read_direct16(std::uint8_t offset) noexcept;
    void write_direct(std::uint8_t offset, std::uint8_t value) noexcept;
    void push(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t pull() noexcept;
    [[nodiscard]] std::uint8_t alu(std::uint8_t family,
        std::uint8_t lhs, std::uint8_t rhs) noexcept;
    [[nodiscard]] SpcStepResult step_alu_family(std::uint8_t opcode) noexcept;
    void set_nz8(std::uint8_t value) noexcept;
    void set_nz16(std::uint16_t value) noexcept;
    void compare8(std::uint8_t lhs, std::uint8_t rhs) noexcept;
    [[nodiscard]] SpcStepResult finish(
        std::uint8_t opcode, std::uint8_t bytes, std::uint8_t cycles) noexcept;
    void advance_timers(std::uint8_t cycles) noexcept;
    void write_test(std::uint8_t value) noexcept;
    void write_control(std::uint8_t value) noexcept;

    std::array<std::uint8_t, kRamSize> ram_{};
    std::array<std::uint8_t, kIplSize> ipl_{};
    std::array<std::uint8_t, 4> cpu_to_spc_{};
    std::array<std::uint8_t, 4> spc_to_cpu_{};
    dsp::DspRegisterFile dsp_registers_{};
    dsp::DspCore dsp_core_{};
    std::array<std::uint8_t, 2> auxiliary_io_{};
    std::array<SpcTimerState, 3> timers_{};
    Spc700Registers registers_{};
    std::uint8_t dsp_address_{};
    bool timers_disabled_{};
    bool timers_enabled_{true};
    bool ipl_loaded_{};
    bool ipl_enabled_{true};
    bool stopped_{};
    bool sleeping_{};
    bool io_fault_{};
};

} // namespace kss::apu
