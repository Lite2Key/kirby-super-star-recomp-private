#pragma once

#include "kss/scheduler.hpp"
#include "kss/snes_frame_renderer.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace kss {

class RomBackedDualBus;

enum class FrameBoundaryStatus : std::uint8_t {
    forced_blank,
    black,
    visible,
    unsupported_visible_mode,
    unsupported_feature,
    non_monotonic_boundary,
};

struct FrameBoundaryObservation {
    FrameBoundaryStatus status{FrameBoundaryStatus::unsupported_visible_mode};
    FrameRenderStatus render_status{FrameRenderStatus::unsupported_visible_mode};
    std::uint64_t frame_ordinal{};
    MasterClock boundary_master{};
    std::uint32_t nonblack_pixels{};
    std::uint16_t first_nonblack_x{};
    std::uint16_t first_nonblack_y{};
    bool captured_first_visible{};
};

// Consumes immutable PPU snapshots at completed frame boundaries. The caller
// owns CPU/device scheduling and must call observe only after all same-clock
// bus commits and device samples have completed. This class deliberately does
// not invent guest progress or clear forced blank on the game's behalf.
class VisibleFrameCapture {
public:
    [[nodiscard]] FrameBoundaryObservation observe(
        MasterClock boundary_master, const PpuFunctionalState& state);
    [[nodiscard]] FrameBoundaryObservation observe(
        MasterClock boundary_master, const RomBackedDualBus& bus);

    [[nodiscard]] std::uint64_t observed_boundaries() const noexcept;
    [[nodiscard]] std::optional<MasterClock> last_boundary_master() const noexcept;
    [[nodiscard]] const std::optional<FrameBoundaryObservation>&
        first_visible_observation() const noexcept;
    [[nodiscard]] const RgbaFrame* first_visible_frame() const noexcept;
    [[nodiscard]] FrameWriteStatus write_first_visible_bmp(
        const std::filesystem::path& path) const;

private:
    std::uint64_t observed_boundaries_{};
    std::optional<MasterClock> last_boundary_master_{};
    std::optional<FrameBoundaryObservation> first_visible_observation_{};
    RgbaFrame first_visible_frame_{};
};

} // namespace kss
