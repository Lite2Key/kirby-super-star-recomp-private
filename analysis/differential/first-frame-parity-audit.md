# First observed hardware-frame parity audit

The current runtime reproduces the **pixels** of the first observed SNES frame,
but it does not yet reproduce the **hardware boundary** that produced them.
Both surfaces are 256x239, contain zero nonblack pixels, and have RGBA SHA-256
`c664df7cb2d0d7512f75d4eb998776a29980a051456da362c8152cc14c5416ec`.
That is exact output parity for this forced-blank surface, not complete
first-frame execution parity.

## What is proven

- The ignored Mesen traces independently prove a first-`endFrame` boundary at
  master clock 306900, including final S-CPU/SA-1 state digests, 26,906 ordered
  CPU writes, 3,661 SPC instruction records, 462 SPC port records, and 54 PPU
  plus 23 DMA register events.
- The native runtime causally completes the real SPC IPL start acknowledgement,
  uses no patched state, and executes all 254 generated block identities.
- At its current translated upload frontier, the native renderer emits the
  exact same black RGBA surface as the hardware reference.

## What remains divergent

The runtime renders before it has advanced every processor and device through
the hardware's first `endFrame`; the coordinator then advances to a local frame
marker at 306900. Consequently, the matching black digest does not verify the
reference S-CPU/SA-1 final states, CPU write chain, SPC state/port chains,
PPU/DMA register chains, bus arbitration, scan timing, or interrupt delivery.

The precise next acceptance gate is to continue live execution to the shared
hardware boundary and emit canonical state and event-chain digests. Complete
parity requires those digests to match the committed reference in addition to
the already matching framebuffer digest.

The machine-readable audit is `first-frame-parity-audit.json`. It contains only
counts, identities, booleans, and cryptographic digests; no ROM, IPL, trace, or
frame bytes are included.
