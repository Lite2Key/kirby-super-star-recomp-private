#pragma once

#include "kss/cpu.hpp"

#include <array>
#include <cstdint>

namespace kss {

struct Sa1MessageLatchSummary {
    static constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    std::uint32_t sa1_writes{};
    std::uint32_t scpu_reads{};
    std::uint64_t sequence_digest{kFnvOffsetBasis};

    friend constexpr bool operator==(
        const Sa1MessageLatchSummary&, const Sa1MessageLatchSummary&) = default;
};

struct Sa1ControlState {
    bool reset{true};
    bool wait{};
    bool irq_requested{};
    bool nmi_requested{};
    std::uint8_t sa1_message{};
    std::uint8_t snes_message{};
    std::uint16_t reset_vector{};
    std::uint8_t snes_iram_write_mask{};
    std::uint8_t sa1_iram_write_mask{};
};

// Functional SA-1 control/status subset observed before the first EndFrame.
// This intentionally contains no bus-arbitration or master-clock model.
class Sa1RegisterFile {
public:
    [[nodiscard]] std::uint8_t read(ProcessorId processor, std::uint16_t address,
        std::uint8_t open_bus) const noexcept;
    void write(ProcessorId processor, std::uint16_t address, std::uint8_t value) noexcept;
    [[nodiscard]] bool iram_write_enabled(ProcessorId processor, std::uint16_t offset) const noexcept;
    [[nodiscard]] const Sa1ControlState& state() const noexcept;
    [[nodiscard]] Sa1MessageLatchSummary message_latch_summary() const noexcept;

private:
    void record_message_event(bool sa1_write, std::uint8_t value) const noexcept;

    std::array<std::uint8_t, 0x200> registers_{};
    Sa1ControlState state_{};
    mutable Sa1MessageLatchSummary message_latch_summary_{};
};

} // namespace kss
