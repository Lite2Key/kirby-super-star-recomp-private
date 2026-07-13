#include "kss/dispatcher.hpp"

#include <algorithm>

namespace kss {

RegistrationStatus CheckedDispatcher::register_block(BlockKey key, BlockFunction function) {
    if (function == nullptr) {
        return RegistrationStatus::null_function;
    }
    const auto [unused, inserted] = blocks_.emplace(key, function);
    (void)unused;
    if (inserted) registration_order_.push_back(key);
    return inserted ? RegistrationStatus::registered : RegistrationStatus::duplicate_key;
}

DispatchStatus CheckedDispatcher::dispatch(CpuContext& cpu, Bus& bus, Scheduler& scheduler) const {
    if (cpu.stopped) {
        return DispatchStatus::cpu_stopped;
    }
    const auto identity = cpu.block_key();
    const auto found = blocks_.find(identity);
    if (found == blocks_.end()) {
        return DispatchStatus::unknown_block;
    }
    if (execution_identity_sink_
        && std::find(execution_identity_sink_->begin(), execution_identity_sink_->end(), identity)
            == execution_identity_sink_->end()) {
        execution_identity_sink_->push_back(identity);
    }
    found->second(cpu, bus, scheduler);
    return DispatchStatus::executed;
}

bool CheckedDispatcher::contains(BlockKey key) const noexcept {
    return blocks_.contains(key);
}

std::size_t CheckedDispatcher::size() const noexcept {
    return blocks_.size();
}

const std::vector<BlockKey>& CheckedDispatcher::registered_identities() const noexcept {
    return registration_order_;
}

void CheckedDispatcher::set_execution_identity_sink(
    std::vector<BlockKey>* sink) const noexcept {
    execution_identity_sink_ = sink;
}

} // namespace kss
