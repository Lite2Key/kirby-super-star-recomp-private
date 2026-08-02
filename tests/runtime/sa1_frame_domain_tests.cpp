#include "kss/deterministic_scheduler.hpp"
#include "kss/dual_bus.hpp"
#include "kss/generated_first_frame_blocks.hpp"
#include "kss/sa1_frame_domain.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

struct Fixture {
    std::vector<std::uint8_t> rom = std::vector<std::uint8_t>(4U * 1024U * 1024U);
    kss::RomBackedDualBus bus{rom};
    kss::DeterministicScheduler scheduler{};
    kss::CheckedDispatcher dispatcher{};
    kss::CpuContext cpu{};

    Fixture() {
        assert(kss::generated::register_first_frame_blocks(dispatcher));
        cpu.processor = kss::ProcessorId::sa1;
        cpu.pc = 0x8c58U;
        cpu.status = 0x07U;
        cpu.emulation = false;
    }
};

void test_two_poll_blocks_reach_exact_domain_boundary() {
    Fixture fixture;
    const auto result = kss::advance_sa1_poll_to(
        fixture.cpu, fixture.bus, fixture.scheduler, fixture.dispatcher,
        100U, 116U, 2U);
    assert(result.status == kss::Sa1PollAdvanceStatus::target_reached);
    assert(result.completed_blocks == 2U && result.completed_cycles == 8U);
    assert(result.ready_at == 116U && result.shortfall == 0U);
    assert(fixture.cpu.address() == 0x008c58U && fixture.cpu.cycles == 8U);
}

void test_target_inside_instruction_preserves_last_whole_boundary() {
    Fixture fixture;
    const auto result = kss::advance_sa1_poll_to(
        fixture.cpu, fixture.bus, fixture.scheduler, fixture.dispatcher,
        100U, 112U, 2U);
    assert(result.status == kss::Sa1PollAdvanceStatus::target_inside_instruction);
    assert(result.completed_blocks == 1U && result.completed_cycles == 5U);
    assert(result.ready_at == 110U && result.shortfall == 2U);
    assert(fixture.cpu.address() == 0x008c5bU && fixture.cpu.cycles == 5U);
}

void test_first_frame_target_exposes_four_master_clock_residual() {
    Fixture fixture;
    fixture.cpu.cycles = 76176U;
    const auto result = kss::advance_sa1_poll_to(
        fixture.cpu, fixture.bus, fixture.scheduler, fixture.dispatcher,
        152352U, 306900U, 20000U);
    assert(result.status == kss::Sa1PollAdvanceStatus::target_inside_instruction);
    assert(result.completed_blocks == 19318U && result.completed_cycles == 77272U);
    assert(result.ready_at == 306896U && result.shortfall == 4U);
    assert(fixture.cpu.address() == 0x008c58U && fixture.cpu.cycles == 153448U);
}

void test_negative_shared_word_reports_real_poll_release() {
    Fixture fixture;
    fixture.bus.sa1_iram()[0x0aU] = 0x00U;
    fixture.bus.sa1_iram()[0x0bU] = 0x80U;
    const auto result = kss::advance_sa1_poll_to(
        fixture.cpu, fixture.bus, fixture.scheduler, fixture.dispatcher,
        100U, 116U, 2U);
    assert(result.status == kss::Sa1PollAdvanceStatus::poll_released);
    assert(result.completed_blocks == 1U && result.ready_at == 110U);
    assert(fixture.cpu.address() == 0x008c5bU);
    assert(fixture.cpu.flag(kss::StatusFlag::negative));
    assert(!fixture.cpu.stopped);
}

void test_rejects_non_sa1_and_non_poll_state() {
    Fixture fixture;
    fixture.cpu.processor = kss::ProcessorId::snes_cpu;
    auto result = kss::advance_sa1_poll_to(
        fixture.cpu, fixture.bus, fixture.scheduler, fixture.dispatcher,
        100U, 116U, 2U);
    assert(result.status == kss::Sa1PollAdvanceStatus::invalid_cpu);

    fixture.cpu.processor = kss::ProcessorId::sa1;
    fixture.cpu.pc = 0x8c55U;
    result = kss::advance_sa1_poll_to(
        fixture.cpu, fixture.bus, fixture.scheduler, fixture.dispatcher,
        100U, 116U, 2U);
    assert(result.status == kss::Sa1PollAdvanceStatus::invalid_identity);
}

} // namespace

int main() {
    test_two_poll_blocks_reach_exact_domain_boundary();
    test_target_inside_instruction_preserves_last_whole_boundary();
    test_first_frame_target_exposes_four_master_clock_residual();
    test_negative_shared_word_reports_real_poll_release();
    test_rejects_non_sa1_and_non_poll_state();
    return 0;
}
