#pragma once

#include "kss/scheduler.hpp"

#include <vector>

namespace kss {

class DeterministicScheduler final : public Scheduler {
public:
    [[nodiscard]] MasterClock now() const noexcept override;
    void schedule(ScheduledEvent event) override;
    [[nodiscard]] std::optional<ScheduledEvent> pop_next_due() override;
    void advance_to(MasterClock target) override;

    [[nodiscard]] std::size_t pending() const noexcept;

private:
    MasterClock now_{};
    std::uint64_t next_sequence_{};
    std::vector<ScheduledEvent> events_;
};

} // namespace kss
