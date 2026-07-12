#pragma once

#include "kss/cpu.hpp"

#include <cstdint>
#include <optional>

namespace kss {

using MasterClock = std::uint64_t;

enum class EventKind : std::uint8_t {
    cpu_resume,
    interrupt,
    dma,
    hdma,
    scanline,
    frame,
    apu_port,
    custom,
};

struct ScheduledEvent {
    MasterClock at{};
    std::uint64_t sequence{};
    EventKind kind{EventKind::custom};
    ProcessorId processor{ProcessorId::snes_cpu};
    std::uint32_t payload{};

    friend constexpr bool operator==(const ScheduledEvent&, const ScheduledEvent&) = default;
};

class Scheduler {
public:
    virtual ~Scheduler() = default;

    [[nodiscard]] virtual MasterClock now() const noexcept = 0;
    virtual void schedule(ScheduledEvent event) = 0;
    [[nodiscard]] virtual std::optional<ScheduledEvent> pop_next_due() = 0;
    virtual void advance_to(MasterClock target) = 0;
};

} // namespace kss
