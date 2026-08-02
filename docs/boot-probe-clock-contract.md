# Boot-probe clock contract

The executable boot probe records its complete generated S-CPU access stream
locally. A default call can therefore reach `expected_frontier_reached` without
external timing evidence while retaining the same S-CPU, SA-1, shared-memory,
and forced-blank frame diagnostics.

The timing contract is:

- Every generated block emits value-free opcode and operand fetch addresses
  before executing its lifted semantics. `ScpuMicroAccessRecorder` interleaves
  those observations with data, stack, vector, and DMA bus calls in execution
  order. Each recorded address is charged through
  `snes_bus_cycle_master_clocks`; architectural cycle counts are never converted
  approximately.
- Records contain address, access kind, read/write direction, and the FastROM
  latch state only. `$420D` values are used transiently to update that latch and
  are never retained in the recorder.
- SA-1 `CpuContext::cycles` is converted exactly at two master clocks per SA-1
  cycle. The translated block must already have added any evidence-backed waits.
- SPC execution requires a runtime-provided 64-byte IPL and explicit
  `(master clock, phase)` step points. The probe schedules each point through
  `MultiClockCoordinator` and calls `step_spc()` once for each popped SPC event.
  Port access itself never runs the SPC.
- SPC points after the first-frame boundary at master `306900` are timing debt.
- Missing/invalid IPL, unsupported SPC behavior, overflow, and non-monotonic
  timestamps fail closed with distinct statuses.

The current default executable proves timing for the generated access stream;
this is not yet a whole-frame hardware timing-parity claim. Private IPL bytes
remain runtime-only and are never committed or included in timing records.
