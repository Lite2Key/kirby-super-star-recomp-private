#include "kss/spc_exact_master.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

namespace {

using kss::ClockDomain;
using kss::CoordinatorStatus;
using kss::MultiClockCoordinator;
using kss::SpcExactAdvanceStatus;
using kss::apu::Spc700Core;

std::array<std::uint8_t, Spc700Core::kIplSize> synthetic_ipl(
    std::uint16_t vector = 0x0200) {
    std::array<std::uint8_t, Spc700Core::kIplSize> bytes{};
    bytes[62] = static_cast<std::uint8_t>(vector);
    bytes[63] = static_cast<std::uint8_t>(vector >> 8U);
    return bytes;
}

Spc700Core core_at(std::uint16_t pc = 0x0200) {
    Spc700Core core;
    const auto ipl = synthetic_ipl(pc);
    assert(core.load_ipl(ipl));
    assert(core.reset());
    return core;
}

void test_first_frame_target_is_an_explicit_in_flight_phase() {
    auto core = core_at();
    core.ram()[0x0200] = 0x2f; // BRA -2, four architectural cycles.
    core.ram()[0x0201] = 0xfe;
    core.registers().cycles = 14'632U;
    core.ram()[0x0042] = 0xa5U;
    core.cpu_write_port(2, 0x77U);

    MultiClockCoordinator clocks;
    const auto baseline = clocks.account_spc_cycles(14'632U);
    assert(baseline.status == CoordinatorStatus::accepted);
    assert(baseline.ready_at == 306'890U);
    assert(clocks.spc_clock_remainder() == 83'904U);

    const auto registers_before = core.registers();
    std::array<std::uint8_t, Spc700Core::kRamSize> ram_before{};
    std::copy(core.ram().begin(), core.ram().end(), ram_before.begin());
    std::array<std::uint8_t, 4> input_before{};
    std::copy(core.cpu_input_ports().begin(), core.cpu_input_ports().end(),
        input_before.begin());

    const auto result = kss::advance_spc_to_exact_master(
        core, clocks, kss::kSnesFirstFrameMasterClock);
    assert(result.status == SpcExactAdvanceStatus::target_inside_instruction);
    assert(result.completed_instructions == 0U);
    assert(result.architectural_ready_at == 306'890U);
    assert(result.observed_at == 306'900U);
    assert(result.in_flight);
    const auto& phase = *result.in_flight;
    assert(phase.architectural_state_is_entry);
    assert(phase.architectural_entry.pc == 0x0200U);
    assert(phase.architectural_entry.cycles == 14'632U);
    assert(phase.pending_instruction.opcode == 0x2fU);
    assert(phase.pending_instruction.instruction_cycles == 4U);
    assert(phase.instruction_start == 306'890U);
    assert(phase.observed_at == 306'900U);
    assert(phase.instruction_completion == 306'973U);
    assert(phase.elapsed_master_clocks == 10U);
    assert(phase.remaining_master_clocks == 73U);
    assert(phase.clock_remainder_at_entry == 83'904U);
    assert(phase.clock_remainder_at_completion == 1'000'992U);

    // Suspension neither executes nor rewinds the real core and does not lie
    // by moving its architectural cursor to the frame timestamp.
    assert(core.registers().pc == registers_before.pc);
    assert(core.registers().cycles == registers_before.cycles);
    assert(std::equal(core.ram().begin(), core.ram().end(), ram_before.begin()));
    assert(std::equal(core.cpu_input_ports().begin(), core.cpu_input_ports().end(),
        input_before.begin()));
    assert(clocks.ready_at(ClockDomain::spc) == 306'890U);
    assert(clocks.spc_clock_remainder() == 83'904U);
}

void test_whole_instructions_commit_only_through_target() {
    auto core = core_at();
    core.ram()[0x0200] = 0x00; // NOP: 2 cycles
    core.ram()[0x0201] = 0x00; // NOP: 2 cycles
    core.ram()[0x0202] = 0x2f; // BRA -2: 4 cycles
    core.ram()[0x0203] = 0xfe;
    MultiClockCoordinator clocks;

    const auto two_nops = clocks.preview_spc_cycles(4U);
    assert(two_nops.status == CoordinatorStatus::accepted);
    const auto result = kss::advance_spc_to_exact_master(
        core, clocks, two_nops.ready_at + 1U);
    assert(result.status == SpcExactAdvanceStatus::target_inside_instruction);
    assert(result.completed_instructions == 2U);
    assert(result.completed_architectural_cycles == 4U);
    assert(result.architectural_ready_at == two_nops.ready_at);
    assert(core.registers().pc == 0x0202U && core.registers().cycles == 4U);
    assert(result.in_flight && result.in_flight->pending_instruction.opcode == 0x2fU);
    assert(clocks.ready_at(ClockDomain::spc) == two_nops.ready_at);

    const auto past = kss::advance_spc_to_exact_master(
        core, clocks, two_nops.ready_at - 1U);
    assert(past.status == SpcExactAdvanceStatus::target_before_cursor);
    assert(core.registers().pc == 0x0202U && core.registers().cycles == 4U);
}

void test_exact_instruction_boundary_commits_architectural_state() {
    auto core = core_at();
    core.ram()[0x0200] = 0x00;
    MultiClockCoordinator clocks;
    const auto boundary = clocks.preview_spc_cycles(2U);
    const auto result = kss::advance_spc_to_exact_master(
        core, clocks, boundary.ready_at);
    assert(result.status == SpcExactAdvanceStatus::target_reached);
    assert(!result.in_flight && result.completed_instructions == 1U);
    assert(core.registers().pc == 0x0201U && core.registers().cycles == 2U);
    assert(clocks.ready_at(ClockDomain::spc) == boundary.ready_at);
    assert(clocks.spc_clock_remainder() == boundary.completion_remainder);
}

void count_port_write(void* context, std::uint64_t,
    std::uint8_t, std::uint8_t) noexcept {
    ++*static_cast<unsigned*>(context);
}

void test_preview_detaches_nonarchitectural_observers() {
    auto core = core_at();
    // MOV $F4,#$CC writes an output port, but target master 1 is inside the
    // five-cycle instruction and therefore must not publish that write.
    core.ram()[0x0200] = 0x8f;
    core.ram()[0x0201] = 0xcc;
    core.ram()[0x0202] = 0xf4;
    unsigned writes = 0;
    core.set_port_write_sink(&writes, count_port_write);
    MultiClockCoordinator clocks;
    const auto result = kss::advance_spc_to_exact_master(core, clocks, 1U);
    assert(result.status == SpcExactAdvanceStatus::target_inside_instruction);
    assert(writes == 0U && core.cpu_read_port(0) == 0U);
    assert(core.registers().pc == 0x0200U && core.registers().cycles == 0U);
    assert(clocks.ready_at(ClockDomain::spc) == 0U);
}

} // namespace

int main() {
    test_first_frame_target_is_an_explicit_in_flight_phase();
    test_whole_instructions_commit_only_through_target();
    test_exact_instruction_boundary_commits_architectural_state();
    test_preview_detaches_nonarchitectural_observers();
}
