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

The authorized private probe currently reports:

- status `expected_frontier_reached`;
- inventory `2,273`, executed `388`, missing `1,885` identities;
- `4,096` pre-frame upload blocks and a bounded `4,194,304` post-frame route
  budget, ending at the explicit step limit;
- the route executes `388` unique identities, reducing the missing set from
  `1,919` at the prior checkpoint to `1,885` without changing the static
  inventory;
- post-frame SA-1 handoff now follows the generated `$8C5D/$8C60` tail after
  the strict first-frame `$8C58/$8C5B` poll; the bounded route reaches S-CPU
  `$0014` and SA-1 `$8A01`;
- the SPC reaches uploaded RAM and continues through the authentic IPL-driven
  protocol (`5,198,846` SPC cycles at the latest bounded endpoint; final
  F4=`0x72`), with no architectural state patched or rewound.
- a clean private generator/build rerun with the authorized ROM and IPL
  reproduces the same status, counts, endpoint registers, and SPC cycle total;
  the generated route output remains ignored and unpublished.

This is a measured continuation frontier, not a gameplay-completion claim.
The upload counter/latch ordering is now evidenced by the private Mesen trace;
the next proof is to carry the same causal route beyond the SA-1 `$8A01` /
S-CPU `$0014` dynamic-return boundary instead of treating the bounded frontier
as completion.

## Verification gates

- Python suite: `226 passed`.
- Public Windows native suite: `25 / 25 passed`.
- Private generated Windows native suite: `25 / 25 passed`.
