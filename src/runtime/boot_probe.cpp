#include "kss/boot_probe.hpp"

#include "kss/deterministic_scheduler.hpp"
#include "kss/dispatcher.hpp"
#include "kss/dual_bus.hpp"
#include "kss/generated_first_frame_blocks.hpp"
#include "kss/scpu_micro_access_recorder.hpp"
#ifdef KSS_USE_PRIVATE_GENERATED
#include "kss/generated_private_first_frame_blocks.hpp"
#endif

#include <algorithm>

namespace kss {

BootProbeResult run_boot_probe(
    std::span<const std::uint8_t> rom,
    BootProbeTimingEvidence timing,
    std::span<std::uint8_t> persistent_bwram,
    NativeHostPlatform* host) noexcept {
    BootProbeResult result{};
    RomBackedDualBus hardware_bus(rom);
    const auto save_buffer_valid = persistent_bwram.empty()
        || persistent_bwram.size() == kKssSaveRamSize;
    if (!save_buffer_valid) return result;
    if (!persistent_bwram.empty()) {
        std::copy(persistent_bwram.begin(), persistent_bwram.end(),
            hardware_bus.persistent_bwram().begin());
    }
    struct SaveRamExporter {
        RomBackedDualBus& bus;
        std::span<std::uint8_t> destination;
        ~SaveRamExporter() {
            if (!destination.empty()) {
                std::copy(bus.persistent_bwram().begin(), bus.persistent_bwram().end(),
                    destination.begin());
            }
        }
    } save_exporter{hardware_bus, persistent_bwram};
    ScpuMicroAccessRecorder bus(hardware_bus);
    DeterministicScheduler scheduler;
    CheckedDispatcher dispatcher;
#ifdef KSS_USE_PRIVATE_GENERATED
    if (!generated::register_private_first_frame_blocks(dispatcher)) {
#else
    if (!generated::register_first_frame_blocks(dispatcher)) {
#endif
        return result;
    }
    result.inventory_block_identities = dispatcher.registered_identities();
    dispatcher.set_execution_identity_sink(&result.executed_block_identities);
    MultiClockCoordinator clocks;
    const bool live_spc = !timing.spc_ipl.empty() && timing.spc_steps.empty();
    if (live_spc && !hardware_bus.provision_spc_ipl(timing.spc_ipl)) {
        result.status = BootProbeStatus::spc_provision_failed;
        return result;
    }

    result.scpu.processor = ProcessorId::snes_cpu;
    result.scpu.pc = 0x8004;
    result.scpu.status = 0x34;
    result.scpu.emulation = true;
    result.sa1.processor = ProcessorId::sa1;
    result.sa1.pc = 0x8bf4;
    result.sa1.status = 0x34;
    result.sa1.emulation = true;

    const auto scpu_wait = BlockKey::make(
        ProcessorId::snes_cpu, 0x00816d, false, false, false);
    result.scpu_setup = run_generated_until(
        result.scpu, bus, scheduler, dispatcher, scpu_wait, 160, 160);
    if (result.scpu_setup.status != GeneratedRunStatus::checkpoint_reached) {
        result.status = BootProbeStatus::scpu_setup_failed;
        return result;
    }

    // The reset trace proves that shared-I-RAM production is the causal event
    // which releases the S-CPU wait. Exact CPU interleaving/master clocks are
    // deliberately not inferred by this staged diagnostic.
    const auto sa1_poll = BlockKey::make(
        ProcessorId::sa1, 0x008c58, false, false, false);
    result.sa1_initialization = run_generated_until(
        result.sa1, bus, scheduler, dispatcher, sa1_poll, 12000);
    if (result.sa1_initialization.status != GeneratedRunStatus::checkpoint_reached) {
        result.status = BootProbeStatus::sa1_initialization_failed;
        return result;
    }

    // Execute the load and taken branch once, returning to the same identity.
    // The shared-I-RAM value is observed, never patched to force poll release.
    result.sa1_poll_observation = run_generated_until(
        result.sa1, bus, scheduler, dispatcher, sa1_poll, 2, 2);
    result.sa1_poll_value = hardware_bus.sa1_iram()[0x0aU];
    if (result.sa1_poll_observation.status != GeneratedRunStatus::checkpoint_reached) {
        result.status = BootProbeStatus::sa1_poll_observation_failed;
        return result;
    }

    std::size_t accounted_scpu_accesses = 0;
    bool live_spc_ok = true;
    const auto account_new_scpu_accesses = [&]() noexcept {
        const auto accesses = bus.timing_accesses();
        if (accesses.size() == accounted_scpu_accesses) return true;
        const auto advance = clocks.account_scpu_accesses(
            accesses.subspan(accounted_scpu_accesses));
        accounted_scpu_accesses = accesses.size();
        return advance.status == CoordinatorStatus::accepted;
    };
    const auto advance_spc_to = [&](MasterClock target) noexcept {
        // One first-frame synchronization never needs remotely this many SPC
        // instructions (the reset-to-frame reference is under four thousand).
        // Keep a strict fail-closed guard against a zero-cycle or stuck core.
        constexpr std::size_t instruction_limit = 16'384U;
        for (std::size_t count = 0;
             clocks.ready_at(ClockDomain::spc) < target && count < instruction_limit;
             ++count) {
            result.last_spc_step = hardware_bus.step_spc();
            if (result.last_spc_step->status != apu::SpcStepStatus::executed) {
                live_spc_ok = false;
                return false;
            }
            const auto advance = clocks.account_spc_cycles(
                result.last_spc_step->instruction_cycles);
            if (advance.status != CoordinatorStatus::accepted) {
                live_spc_ok = false;
                return false;
            }
            ++result.spc_steps_completed;
        }
        if (clocks.ready_at(ClockDomain::spc) < target) {
            live_spc_ok = false;
            return false;
        }
        return true;
    };
    const auto run_interleaved_until = [&](BlockKey checkpoint,
        std::size_t max_blocks, std::size_t minimum_blocks = 0U) noexcept {
        GeneratedRunResult run{};
        if (minimum_blocks == 0U && result.scpu.block_key() == checkpoint) {
            run.status = GeneratedRunStatus::checkpoint_reached;
            return run;
        }
        for (std::size_t step = 0; step < max_blocks; ++step) {
            if (result.scpu.stopped) {
                run.status = GeneratedRunStatus::cpu_stopped;
                return run;
            }
            const auto before = result.scpu.block_key();
            const auto dispatch = dispatcher.dispatch(result.scpu, bus, scheduler);
            if (dispatch == DispatchStatus::unknown_block) {
                run.status = GeneratedRunStatus::unknown_block;
                return run;
            }
            if (dispatch == DispatchStatus::cpu_stopped) {
                run.status = GeneratedRunStatus::cpu_stopped;
                return run;
            }
            if (result.scpu.stopped) {
                run.status = GeneratedRunStatus::generated_block_failed_closed;
                return run;
            }
            run.last_completed = before;
            ++run.completed_blocks;
            if (!account_new_scpu_accesses()
                || !advance_spc_to(clocks.ready_at(ClockDomain::scpu))) {
                run.status = GeneratedRunStatus::generated_block_failed_closed;
                return run;
            }
            if (run.completed_blocks >= minimum_blocks
                && result.scpu.block_key() == checkpoint) {
                run.status = GeneratedRunStatus::checkpoint_reached;
                return run;
            }
        }
        run.status = GeneratedRunStatus::step_limit;
        return run;
    };

    if (live_spc) {
        if (!account_new_scpu_accesses()) {
            result.status = BootProbeStatus::timing_debt;
            return result;
        }
        const auto sa1_timing = clocks.account_sa1_cycles(result.sa1.cycles);
        result.sa1_master_ready = sa1_timing.ready_at;
        if (sa1_timing.status != CoordinatorStatus::accepted
            || clocks.align_domain(ClockDomain::scpu, result.sa1_master_ready)
                != CoordinatorStatus::accepted) {
            result.status = BootProbeStatus::timing_debt;
            return result;
        }
        if (!advance_spc_to(result.sa1_master_ready)) {
            result.spc_registers = hardware_bus.spc_core()->registers();
            result.status = BootProbeStatus::spc_step_failed;
            return result;
        }
    }

    const auto current_frontier = BlockKey::make(
        ProcessorId::snes_cpu, 0x00d66e, false, true, false);
    result.scpu_frontier = live_spc
        ? run_interleaved_until(current_frontier, 4096)
        : run_generated_until(result.scpu, bus, scheduler, dispatcher,
            current_frontier, 4096);
    if (result.scpu_frontier.status != GeneratedRunStatus::checkpoint_reached) {
        result.status = BootProbeStatus::scpu_frontier_failed;
        return result;
    }

    // No-IPL mode proves one bounded wait loop. Runtime-IPL mode advances both
    // processors honestly until the IPL copies the observed $CC token to F4,
    // causing the S-CPU to branch into the upload body at $D648.
    const auto apu_acknowledgement_wait = BlockKey::make(ProcessorId::snes_cpu,
        live_spc ? 0x00d648U : 0x00d68eU, false, true, false);
    result.scpu_apu_wait_observation = live_spc
        ? run_interleaved_until(apu_acknowledgement_wait, 4096)
        : run_generated_until(result.scpu, bus, scheduler, dispatcher,
            apu_acknowledgement_wait, 20, 20);
    result.apu_port0_output = hardware_bus.apu_output_ports()[0];
    result.apu_cc_acknowledged = live_spc && result.apu_port0_output == 0xccU;
    if (result.scpu_apu_wait_observation.status
        != GeneratedRunStatus::checkpoint_reached) {
        if (live_spc && hardware_bus.spc_core()) {
            result.spc_registers = hardware_bus.spc_core()->registers();
        }
        result.status = BootProbeStatus::scpu_apu_wait_observation_failed;
        return result;
    }

    if (live_spc) {
        // Reach the second upload iteration as needed: the first iteration's
        // entry path skips four identities, while the genuine token/data
        // acknowledgement loop reaches them on the next pass.
        for (std::size_t step = 0;
             step < 4096U
                && result.executed_block_identities.size()
                    < result.inventory_block_identities.size();
             ++step) {
            const auto impossible_checkpoint = BlockKey::make(
                ProcessorId::snes_cpu, 0x00ffffU, false, true, false);
            const auto one = run_interleaved_until(impossible_checkpoint, 1U);
            result.scpu_upload_observation.last_completed = one.last_completed;
            result.scpu_upload_observation.completed_blocks += one.completed_blocks;
            if (one.status != GeneratedRunStatus::step_limit) {
                result.scpu_upload_observation.status = one.status;
                break;
            }
        }
        if (result.executed_block_identities.size()
            == result.inventory_block_identities.size()) {
            result.scpu_upload_observation.status = GeneratedRunStatus::checkpoint_reached;
        }
        if (!live_spc_ok) {
            result.spc_registers = hardware_bus.spc_core()->registers();
            result.status = BootProbeStatus::spc_step_failed;
            return result;
        }
    }

    result.frame = SnesFrameRenderer::render(hardware_bus.ppu_state());
    DomainAdvanceResult scpu_timing{CoordinatorStatus::accepted, 0,
        clocks.ready_at(ClockDomain::scpu)};
    if (!live_spc) {
        const auto sa1_timing = clocks.account_sa1_cycles(result.sa1.cycles);
        result.sa1_master_ready = sa1_timing.ready_at;
        if (sa1_timing.status != CoordinatorStatus::accepted) {
            result.timing_status = sa1_timing.status;
            result.status = BootProbeStatus::timing_debt;
            return result;
        }
        scpu_timing = clocks.account_scpu_accesses(bus.timing_accesses());
    }
    result.scpu_accesses_recorded = bus.accesses().size();
    result.scpu_master_ready = scpu_timing.ready_at;
    result.timing_status = scpu_timing.status;

    if (!timing.spc_steps.empty()) {
        if (timing.spc_ipl.empty() || !hardware_bus.provision_spc_ipl(timing.spc_ipl)) {
            result.status = BootProbeStatus::spc_provision_failed;
            return result;
        }
        for (const auto& point : timing.spc_steps) {
            if (point.at > kSnesFirstFrameMasterClock
                || clocks.schedule_spc_sample(point.at, point.phase)
                    != CoordinatorStatus::accepted) {
                result.timing_status = CoordinatorStatus::timing_debt;
                result.status = BootProbeStatus::timing_debt;
                return result;
            }
        }
    } else if (!live_spc && !timing.spc_ipl.empty()
        && !hardware_bus.provision_spc_ipl(timing.spc_ipl)) {
        result.status = BootProbeStatus::spc_provision_failed;
        return result;
    }
    for (const auto& point : timing.cpu_signals) {
        if (point.at > kSnesFirstFrameMasterClock) {
            result.timing_status = CoordinatorStatus::timing_debt;
            result.status = BootProbeStatus::timing_debt;
            return result;
        }
        CoordinatorStatus scheduled = CoordinatorStatus::timing_debt;
        switch (point.signal) {
        case CpuAsyncSignal::irq: scheduled = clocks.schedule_scpu_irq(point.at); break;
        case CpuAsyncSignal::nmi: scheduled = clocks.schedule_scpu_nmi(point.at); break;
        case CpuAsyncSignal::reset: scheduled = clocks.schedule_scpu_reset(point.at); break;
        }
        if (scheduled != CoordinatorStatus::accepted) {
            result.timing_status = scheduled;
            result.status = BootProbeStatus::timing_debt;
            return result;
        }
    }
    if (clocks.schedule_first_frame() != CoordinatorStatus::accepted) {
        result.status = BootProbeStatus::timing_debt;
        return result;
    }
    while (const auto event = clocks.pop_next()) {
        if (event->kind == CoordinatorEventKind::spc_phase) {
            result.last_spc_step = hardware_bus.step_spc();
            if (result.last_spc_step->status != apu::SpcStepStatus::executed) {
                result.spc_registers = hardware_bus.spc_core()->registers();
                result.master_now = clocks.master_now();
                result.status = BootProbeStatus::spc_step_failed;
                return result;
            }
            ++result.spc_steps_completed;
        } else if (event->kind == CoordinatorEventKind::scpu_irq
            || event->kind == CoordinatorEventKind::scpu_nmi
            || event->kind == CoordinatorEventKind::scpu_reset) {
            const auto signal = event->kind == CoordinatorEventKind::scpu_irq
                ? CpuAsyncSignal::irq
                : event->kind == CoordinatorEventKind::scpu_nmi
                    ? CpuAsyncSignal::nmi : CpuAsyncSignal::reset;
            result.last_cpu_signal = service_lifted_async_signal(result.scpu, bus, signal);
            ++result.cpu_signals_processed;
        } else if (event->kind == CoordinatorEventKind::first_frame) {
            result.first_frame_event_seen = true;
        }
    }
    if (hardware_bus.spc_core()) result.spc_registers = hardware_bus.spc_core()->registers();
    result.master_now = clocks.master_now();
    if (scpu_timing.status != CoordinatorStatus::accepted) {
        result.status = BootProbeStatus::timing_debt;
        return result;
    }
    for (const auto identity : result.inventory_block_identities) {
        if (std::find(result.executed_block_identities.begin(),
                result.executed_block_identities.end(), identity)
            == result.executed_block_identities.end()) {
            result.missing_block_identities.push_back(identity);
        }
    }
    if (host) {
        const HostControllerSink controllers{
            &hardware_bus,
            [](void* context, std::size_t port, std::uint16_t buttons) noexcept {
                static_cast<RomBackedDualBus*>(context)->set_controller_buttons(port, buttons);
            }};
        result.host_status = run_native_host_session(
            *host, result.frame.frame, controllers);
    }
    result.status = BootProbeStatus::expected_frontier_reached;
    return result;
}

} // namespace kss
