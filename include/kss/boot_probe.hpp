#pragma once

#include "kss/cpu.hpp"
#include "kss/generated_block_runner.hpp"
#include "kss/lifted_execution.hpp"
#include "kss/multi_clock_coordinator.hpp"
#include "kss/native_host.hpp"
#include "kss/runtime_event_chain.hpp"
#include "kss/sa1_frame_domain.hpp"
#include "kss/scpu_access_boundary.hpp"
#include "kss/sa1_registers.hpp"
#include "kss/snes_frame_renderer.hpp"
#include "kss/spc700.hpp"
#include "kss/spc_exact_master.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace kss {

enum class BootProbeStatus : std::uint8_t {
    scpu_setup_failed,
    sa1_initialization_failed,
    sa1_poll_observation_failed,
    scpu_frontier_failed,
    scpu_apu_wait_observation_failed,
    timing_debt,
    spc_provision_failed,
    spc_step_failed,
    expected_frontier_reached,
};

struct BootProbeSpcStep {
    MasterClock at{};
    std::uint32_t phase{};
};

struct BootProbeCpuSignal {
    MasterClock at{};
    CpuAsyncSignal signal{CpuAsyncSignal::irq};
};

struct BootProbeTimingEvidence {
    // IPL bytes remain runtime-only. SPC steps require both a valid IPL and
    // explicit coordinator phase stamps at or before the first frame.
    std::span<const std::uint8_t> spc_ipl{};
    std::span<const BootProbeSpcStep> spc_steps{};
    std::span<const BootProbeCpuSignal> cpu_signals{};
    // Optional memory-only event sink. Captured values remain owned by the
    // caller; BootProbeResult exposes only a count/digest summary.
    RuntimeEventChainRecorder* event_chain{};
    // Development-only route continuation. When enabled without an event
    // recorder, the live SPC model may advance past the first endFrame while
    // the bounded generated route continues. Event-chain probes keep the
    // first-frame clamp regardless of this flag so post-boundary records
    // cannot be mistaken for first-frame evidence.
    bool continue_route_after_first_frame{};
    // Finite development-only CPU/SA-1 continuation budget after the first
    // frame. Public/default probes keep this small; private route probes may
    // raise it to cross a measured upload phase without permitting an
    // unbounded loop.
    std::size_t post_frame_route_block_budget{4'096U};
};

enum class BootProbeEventChainStatus : std::uint8_t {
    not_requested,
    instruction_retirement_stream,
    record_rejected,
};

struct BootProbeResult {
    BootProbeStatus status{BootProbeStatus::scpu_setup_failed};
    GeneratedRunResult scpu_setup{};
    GeneratedRunResult sa1_initialization{};
    // S-CPU work performed after it first reaches the shared-I-RAM wait while
    // SA-1 reset code is still running.  The two domains are selected by their
    // next ready master-clock cursor; this is separate from the later SPC
    // upload interleave.
    GeneratedRunResult scpu_sa1_interleave{};
    // One complete trip around the hardware-gated SA-1 $300A poll. Reaching
    // the same checkpoint after two blocks proves the wait without inventing
    // the external event that releases it.
    GeneratedRunResult sa1_poll_observation{};
    // Whole-instruction continuation of the same $8C58/$8C5B poll through
    // the first-frame interval. The result retains the exact final SA-1
    // boundary and any residual when endFrame falls inside an instruction.
    Sa1PollAdvanceResult sa1_frame_observation{};
    GeneratedRunResult scpu_frontier{};
    // The upload setup plus one complete $2140 acknowledgement comparison.
    // Without a runtime IPL it returns to $D68E while the modeled output latch
    // remains $AA. With a runtime IPL it reaches $D648 through the real $CC
    // acknowledgement.
    GeneratedRunResult scpu_apu_wait_observation{};
    // Runtime-IPL mode continues the real token/data protocol until every
    // generated first-frame identity has been observed or a hard boundary is
    // reached.
    GeneratedRunResult scpu_upload_observation{};
    // Runtime-IPL mode keeps dispatching generated S-CPU blocks and stepping
    // the real SPC core until both local clocks cover the first-frame master
    // boundary. This is distinct from merely enqueueing the frame event.
    GeneratedRunResult scpu_frame_observation{};
    // Development-only route continuation after the first-frame boundary.
    // This is populated only by the explicit route-only mode and remains a
    // bounded diagnostic when the live upload handshake has no next edge.
    GeneratedRunResult post_frame_route_observation{};
    // Exact value-free observation at the first endFrame master clock. This
    // retains the preceding committed CPU context while exposing an in-flight
    // fetch/access when the boundary falls inside a generated block.
    std::optional<ScpuAccessBoundary> scpu_first_frame_boundary{};
    // Side-effect-free SPC preview at the same exact master clock. The live
    // core subsequently continues through its whole instruction as normal.
    std::optional<SpcExactAdvanceResult> spc_first_frame_boundary{};
    CpuContext scpu{};
    CpuContext sa1{};
    // Runtime-only PPU snapshot at the exact first-frame boundary. This is
    // intentionally captured before any opt-in route continuation so the
    // native "first-frame" surface cannot silently become a later endpoint.
    std::optional<PpuFunctionalState> first_frame_ppu_state{};
    FrameRenderResult first_frame{};
    // Runtime-only final PPU snapshot for bounded diagnostics. This is kept
    // separate from the first-frame render so a route probe can distinguish
    // missing video payload/state from an unsupported visible mode without
    // serializing VRAM, CGRAM, OAM, or private trace data.
    std::optional<PpuFunctionalState> ppu_state{};
    FrameRenderResult frame{};
    CoordinatorStatus timing_status{CoordinatorStatus::timing_debt};
    MasterClock scpu_master_ready{};
    std::size_t scpu_accesses_recorded{};
    MasterClock sa1_master_ready{};
    MasterClock sa1_frame_observation_start{};
    std::size_t sa1_frame_sync_points{};
    MasterClock sa1_last_sync_target{};
    MasterClock sa1_release_master{};
    MasterClock sa1_master_at_first_completion{};
    MasterClock scpu_master_at_sa1_first_dispatch{};
    MasterClock scpu_master_at_sa1_checkpoint{};
    std::size_t reset_domain_switches{};
    bool reset_domains_interleaved{};
    MasterClock spc_master_ready{};
    MasterClock master_now{};
    bool live_domains_reached_first_frame{};
    bool first_frame_event_seen{};
    std::size_t spc_steps_completed{};
    std::optional<apu::SpcStepResult> last_spc_step{};
    std::optional<apu::Spc700Registers> spc_registers{};
    std::size_t cpu_signals_processed{};
    std::optional<CpuAsyncResult> last_cpu_signal{};
    std::uint8_t sa1_poll_value{};
    // Final SA-1 $2209 message-latch state at the bounded route endpoint.
    // This is populated after first-frame/route execution; early fail-closed
    // returns leave it disengaged so callers do not confuse an unobserved
    // latch with a measured zero.
    std::optional<std::uint8_t> sa1_snes_message_latch{};
    // Value-free causal summary for SA-1 $2209 writes and S-CPU $2300 reads.
    // The event stream remains private; only counts and an order digest leave
    // the runtime boundary.
    std::optional<Sa1MessageLatchSummary> sa1_message_latch_summary{};
    // Route-only timing anchors for completion of the first two S-CPU $2300
    // polls after the first-frame boundary. These retain only master-clock
    // positions, making entry drift and steady-state poll cadence separately
    // measurable without exposing the private event stream.
    // The SA-1 release anchor below is a scheduler edge, not a bus callback;
    // it deliberately avoids implying false `$3010` write precision.
    std::optional<MasterClock> route_sa1_poll_release_master{};
    std::optional<MasterClock> route_first_message_poll_master{};
    std::optional<MasterClock> route_second_message_poll_master{};
    MasterClock route_message_poll_cadence_master{};
    std::uint8_t apu_port0_output{};
    bool apu_cc_acknowledged{};
    BootProbeEventChainStatus event_chain_status{
        BootProbeEventChainStatus::not_requested};
    std::optional<RuntimeEventChainSummary> event_chain_summary{};
    // Sanitized identity-only coverage. No instruction or ROM bytes are
    // captured by this path.
    std::vector<BlockKey> inventory_block_identities{};
    std::vector<BlockKey> executed_block_identities{};
    std::vector<BlockKey> missing_block_identities{};
    HostSessionStatus host_status{HostSessionStatus::not_requested};
};

// Execute the causal reset synchronization proven by the private trace:
// S-CPU hardware setup releases/configures SA-1, SA-1 fills shared I-RAM,
// then S-CPU leaves its shared-memory wait and runs to the current explicit
// semantic frontier. This is a development checkpoint, not a frame claim.
[[nodiscard]] BootProbeResult run_boot_probe(
    std::span<const std::uint8_t> rom,
    BootProbeTimingEvidence timing = {},
    std::span<std::uint8_t> persistent_bwram = {},
    NativeHostPlatform* host = nullptr) noexcept;

} // namespace kss
