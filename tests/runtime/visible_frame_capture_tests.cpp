#include "kss/dual_bus.hpp"
#include "kss/snes_timing.hpp"
#include "kss/visible_frame_capture.hpp"

#include <array>
#include <cassert>
#ifdef _WIN32
#include <crtdbg.h>
#endif
#include <filesystem>
#include <fstream>

namespace {

constexpr auto scpu = kss::ProcessorId::snes_cpu;

void write(kss::RomBackedDualBus& bus, std::uint32_t address, std::uint8_t value) {
    bus.write8(scpu, address, value, kss::BusAccessKind::data);
}

void test_live_ppu_boundaries_capture_only_the_first_nonblack_frame() {
    std::array<std::uint8_t, 1> rom{};
    kss::RomBackedDualBus bus(rom);
    kss::VisibleFrameCapture capture;
    constexpr auto step = kss::kSnesMasterClocksPerScanline;

    assert(capture.write_first_visible_bmp("missing-visible-frame.bmp")
        == kss::FrameWriteStatus::invalid_frame);

    write(bus, 0x002100, 0x8f); // Forced blank, brightness 15.
    auto observed = capture.observe(kss::kSnesFirstFrameMasterClock, bus);
    assert(observed.status == kss::FrameBoundaryStatus::forced_blank);
    assert(observed.frame_ordinal == 1 && observed.nonblack_pixels == 0);
    assert(!capture.first_visible_observation() && capture.first_visible_frame() == nullptr);

    write(bus, 0x002100, 0x0f); // Visible, but BGMODE is not implemented yet.
    observed = capture.observe(kss::kSnesFirstFrameMasterClock + step, bus);
    assert(observed.status == kss::FrameBoundaryStatus::unsupported_visible_mode);
    assert(observed.frame_ordinal == 2);

    write(bus, 0x002105, 0x01); // Mode 1.
    write(bus, 0x002100, 0x00); // Brightness zero: rendered but genuinely black.
    observed = capture.observe(kss::kSnesFirstFrameMasterClock + 2U * step, bus);
    assert(observed.status == kss::FrameBoundaryStatus::black);
    assert(observed.nonblack_pixels == 0 && observed.frame_ordinal == 3);

    write(bus, 0x002121, 0x00);
    write(bus, 0x002122, 0x1f); // Backdrop red.
    write(bus, 0x002122, 0x00);
    write(bus, 0x002100, 0x0f);
    observed = capture.observe(kss::kSnesFirstFrameMasterClock + 3U * step, bus);
    assert(observed.status == kss::FrameBoundaryStatus::visible);
    assert(observed.captured_first_visible && observed.frame_ordinal == 4);
    assert(observed.nonblack_pixels == 256U * 239U);
    assert(observed.first_nonblack_x == 0 && observed.first_nonblack_y == 0);

    const auto first = capture.first_visible_observation();
    assert(first && first->frame_ordinal == 4);
    assert(capture.first_visible_frame() && capture.first_visible_frame()->valid());
    assert(capture.first_visible_frame()->pixels[0] == 255U);

    // A later visible frame must never replace the first capture.
    write(bus, 0x002121, 0x00);
    write(bus, 0x002122, 0xe0); // Backdrop green ($03E0).
    write(bus, 0x002122, 0x03);
    observed = capture.observe(kss::kSnesFirstFrameMasterClock + 4U * step, bus);
    assert(observed.status == kss::FrameBoundaryStatus::visible);
    assert(!observed.captured_first_visible && observed.frame_ordinal == 5);
    assert(capture.first_visible_frame()->pixels[0] == 255U);

    // Same-time or earlier boundaries fail closed and do not advance ordinal.
    observed = capture.observe(kss::kSnesFirstFrameMasterClock + 4U * step, bus);
    assert(observed.status == kss::FrameBoundaryStatus::non_monotonic_boundary);
    assert(observed.frame_ordinal == 6);
    assert(capture.observed_boundaries() == 5);

    const auto output = std::filesystem::path{"visible-frame-capture-test.bmp"};
    assert(capture.write_first_visible_bmp(output) == kss::FrameWriteStatus::written);
    std::ifstream image(output, std::ios::binary | std::ios::ate);
    assert(image && image.tellg() == static_cast<std::streamoff>(54U + 256U * 239U * 4U));
    image.close();
    std::filesystem::remove(output);
}

} // namespace

int main() {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    test_live_ppu_boundaries_capture_only_the_first_nonblack_frame();
}
