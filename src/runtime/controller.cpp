#include "kss/controller.hpp"

namespace kss {

void SnesControllerPorts::set_buttons(
    std::size_t port, std::uint16_t buttons_value) noexcept {
    if (port >= kPortCount) return;
    live_[port] = static_cast<std::uint16_t>(buttons_value & kButtonMask);
    if (strobe_high_) latched_[port] = live_[port];
}

std::uint16_t SnesControllerPorts::buttons(std::size_t port) const noexcept {
    return port < kPortCount ? live_[port] : 0;
}

void SnesControllerPorts::write_strobe(bool high) noexcept {
    if ((strobe_high_ && !high) || high) {
        latched_ = live_;
        bit_index_.fill(0);
    }
    strobe_high_ = high;
}

bool SnesControllerPorts::strobe() const noexcept { return strobe_high_; }

std::uint8_t SnesControllerPorts::read_serial(std::size_t port) noexcept {
    if (port >= kPortCount) return 1;
    if (strobe_high_) return static_cast<std::uint8_t>(live_[port] & 1U);
    const auto index = bit_index_[port];
    if (index < 16U) ++bit_index_[port];
    if (index < 12U) {
        return static_cast<std::uint8_t>((latched_[port] >> index) & 1U);
    }
    return static_cast<std::uint8_t>(index >= 16U ? 1U : 0U);
}

std::uint16_t SnesControllerPorts::auto_joypad(std::size_t port) const noexcept {
    if (port >= kPortCount) return 0;
    // The CPU's auto-poll shift register presents the first serial bit (B) at
    // bit 15 and the twelfth (R) at bit 4, followed by the four signature bits.
    std::uint16_t result{};
    for (std::uint8_t serial_bit = 0; serial_bit < 12U; ++serial_bit) {
        if ((live_[port] & (1U << serial_bit)) != 0U)
            result = static_cast<std::uint16_t>(result | (1U << (15U - serial_bit)));
    }
    return result;
}

} // namespace kss
