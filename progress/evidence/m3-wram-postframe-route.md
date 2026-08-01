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
- inventory `2,273`, executed `254`, missing `2,019` identities;
- `4,096` pre-frame upload blocks and a bounded `131,072` post-frame route
  budget, ending at the explicit step limit;
- the route executes `279` unique identities, reducing the missing set to
  `1,994` without changing the static inventory;
- S-CPU `$D5E9` and SA-1 `$8C5B` are the active generated identities after the
  upload wait;
- the SPC reaches uploaded RAM at `$0760` after `155,855` cycles, and the
  final SPC output latch is `0x00` while the S-CPU remains at the transfer
  boundary, exposing the next handshake phase without fabricated state.

This is a measured continuation frontier, not a gameplay-completion claim.
The next proof is to model the counter/latch ordering and then extend the
route through the first newly observed post-upload identity.

## Verification gates

- Python suite: `226 passed`.
- Public Windows native suite: `25 / 25 passed`.
- Private generated Windows native suite: `25 / 25 passed`.
