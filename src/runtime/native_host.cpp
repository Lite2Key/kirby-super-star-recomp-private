#include "kss/native_host.hpp"

#include <algorithm>

namespace kss {
namespace {

constexpr std::uint16_t button(SnesButton value) noexcept {
    return static_cast<std::uint16_t>(value);
}

void include_if(std::uint16_t& result, bool pressed, SnesButton value) noexcept {
    if (pressed) result = static_cast<std::uint16_t>(result | button(value));
}

} // namespace

std::uint16_t map_keyboard_to_snes(const HostKeyboardInput& input) noexcept {
    std::uint16_t result{};
    include_if(result, input.up, SnesButton::up);
    include_if(result, input.down, SnesButton::down);
    include_if(result, input.left, SnesButton::left);
    include_if(result, input.right, SnesButton::right);
    include_if(result, input.b, SnesButton::b);
    include_if(result, input.a, SnesButton::a);
    include_if(result, input.y, SnesButton::y);
    include_if(result, input.x, SnesButton::x);
    include_if(result, input.select, SnesButton::select);
    include_if(result, input.start, SnesButton::start);
    include_if(result, input.l, SnesButton::l);
    include_if(result, input.r, SnesButton::r);
    return result;
}

std::uint16_t map_gamepad_to_snes(const HostGamepadInput& input) noexcept {
    const auto pressed = [&input](std::uint16_t mask) {
        return (input.buttons & mask) != 0U;
    };
    std::uint16_t result{};
    include_if(result, pressed(host_gamepad::dpad_up)
        || input.left_y > host_gamepad::stick_threshold, SnesButton::up);
    include_if(result, pressed(host_gamepad::dpad_down)
        || input.left_y < -host_gamepad::stick_threshold, SnesButton::down);
    include_if(result, pressed(host_gamepad::dpad_left)
        || input.left_x < -host_gamepad::stick_threshold, SnesButton::left);
    include_if(result, pressed(host_gamepad::dpad_right)
        || input.left_x > host_gamepad::stick_threshold, SnesButton::right);
    // Physical-position mapping: Xbox bottom/right/left/top -> SNES B/A/Y/X.
    include_if(result, pressed(host_gamepad::a), SnesButton::b);
    include_if(result, pressed(host_gamepad::b), SnesButton::a);
    include_if(result, pressed(host_gamepad::x), SnesButton::y);
    include_if(result, pressed(host_gamepad::y), SnesButton::x);
    include_if(result, pressed(host_gamepad::back), SnesButton::select);
    include_if(result, pressed(host_gamepad::start), SnesButton::start);
    include_if(result, pressed(host_gamepad::left_shoulder), SnesButton::l);
    include_if(result, pressed(host_gamepad::right_shoulder), SnesButton::r);
    return result;
}

HostSessionStatus run_native_host_session(
    NativeHostPlatform& platform,
    const RgbaFrame& frame,
    HostControllerSink controllers) noexcept {
    if (!frame.valid()) return HostSessionStatus::invalid_frame;
    if (!platform.open(frame.width, frame.height)) return HostSessionStatus::open_failed;
    struct CloseGuard {
        NativeHostPlatform& platform;
        ~CloseGuard() { platform.close(); }
    } close_guard{platform};
    if (!platform.present(frame)) return HostSessionStatus::presentation_failed;

    HostInputSnapshot input{};
    std::array<std::uint16_t, SnesControllerPorts::kPortCount> delivered{};
    delivered.fill(0xffffU);
    for (;;) {
        const auto status = platform.poll(input);
        for (std::size_t port = 0; port < delivered.size(); ++port) {
            const auto buttons = static_cast<std::uint16_t>(
                input.controller_buttons[port] & SnesControllerPorts::kButtonMask);
            if (buttons != delivered[port]) {
                controllers.update(port, buttons);
                delivered[port] = buttons;
            }
        }
        if (status == HostPollStatus::clean_shutdown) {
            return HostSessionStatus::clean_shutdown;
        }
        if (status == HostPollStatus::platform_error) {
            return HostSessionStatus::platform_error;
        }
    }
}

HostFrameSessionResult run_native_host_frame_session(
    NativeHostPlatform& platform,
    HostFrameSource frames,
    HostControllerSink controllers,
    const std::filesystem::path& first_visible_bmp) noexcept {
    HostFrameSessionResult result;
    if (!frames.next_frame) {
        result.status = HostSessionStatus::frame_source_error;
        return result;
    }
    if (!platform.open(kSnesFrameWidth, kSnesFrameHeight)) {
        result.status = HostSessionStatus::open_failed;
        return result;
    }
    struct CloseGuard {
        NativeHostPlatform& platform;
        ~CloseGuard() { platform.close(); }
    } close_guard{platform};

    VisibleFrameCapture capture;
    HostInputSnapshot input{};
    std::array<std::uint16_t, SnesControllerPorts::kPortCount> delivered{};
    delivered.fill(0xffffU);

    const auto finish = [&result, &capture](HostSessionStatus status) {
        result.status = status;
        result.observed_boundaries = capture.observed_boundaries();
        result.first_visible = capture.first_visible_observation();
        return result;
    };

    for (;;) {
        const auto poll_status = platform.poll(input);
        for (std::size_t port = 0; port < delivered.size(); ++port) {
            const auto buttons = static_cast<std::uint16_t>(
                input.controller_buttons[port] & SnesControllerPorts::kButtonMask);
            if (buttons != delivered[port]) {
                controllers.update(port, buttons);
                delivered[port] = buttons;
            }
        }
        if (poll_status == HostPollStatus::clean_shutdown) {
            return finish(HostSessionStatus::clean_shutdown);
        }
        if (poll_status == HostPollStatus::platform_error) {
            return finish(HostSessionStatus::platform_error);
        }

        HostFrameBoundarySnapshot snapshot{};
        switch (frames.next(snapshot)) {
        case HostFrameSourceStatus::no_frame:
            continue;
        case HostFrameSourceStatus::exhausted:
            return finish(HostSessionStatus::frame_source_exhausted);
        case HostFrameSourceStatus::source_error:
            return finish(HostSessionStatus::frame_source_error);
        case HostFrameSourceStatus::frame_boundary:
            break;
        }
        if (!snapshot.ppu_state) {
            return finish(HostSessionStatus::frame_source_error);
        }

        const auto observation = capture.observe(
            snapshot.boundary_master, *snapshot.ppu_state);
        if (observation.status == FrameBoundaryStatus::non_monotonic_boundary) {
            return finish(HostSessionStatus::non_monotonic_frame_boundary);
        }

        auto rendered = SnesFrameRenderer::render(*snapshot.ppu_state);
        if (rendered.status == FrameRenderStatus::rendered) {
            if (!rendered.frame.valid()) {
                return finish(HostSessionStatus::invalid_frame);
            }
            if (!platform.present(rendered.frame)) {
                return finish(HostSessionStatus::presentation_failed);
            }
        }

        if (observation.captured_first_visible && !first_visible_bmp.empty()) {
            if (capture.write_first_visible_bmp(first_visible_bmp)
                != FrameWriteStatus::written) {
                return finish(HostSessionStatus::capture_write_failed);
            }
            result.first_visible_bmp_written = true;
        }
    }
}

} // namespace kss
