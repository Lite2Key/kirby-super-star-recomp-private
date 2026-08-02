#include "kss/dual_bus.hpp"
#include "kss/native_host.hpp"
#include "kss/snes_timing.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
    std::uint16_t opened_width{};
    std::uint16_t opened_height{};
    std::size_t present_count{};
    std::vector<std::uint8_t> presented_red;
    std::vector<Tick> ticks;
    std::size_t next_tick{};

    bool open(std::uint16_t width, std::uint16_t height) override {
        opened = true;
        opened_width = width;
        opened_height = height;
        return open_result;
    }
    bool present(const kss::RgbaFrame& frame) override {
        presented = frame.valid();
        if (presented) {
            ++present_count;
            presented_red.push_back(frame.pixels[0]);
        }
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

struct FrameEvent {
    kss::HostFrameSourceStatus status{kss::HostFrameSourceStatus::no_frame};
    kss::MasterClock boundary_master{};
    kss::PpuFunctionalState state{};
    bool supplies_state{};
};

struct FrameSequence {
    std::vector<FrameEvent> events;
    std::size_t next_event{};

    static kss::HostFrameSourceStatus next(
        void* context, kss::HostFrameBoundarySnapshot& snapshot) noexcept {
        auto& self = *static_cast<FrameSequence*>(context);
        if (self.next_event >= self.events.size()) {
            return kss::HostFrameSourceStatus::source_error;
        }
        auto& event = self.events[self.next_event++];
        if (event.status == kss::HostFrameSourceStatus::frame_boundary) {
            snapshot.boundary_master = event.boundary_master;
            snapshot.ppu_state = event.supplies_state ? &event.state : nullptr;
        }
        return event.status;
    }

    kss::HostFrameSource source() noexcept { return {this, &FrameSequence::next}; }
};

kss::PpuFunctionalState forced_blank_state() {
    kss::PpuFunctionalState state{};
    state.forced_blank = true;
    state.brightness = 15;
    return state;
}

kss::PpuFunctionalState black_mode1_state() {
    kss::PpuFunctionalState state{};
    state.brightness = 0;
    state.registers[0x05] = 0x01;
    return state;
}

kss::PpuFunctionalState red_mode1_state() {
    auto state = black_mode1_state();
    state.brightness = 15;
    state.cgram[0] = 0x1f;
    return state;
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

void test_frame_session_consumes_only_supplied_boundaries_and_captures_visible() {
    FakePlatform platform;
    platform.ticks.resize(6);
    FrameSequence sequence;
    sequence.events = {
        {kss::HostFrameSourceStatus::no_frame},
        {kss::HostFrameSourceStatus::frame_boundary,
            kss::kSnesFirstFrameMasterClock, forced_blank_state(), true},
        {kss::HostFrameSourceStatus::frame_boundary,
            kss::kSnesFirstFrameMasterClock + 100U, black_mode1_state(), true},
        {kss::HostFrameSourceStatus::frame_boundary,
            kss::kSnesFirstFrameMasterClock + 200U, red_mode1_state(), true},
        {kss::HostFrameSourceStatus::no_frame},
        {kss::HostFrameSourceStatus::exhausted},
    };
    const auto output = std::filesystem::path{"native-host-first-visible-test.bmp"};
    std::filesystem::remove(output);
    const auto result = kss::run_native_host_frame_session(
        platform, sequence.source(), {}, output);

    assert(result.status == kss::HostSessionStatus::frame_source_exhausted);
    assert(result.observed_boundaries == 3U);
    assert(result.first_visible && result.first_visible->frame_ordinal == 3U);
    assert(result.first_visible->status == kss::FrameBoundaryStatus::visible);
    assert(result.first_visible_bmp_written);
    assert(platform.opened_width == kss::kSnesFrameWidth);
    assert(platform.opened_height == kss::kSnesFrameHeight);
    assert(platform.present_count == 3U); // no_frame never repeats an old surface.
    assert(platform.presented_red == (std::vector<std::uint8_t>{0, 0, 255}));
    assert(platform.closed);

    std::ifstream image(output, std::ios::binary | std::ios::ate);
    assert(image && image.tellg() == static_cast<std::streamoff>(
        54U + kss::kSnesFrameWidth * kss::kSnesFrameHeight * 4U));
    image.close();
    std::filesystem::remove(output);
}

void test_frame_session_rejects_non_monotonic_or_invalid_sources() {
    FakePlatform platform;
    platform.ticks.resize(2);
    FrameSequence repeated;
    repeated.events = {
        {kss::HostFrameSourceStatus::frame_boundary, 100U,
            forced_blank_state(), true},
        {kss::HostFrameSourceStatus::frame_boundary, 100U,
            red_mode1_state(), true},
    };
    auto result = kss::run_native_host_frame_session(
        platform, repeated.source(), {});
    assert(result.status == kss::HostSessionStatus::non_monotonic_frame_boundary);
    assert(result.observed_boundaries == 1U && !result.first_visible);
    assert(platform.present_count == 1U && platform.closed);

    FakePlatform missing_platform;
    missing_platform.ticks.resize(1);
    FrameSequence missing;
    missing.events = {{kss::HostFrameSourceStatus::frame_boundary, 200U,
        red_mode1_state(), false}};
    result = kss::run_native_host_frame_session(
        missing_platform, missing.source(), {});
    assert(result.status == kss::HostSessionStatus::frame_source_error);
    assert(result.observed_boundaries == 0U);
    assert(missing_platform.present_count == 0U && missing_platform.closed);

    FakePlatform null_platform;
    result = kss::run_native_host_frame_session(null_platform, {}, {});
    assert(result.status == kss::HostSessionStatus::frame_source_error);
    assert(!null_platform.opened && !null_platform.closed);
}

} // namespace

int main() {
    test_keyboard_mapping();
    test_gamepad_mapping_and_deadzone();
    test_headless_session_delivers_two_pads_and_closes();
    test_session_failures_are_explicit_and_closed();
    test_session_targets_existing_two_port_bus_api();
    test_frame_session_consumes_only_supplied_boundaries_and_captures_visible();
    test_frame_session_rejects_non_monotonic_or_invalid_sources();
}
