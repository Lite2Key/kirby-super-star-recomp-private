# Multi-clock coordinator integration contract

`MultiClockCoordinator` uses the SNES master clock as the only global event
timeline. It does not run guest cores and does not infer missing timing.

Runtime integration must follow these rules:

1. Supply the complete S-CPU micro-access sequence to
   `account_scpu_accesses`. Each address is charged through
   `snes_bus_cycle_master_clocks`. If the sequence is unavailable, pass an
   empty span and stop on `CoordinatorStatus::timing_debt`; never substitute
   architectural instruction cycles.
2. Charge executed SA-1 cycles with `account_sa1_cycles`. The conversion is
   exactly two SNES master clocks per SA-1 cycle. Contention waits must already
   be present in the charged SA-1 cycle count.
3. The SPC clock source must provide explicit `(master clock, phase)` stamps.
   The coordinator records those stamps but deliberately does not invent an
   SPC-to-SNES frequency ratio.
4. Queue S-CPU-to-SPC port commits with `schedule_cpu_to_spc_port` and SPC
   observations with `schedule_spc_sample`. At the same master timestamp,
   `bus_commit` precedes `device_sample`, so the sample observes the committed
   port value when the integration applies events in pop order.
5. Consume events only through `pop_next`. Stable ordering is master clock,
   phase, domain (`S-CPU`, `SA-1`, `SPC`, `PPU`), then insertion sequence.
6. Install the initial frame boundary with `schedule_first_frame`. Its exact
   timestamp is master clock `306900`; this aligns with SA-1 cycle `153450`.

The coordinator does not resolve S-CPU microcode, SPC oscillator conversion,
PPU rendering, DMA arbitration, or shared-bus stalls. Those producers must
provide exact timestamps or retain explicit timing debt.
