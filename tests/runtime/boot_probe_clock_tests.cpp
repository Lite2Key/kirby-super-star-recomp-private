#include "kss/boot_probe.hpp"
#include "kss/rom_validation.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace {

std::array<std::uint8_t, kss::apu::Spc700Core::kIplSize> ipl_with(
    std::initializer_list<std::uint8_t> program) {
    std::array<std::uint8_t, kss::apu::Spc700Core::kIplSize> result{};
    std::size_t index = 0;
    for (const auto byte : program) result[index++] = byte;
    result[62] = 0xc0;
    result[63] = 0xff;
    return result;
}

std::vector<std::uint8_t> synthetic_rom() {
    return std::vector<std::uint8_t>(kss::kExpectedRomSize);
}

void test_default_probe_uses_local_complete_scpu_stream() {
    const auto result = kss::run_boot_probe(synthetic_rom());
    assert(result.status == kss::BootProbeStatus::expected_frontier_reached);
    assert(result.timing_status == kss::CoordinatorStatus::accepted);
    assert(result.scpu_setup.completed_blocks == 160U);
    assert(result.sa1_initialization.completed_blocks == 10018U);
    assert(result.sa1_poll_observation.status
        == kss::GeneratedRunStatus::checkpoint_reached);
    assert(result.sa1_poll_observation.completed_blocks == 2U);
    assert(result.scpu_frontier.completed_blocks == 18U);
    assert(result.scpu_apu_wait_observation.status
        == kss::GeneratedRunStatus::checkpoint_reached);
    assert(result.scpu_apu_wait_observation.completed_blocks == 20U);
    assert(result.scpu.address() == 0x00d68eU && result.sa1.address() == 0x008c58U);
    assert(result.sa1_poll_value == 0x00U && result.apu_port0_output == 0xaaU);
    assert(result.scpu_master_ready > 0U && result.scpu_accesses_recorded > 0U);
    assert(result.sa1_master_ready == result.sa1.cycles * 2U);
    assert(result.first_frame_event_seen && result.master_now == 306900U);
    assert(result.spc_steps_completed == 0U && !result.spc_registers);
    assert(result.cpu_signals_processed == 0U && !result.last_cpu_signal);
    assert(result.inventory_block_identities.size() == 254U);
    assert(result.executed_block_identities.size() == 236U);
    assert(result.missing_block_identities.size() == 18U);
    assert(std::count_if(result.executed_block_identities.begin(),
        result.executed_block_identities.end(), [](const auto& identity) {
            return identity.processor == kss::ProcessorId::snes_cpu;
        }) == 196);
    assert(std::count_if(result.executed_block_identities.begin(),
        result.executed_block_identities.end(), [](const auto& identity) {
            return identity.processor == kss::ProcessorId::sa1;
        }) == 40);
    constexpr std::array expected_missing{
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d648, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d64a, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d64b, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d64c, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d64e, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d650, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d651, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d653, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d654, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d655, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d658, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d65a, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d65b, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d65d, false, false, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d660, false, false, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d662, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d663, false, true, false),
        kss::BlockKey::make(kss::ProcessorId::snes_cpu, 0x00d693, false, true, false),
    };
    assert(std::equal(result.missing_block_identities.begin(),
        result.missing_block_identities.end(), expected_missing.begin(), expected_missing.end()));
}

void test_explicit_access_evidence_and_spc_phase_drive_only_requested_step() {
    const auto rom = synthetic_rom();
    const auto ipl = ipl_with({0xe8, 0x5a}); // one supported MOV A,#imm
    constexpr std::array steps{kss::BootProbeSpcStep{100, 7}};
    const kss::BootProbeTimingEvidence evidence{ipl, steps};
    const auto result = kss::run_boot_probe(rom, evidence);
    assert(result.status == kss::BootProbeStatus::expected_frontier_reached);
    assert(result.timing_status == kss::CoordinatorStatus::accepted);
    assert(result.scpu_master_ready > 0U && result.scpu_accesses_recorded > 0U);
    assert(result.sa1_master_ready == result.sa1.cycles * 2U);
    assert(result.spc_steps_completed == 1U && result.last_spc_step);
    assert(result.last_spc_step->status == kss::apu::SpcStepStatus::executed);
    assert(result.spc_registers && result.spc_registers->pc == 0xffc2);
    assert(result.spc_registers->a == 0x5a && result.spc_registers->cycles == 2U);
    assert(result.first_frame_event_seen && result.master_now == 306900U);
}

void test_runtime_ipl_clocks_real_port_ack_and_reaches_generated_upload() {
    // Synthetic guest code: advertise AA/BB, wait for the CPU's CC token,
    // publish it through the SPC-owned F4 output latch, then acknowledge later
    // upload tokens by mirroring the SPC input latch from executed SPC code.
    const auto ipl = ipl_with({
        0xe8, 0xaa,       // MOV A,#$AA
        0xc4, 0xf4,       // MOV $F4,A
        0xe8, 0xbb,       // MOV A,#$BB
        0xc4, 0xf5,       // MOV $F5,A
        0xe4, 0xf4,       // wait: MOV A,$F4
        0x68, 0xcc,       // CMP A,#$CC
        0xd0, 0xfa,       // BNE wait
        0xc4, 0xf4,       // MOV $F4,A (real CC acknowledgement)
        0xe4, 0xf4,       // mirror: MOV A,$F4
        0xc4, 0xf4,       // MOV $F4,A
        0x2f, 0xfa,       // BRA mirror
    });
    const kss::BootProbeTimingEvidence evidence{ipl};
    const auto result = kss::run_boot_probe(synthetic_rom(), evidence);
    // The zero-filled synthetic cartridge does not provide KSS's upload
    // pointer/flags, so execution honestly takes the unregistered $D695
    // fallthrough after acknowledging CC. This test proves the cross-CPU port
    // handshake without pretending synthetic cartridge data proves the KSS
    // upload route.
    assert(result.status == kss::BootProbeStatus::scpu_apu_wait_observation_failed);
    assert(result.apu_cc_acknowledged && result.apu_port0_output == 0xccU);
    assert(result.scpu_apu_wait_observation.status
        == kss::GeneratedRunStatus::generated_block_failed_closed);
    assert(result.scpu.address() == 0x00d695U);
    assert(result.spc_steps_completed > 0U && result.spc_registers);
    assert(result.executed_block_identities.size() == 237U);
}

void test_spc_requires_runtime_ipl_and_fails_closed() {
    const auto rom = synthetic_rom();
    constexpr std::array steps{kss::BootProbeSpcStep{50, 1}};
    const kss::BootProbeTimingEvidence missing_ipl{{}, steps};
    assert(kss::run_boot_probe(rom, missing_ipl).status
        == kss::BootProbeStatus::spc_provision_failed);

    // MOV $00F0,A requests TEST RAM write protection with reset A=0. Opcode,
    // timer, and DSP-register coverage are implemented, so this invalid TEST
    // mode remains an explicit fail-closed boundary.
    const auto unsupported = ipl_with({0xc5, 0xf0, 0x00});
    const kss::BootProbeTimingEvidence bad_step{unsupported, steps};
    const auto result = kss::run_boot_probe(rom, bad_step);
    if (result.status != kss::BootProbeStatus::spc_step_failed) {
        std::fprintf(stderr, "unexpected boot status=%u timing=%u steps=%zu\n",
            static_cast<unsigned>(result.status), static_cast<unsigned>(result.timing_status),
            result.spc_steps_completed);
    }
    assert(result.status == kss::BootProbeStatus::spc_step_failed);
    assert(result.spc_steps_completed == 0U && result.last_spc_step);
    assert(result.last_spc_step->status == kss::apu::SpcStepStatus::unsupported_io);
    assert(result.spc_registers && result.spc_registers->pc == 0xffc3);
    assert(result.spc_registers->cycles == 0U);
}

void test_post_frame_spc_phase_is_timing_debt_without_execution() {
    const auto rom = synthetic_rom();
    const auto ipl = ipl_with({0xe8, 0x5a});
    constexpr std::array steps{kss::BootProbeSpcStep{306901, 0}};
    const kss::BootProbeTimingEvidence evidence{ipl, steps};
    const auto result = kss::run_boot_probe(rom, evidence);
    assert(result.status == kss::BootProbeStatus::timing_debt);
    assert(result.spc_steps_completed == 0U);
}

void test_opt_in_cpu_signal_is_applied_before_same_timestamp_frame() {
    const auto rom = synthetic_rom();
    constexpr std::array signals{
        kss::BootProbeCpuSignal{kss::kSnesFirstFrameMasterClock, kss::CpuAsyncSignal::nmi},
    };
    const kss::BootProbeTimingEvidence evidence{{}, {}, signals};
    const auto result = kss::run_boot_probe(rom, evidence);
    assert(result.status == kss::BootProbeStatus::expected_frontier_reached);
    assert(result.cpu_signals_processed == 1U && result.last_cpu_signal);
    assert(result.last_cpu_signal->status == kss::CpuAsyncStatus::serviced);
    assert(result.last_cpu_signal->entry_cycles == 8U); // frontier CPU is native
    assert(result.scpu.address() == 0 && result.scpu.flag(kss::StatusFlag::irq_disable));
    assert(result.first_frame_event_seen && result.master_now == kss::kSnesFirstFrameMasterClock);
}

void test_post_frame_cpu_signal_is_timing_debt_without_execution() {
    const auto rom = synthetic_rom();
    constexpr std::array signals{
        kss::BootProbeCpuSignal{kss::kSnesFirstFrameMasterClock + 1U,
            kss::CpuAsyncSignal::reset},
    };
    const kss::BootProbeTimingEvidence evidence{{}, {}, signals};
    const auto result = kss::run_boot_probe(rom, evidence);
    assert(result.status == kss::BootProbeStatus::timing_debt);
    assert(result.cpu_signals_processed == 0U && !result.last_cpu_signal);
}

} // namespace

int main() {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    test_default_probe_uses_local_complete_scpu_stream();
    test_explicit_access_evidence_and_spc_phase_drive_only_requested_step();
    test_runtime_ipl_clocks_real_port_ack_and_reaches_generated_upload();
    test_spc_requires_runtime_ipl_and_fails_closed();
    test_post_frame_spc_phase_is_timing_debt_without_execution();
    test_opt_in_cpu_signal_is_applied_before_same_timestamp_frame();
    test_post_frame_cpu_signal_is_timing_debt_without_execution();
}
