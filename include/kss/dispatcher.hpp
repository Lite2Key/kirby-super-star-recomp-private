#pragma once

#include "kss/bus.hpp"
#include "kss/scheduler.hpp"

#include <cstddef>
#include <unordered_map>

namespace kss {

using BlockFunction = void (*)(CpuContext&, Bus&, Scheduler&);

enum class RegistrationStatus : std::uint8_t {
    registered,
    duplicate_key,
    null_function,
};

enum class DispatchStatus : std::uint8_t {
    executed,
    unknown_block,
    cpu_stopped,
};

class CheckedDispatcher {
public:
    [[nodiscard]] RegistrationStatus register_block(BlockKey key, BlockFunction function);
    [[nodiscard]] DispatchStatus dispatch(CpuContext& cpu, Bus& bus, Scheduler& scheduler) const;
    [[nodiscard]] bool contains(BlockKey key) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<BlockKey, BlockFunction, BlockKeyHash> blocks_;
};

} // namespace kss
