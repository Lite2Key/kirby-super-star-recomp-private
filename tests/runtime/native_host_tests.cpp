#include "kss/dual_bus.hpp"
#include "kss/native_host.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

constexpr std::uint16_t button(kss::SnesButton value) {
    return static_cast<std::uint16_t>(value);
}

class FakePlatform final : public kss::NativeHostPlatform {
public:
    struct Tick {
        kss::HostPollStatus status{kss::HostPollStatus::running};
        kss::HostInputSnapshot input{};
    };

    bool open_result{true};
    bool present_result{true};
    bool opened{};
    bool presented{};
    bool closed{};
    std::vector<Tick> ticks;
    std::size_t next_tick{};

    bool open(std::uint16_t width, std::uint16_t height) override {
        opened = width == 1U && height == 1U;
        return open_result;
    }
    bool present(const kss::RgbaFrame& frame) override {
        presented = frame.valid();
        return present_result;
    }
    kss::HostPollStatus poll(kss::HostInputSnapshot& input) override {
        assert(next_tick < ticks.size());
        input = ticks[next_tick].input;
        return ticks[next_tick++].status;
    }
    void close() noexcept override { closed = true; }
};

struct RecordedControllers {
    std::vector<std::pair<std::size_t, std::uint16_t>> updates;
};

void record_buttons(void* context, std::size_t port, std::uint16_t buttons) noexcept {
    static_cast<RecordedControllers*>(context)->updates.emplace_back(port, buttons);
}

void set_bus_buttons(void* context, std::size_t port, std::uint16_t buttons) noexcept {
    static_cast<kss::RomBackedDualBus*>(context)->set_controller_buttons(port, buttons);
}

kss::RgbaFrame one_pixel_frame() {
    return {1, 1, {0x11, 0x22, 0x33, 0xff}};
}

void test_keyboard_mapping() {
    kss::HostKeyboardInput input{};
    input.up = true;
    input.left = true;
    input.b = true;
    input.a = true;
    input.start = true;
    input.r = true;
    assert(kss::map_keyboard_to_snes(input)
        == static_cast<std::uint16_t>(button(kss::SnesButton::up)
            | button(kss::SnesButton::left) | button(kss::SnesButton::b)
            | button(kss::SnesButton::a) | button(kss::SnesButton::start)
            | button(kss::SnesButton::r)));
}

void test_gamepad_mapping_and_deadzone() {
    kss::HostGamepadInput input{};
    input.buttons = static_cast<std::uint16_t>(kss::host_gamepad::a
        | kss::host_gamepad::x | kss::host_gamepad::back
        | kss::host_gamepad::right_shoulder);
    input.left_x = 12001;
    input.left_y = -12001;
    assert(kss::map_gamepad_to_snes(input)
        == static_cast<std::uint16_t>(button(kss::SnesButton::b)
            | button(kss::SnesButton::y) | button(kss::SnesButton::select)
            | button(kss::SnesButton::r) | button(kss::SnesButton::right)
            | button(kss::SnesButton::down)));
    input = {};
    input.left_x = kss::host_gamepad::stick_threshold;
    input.left_y = static_cast<std::int16_t>(-kss::host_gamepad::stick_threshold);
    assert(kss::map_gamepad_to_snes(input) == 0U);
}

void test_headless_session_delivers_two_pads_and_closes() {
    FakePlatform platform;
    FakePlatform::Tick first{};
    first.input.controller_buttons = {
        button(kss::SnesButton::b), button(kss::SnesButton::a)};
    FakePlatform::Tick unchanged = first;
    FakePlatform::Tick finish{};
    finish.status = kss::HostPollStatus::clean_shutdown;
    finish.input.controller_buttons = {0, button(kss::SnesButton::a)};
    platform.ticks = {first, unchanged, finish};
    RecordedControllers controllers;
    const auto result = kss::run_native_host_session(platform, one_pixel_frame(),
        {&controllers, &record_buttons});
    assert(result == kss::HostSessionStatus::clean_shutdown);
    assert(platform.opened && platform.presented && platform.closed);
    assert(controllers.updates == (std::vector<std::pair<std::size_t, std::uint16_t>>{
        {0, button(kss::SnesButton::b)}, {1, button(kss::SnesButton::a)}, {0, 0}}));
}

void test_session_failures_are_explicit_and_closed() {
    FakePlatform platform;
    kss::RgbaFrame invalid;
    assert(kss::run_native_host_session(platform, invalid, {})
        == kss::HostSessionStatus::invalid_frame);
    assert(!platform.opened && !platform.closed);

    platform.open_result = false;
    assert(kss::run_native_host_session(platform, one_pixel_frame(), {})
        == kss::HostSessionStatus::open_failed);
    assert(!platform.closed);

    platform.open_result = true;
    platform.present_result = false;
    assert(kss::run_native_host_session(platform, one_pixel_frame(), {})
        == kss::HostSessionStatus::presentation_failed);
    assert(platform.closed);
}

void test_session_targets_existing_two_port_bus_api() {
    FakePlatform platform;
    FakePlatform::Tick finish{};
    finish.status = kss::HostPollStatus::clean_shutdown;
    finish.input.controller_buttons = {
        static_cast<std::uint16_t>(button(kss::SnesButton::left)
            | button(kss::SnesButton::b)),
        static_cast<std::uint16_t>(button(kss::SnesButton::right)
            | button(kss::SnesButton::a))};
    platform.ticks = {finish};
    const std::array<std::uint8_t, 1> rom{};
    kss::RomBackedDualBus bus(rom);
    assert(kss::run_native_host_session(
        platform, one_pixel_frame(), {&bus, &set_bus_buttons})
        == kss::HostSessionStatus::clean_shutdown);
    assert(bus.controller_buttons(0) == finish.input.controller_buttons[0]);
    assert(bus.controller_buttons(1) == finish.input.controller_buttons[1]);
}

} // namespace

int main() {
    test_keyboard_mapping();
    test_gamepad_mapping_and_deadzone();
    test_headless_session_delivers_two_pads_and_closes();
    test_session_failures_are_explicit_and_closed();
    test_session_targets_existing_two_port_bus_api();
}
