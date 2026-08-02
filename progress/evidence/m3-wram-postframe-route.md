# M3 WRAM witness and post-frame route evidence

This is a ROM-free summary of the latest private validation lane. The
authorized ROM and the 64-byte SPC IPL remain runtime-only inputs; no derived
opcode bytes or trace lines are committed here.

## Static route closure

- The compact reset-to-visible corpus contains `2,273` processor/mode
  identities and `2,440` observed edges.
- The private generator now emits `2,273 / 2,273` block functions.
- The fifteen executable S-CPU WRAM identities decode only through the ignored
  `.private/traces/wram-witness-bytes.log` map. The witness schema records
  identity, mode, and bytes locally, while the public tree receives only the
  sanitized count and region classification.

## Route-only continuation probe

The development-only route flag is accepted only without an event recorder.
The ordinary first-frame recorder therefore remains clamped to master clock
`306900`, while the route-only probe may retire whole CPU, SA-1, and SPC
instructions after that boundary.

The authorized private probe currently reports (with the clean Mesen BWRAM
fixture supplied only at runtime):

- status `expected_frontier_reached` after the finite route budget;
- inventory `2,273`, with `1,959` unique identities dispatched and `314`
  inventory identities still untouched by this bounded continuation (all
  `2,273` generated functions remain registered; this is not a static
  generation gap);
- `4,096` pre-frame upload blocks and a bounded `4,194,304` post-frame route
  budget, ending at the explicit step limit with both modeled CPUs running;
- all twelve supplied timestamped NMI edges are consumed at whole S-CPU
  boundaries, and the route reaches S-CPU `$008A56` while the SA-1 remains in
  the generated `$008CC0` poll loop;
- a clean Mesen BWRAM capture and the private 8 KiB SRAM snapshot are
  byte-identical. Direct Mesen seeding reaches the same SA-1 `$008CC0`
  frontier. The generator now treats the `$0084A1` RTI as a runtime-derived
  return instead of freezing it to the bootstrap trace's return set, allowing
  the measured `$00CCB7` NMI return and subsequent route dispatch;
- the SPC reaches uploaded RAM and continues through the authentic IPL-driven
  protocol with no architectural state patched or rewound. The fixture and
  all raw route traces remain ignored/private.
- a clean private generator/build rerun with the authorized ROM and IPL
  reproduces the same status, counts, endpoint registers, and SPC cycle total;
  the generated route output remains ignored and unpublished.

This is a measured continuation frontier, not a gameplay-completion claim.
The upload counter/latch ordering, post-frame NMI timing, and runtime-return
dispatch are now evidenced by the private Mesen trace; the next proof is to
extend the route beyond the remaining `$008A56`/`$008CC0` polling frontier
instead of treating the bounded continuation as completion.

## Verification gates

- Python suite: `227 passed`.
- Public Windows native suite: `25 / 25 passed`.
- Private generated Windows native suite: `25 / 25 passed`.
