#pragma once

#include "kss/controller.hpp"
#include "kss/snes_frame_renderer.hpp"
#include "kss/visible_frame_capture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace kss {

struct HostKeyboardInput {
    bool up{};
    bool down{};
    bool left{};
    bool right{};
    bool b{};
    bool a{};
    bool y{};
    bool x{};
    bool select{};
    bool start{};
    bool l{};
    bool r{};
};

// This dependency-free shape mirrors the standard XInput gamepad bits without
// making the portable runtime library include or link against the Windows SDK.
struct HostGamepadInput {
    std::uint16_t buttons{};
    std::int16_t left_x{};
    std::int16_t left_y{};
};

namespace host_gamepad {
inline constexpr std::uint16_t dpad_up = 0x0001U;
inline constexpr std::uint16_t dpad_down = 0x0002U;
inline constexpr std::uint16_t dpad_left = 0x0004U;
inline constexpr std::uint16_t dpad_right = 0x0008U;
inline constexpr std::uint16_t start = 0x0010U;
inline constexpr std::uint16_t back = 0x0020U;
inline constexpr std::uint16_t left_shoulder = 0x0100U;
inline constexpr std::uint16_t right_shoulder = 0x0200U;
inline constexpr std::uint16_t a = 0x1000U;
inline constexpr std::uint16_t b = 0x2000U;
inline constexpr std::uint16_t x = 0x4000U;
inline constexpr std::uint16_t y = 0x8000U;
inline constexpr std::int16_t stick_threshold = 12000;
} // namespace host_gamepad

[[nodiscard]] std::uint16_t map_keyboard_to_snes(
    const HostKeyboardInput& input) noexcept;
[[nodiscard]] std::uint16_t map_gamepad_to_snes(
    const HostGamepadInput& input) noexcept;

struct HostInputSnapshot {
    std::array<std::uint16_t, SnesControllerPorts::kPortCount> controller_buttons{};
};

struct HostControllerSink {
    void* context{};
    void (*set_buttons)(void*, std::size_t, std::uint16_t) noexcept{};

    void update(std::size_t port, std::uint16_t buttons) const noexcept {
        if (set_buttons) set_buttons(context, port, buttons);
    }
};

enum class HostPollStatus : std::uint8_t {
    running,
    clean_shutdown,
    platform_error,
};

enum class HostSessionStatus : std::uint8_t {
    not_requested,
    clean_shutdown,
    invalid_frame,
    open_failed,
    presentation_failed,
    platform_error,
    frame_source_exhausted,
    frame_source_error,
    non_monotonic_frame_boundary,
    capture_write_failed,
};

enum class HostFrameSourceStatus : std::uint8_t {
    no_frame,
    frame_boundary,
    exhausted,
    source_error,
};

struct HostFrameBoundarySnapshot {
    MasterClock boundary_master{};
    // The source retains ownership. The snapshot must remain immutable while
    // the host consumes this boundary, through the next next_frame call.
    const PpuFunctionalState* ppu_state{};
};

struct HostFrameSource {
    void* context{};
    HostFrameSourceStatus (*next_frame)(
        void*, HostFrameBoundarySnapshot&) noexcept{};

    [[nodiscard]] HostFrameSourceStatus next(
        HostFrameBoundarySnapshot& snapshot) const noexcept {
        return next_frame ? next_frame(context, snapshot)
                          : HostFrameSourceStatus::source_error;
    }
};

struct HostFrameSessionResult {
    HostSessionStatus status{HostSessionStatus::not_requested};
    std::uint64_t observed_boundaries{};
    std::optional<FrameBoundaryObservation> first_visible{};
    bool first_visible_bmp_written{};
};

// OS backends own windows and device polling. The shared session driver below
// owns lifecycle and controller delivery, so it can be exercised headlessly.
class NativeHostPlatform {
public:
    virtual ~NativeHostPlatform() = default;
    [[nodiscard]] virtual bool open(std::uint16_t width, std::uint16_t height) = 0;
    [[nodiscard]] virtual bool present(const RgbaFrame& frame) = 0;
    [[nodiscard]] virtual HostPollStatus poll(HostInputSnapshot& input) = 0;
    virtual void close() noexcept = 0;
};

[[nodiscard]] HostSessionStatus run_native_host_session(
    NativeHostPlatform& platform,
    const RgbaFrame& frame,
    HostControllerSink controllers) noexcept;

// Drives a host window from genuine runtime-owned frame-boundary snapshots.
// This function never advances guest clocks, repeats a prior snapshot, or
// clears forced blank. A runtime that has not reached another boundary must
// report no_frame; a completed bounded run must report exhausted.
[[nodiscard]] HostFrameSessionResult run_native_host_frame_session(
    NativeHostPlatform& platform,
    HostFrameSource frames,
    HostControllerSink controllers,
    const std::filesystem::path& first_visible_bmp = {}) noexcept;

#ifdef _WIN32
[[nodiscard]] std::unique_ptr<NativeHostPlatform> make_win32_host_platform();
#endif

} // namespace kss
