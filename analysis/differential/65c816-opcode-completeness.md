# 65C816 lifted-opcode completeness

The lifted executor has an architectural handler for every opcode byte from
`$00` through `$FF`. The recompiler whitelist is exactly that 256-value set;
there are no opcode-level external-hardware blockers.

The lifted runtime now exposes `service_lifted_async_signal` for the testable
architectural part of asynchronous entry:

- IRQ wakes `WAI` even when masked; a masked IRQ does not vector. Accepted IRQ
  and NMI signals push mode-correct hardware frames, set I, clear D, and read
  their emulation/native vectors through the vector bus access kind.
- IRQ and NMI cannot leave `STP`. An explicit reset signal restores reset mode,
  clears the stopped/waiting and pending states, and reads `$FFFC-$FFFD`.
- Reset-line duration is owned by the machine layer, so reset does not charge
  instruction-entry cycles. Accepted IRQ/NMI entry reports and charges the
  architectural 7 emulation-mode or 8 native-mode cycles.

What remains outside this API is signal scheduling, IRQ-source lifetime,
NMI-edge detection and priority arbitration. ABORT entry is also not modeled
because `CpuContext` has no ABORT-pending source. These are asynchronous machine
integration concerns, not missing opcode semantics.

These boundaries do not justify excluding any opcode from static recompilation.
