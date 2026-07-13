#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kss {

enum class SnesButton : std::uint16_t {
    b = 1U << 0U,
    y = 1U << 1U,
    select = 1U << 2U,
    start = 1U << 3U,
    up = 1U << 4U,
    down = 1U << 5U,
    left = 1U << 6U,
    right = 1U << 7U,
    a = 1U << 8U,
    x = 1U << 9U,
    l = 1U << 10U,
    r = 1U << 11U,
};

class SnesControllerPorts {
public:
    static constexpr std::size_t kPortCount = 2;
    static constexpr std::uint16_t kButtonMask = 0x0fffU;

    void set_buttons(std::size_t port, std::uint16_t buttons) noexcept;
    [[nodiscard]] std::uint16_t buttons(std::size_t port) const noexcept;
    void write_strobe(bool high) noexcept;
    [[nodiscard]] bool strobe() const noexcept;

    // Falling-edge serial order: B,Y,Select,Start,Up,Down,Left,Right,A,X,L,R,
    // then four zero padding bits; subsequent disconnected-line reads are one.
    [[nodiscard]] std::uint8_t read_serial(std::size_t port) noexcept;
    // CPU register layout: B at bit 15 through R at bit 4; bits 3-0 are the
    // standard gamepad signature zeros.
    [[nodiscard]] std::uint16_t auto_joypad(std::size_t port) const noexcept;

private:
    std::array<std::uint16_t, kPortCount> live_{};
    std::array<std::uint16_t, kPortCount> latched_{};
    std::array<std::uint8_t, kPortCount> bit_index_{};
    bool strobe_high_{};
};

} // namespace kss
