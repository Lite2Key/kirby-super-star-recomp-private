#include "kss/visible_frame_capture.hpp"

#include "kss/dual_bus.hpp"

#include <cstddef>
#include <limits>
#include <utility>

namespace kss {

FrameBoundaryObservation VisibleFrameCapture::observe(
    MasterClock boundary_master, const PpuFunctionalState& state) {
    FrameBoundaryObservation observation;
    observation.frame_ordinal = observed_boundaries_ + 1U;
    observation.boundary_master = boundary_master;

    if (last_boundary_master_ && boundary_master <= *last_boundary_master_) {
        observation.status = FrameBoundaryStatus::non_monotonic_boundary;
        return observation;
    }

    auto rendered = SnesFrameRenderer::render(state);
    observation.render_status = rendered.status;
    ++observed_boundaries_;
    last_boundary_master_ = boundary_master;

    if (rendered.status == FrameRenderStatus::unsupported_visible_mode) {
        observation.status = FrameBoundaryStatus::unsupported_visible_mode;
        return observation;
    }
    if (rendered.status == FrameRenderStatus::unsupported_feature) {
        observation.status = FrameBoundaryStatus::unsupported_feature;
        return observation;
    }
    if (!rendered.frame.valid()) {
        observation.status = FrameBoundaryStatus::unsupported_feature;
        return observation;
    }

    bool found_first = false;
    for (std::uint16_t y = 0; y < rendered.frame.height; ++y) {
        for (std::uint16_t x = 0; x < rendered.frame.width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * rendered.frame.width + x) * 4U;
            if (rendered.frame.pixels[offset] == 0U
                && rendered.frame.pixels[offset + 1U] == 0U
                && rendered.frame.pixels[offset + 2U] == 0U) {
                continue;
            }
            if (!found_first) {
                observation.first_nonblack_x = x;
                observation.first_nonblack_y = y;
                found_first = true;
            }
            if (observation.nonblack_pixels < std::numeric_limits<std::uint32_t>::max()) {
                ++observation.nonblack_pixels;
            }
        }
    }

    if (state.forced_blank) {
        observation.status = FrameBoundaryStatus::forced_blank;
        return observation;
    }
    if (observation.nonblack_pixels == 0U) {
        observation.status = FrameBoundaryStatus::black;
        return observation;
    }

    observation.status = FrameBoundaryStatus::visible;
    if (!first_visible_observation_) {
        observation.captured_first_visible = true;
        first_visible_observation_ = observation;
        first_visible_frame_ = std::move(rendered.frame);
    }
    return observation;
}

FrameBoundaryObservation VisibleFrameCapture::observe(
    MasterClock boundary_master, const RomBackedDualBus& bus) {
    return observe(boundary_master, bus.ppu_state());
}

std::uint64_t VisibleFrameCapture::observed_boundaries() const noexcept {
    return observed_boundaries_;
}

std::optional<MasterClock> VisibleFrameCapture::last_boundary_master() const noexcept {
    return last_boundary_master_;
}

const std::optional<FrameBoundaryObservation>&
VisibleFrameCapture::first_visible_observation() const noexcept {
    return first_visible_observation_;
}

const RgbaFrame* VisibleFrameCapture::first_visible_frame() const noexcept {
    return first_visible_observation_ ? &first_visible_frame_ : nullptr;
}

FrameWriteStatus VisibleFrameCapture::write_first_visible_bmp(
    const std::filesystem::path& path) const {
    if (!first_visible_observation_) return FrameWriteStatus::invalid_frame;
    return SnesFrameRenderer::write_bmp(first_visible_frame_, path);
}

} // namespace kss
