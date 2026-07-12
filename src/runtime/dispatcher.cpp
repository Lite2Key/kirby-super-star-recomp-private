#include "kss/dispatcher.hpp"

namespace kss {

RegistrationStatus CheckedDispatcher::register_block(BlockKey key, BlockFunction function) {
    if (function == nullptr) {
        return RegistrationStatus::null_function;
    }
    const auto [unused, inserted] = blocks_.emplace(key, function);
    (void)unused;
    return inserted ? RegistrationStatus::registered : RegistrationStatus::duplicate_key;
}

DispatchStatus CheckedDispatcher::dispatch(CpuContext& cpu, Bus& bus, Scheduler& scheduler) const {
    if (cpu.stopped) {
        return DispatchStatus::cpu_stopped;
    }
    const auto found = blocks_.find(cpu.block_key());
    if (found == blocks_.end()) {
        return DispatchStatus::unknown_block;
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

} // namespace kss
