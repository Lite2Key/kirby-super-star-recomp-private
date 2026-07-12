#include "kss/deterministic_scheduler.hpp"

#include <algorithm>
#include <stdexcept>

namespace kss {
namespace {

bool event_before(const ScheduledEvent& left, const ScheduledEvent& right) noexcept {
    return left.at < right.at || (left.at == right.at && left.sequence < right.sequence);
}

} // namespace

MasterClock DeterministicScheduler::now() const noexcept {
    return now_;
}

void DeterministicScheduler::schedule(ScheduledEvent event) {
    if (event.at < now_) {
        throw std::invalid_argument("cannot schedule an event in the past");
    }
    event.sequence = next_sequence_++;
    const auto position = std::upper_bound(events_.begin(), events_.end(), event, event_before);
    events_.insert(position, event);
}

std::optional<ScheduledEvent> DeterministicScheduler::pop_next_due() {
    if (events_.empty() || events_.front().at > now_) {
        return std::nullopt;
    }
    const auto event = events_.front();
    events_.erase(events_.begin());
    return event;
}

void DeterministicScheduler::advance_to(MasterClock target) {
    if (target < now_) {
        throw std::invalid_argument("scheduler time cannot move backwards");
    }
    now_ = target;
}

std::size_t DeterministicScheduler::pending() const noexcept {
    return events_.size();
}

} // namespace kss
