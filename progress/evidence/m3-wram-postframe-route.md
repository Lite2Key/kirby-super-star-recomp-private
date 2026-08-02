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

The newest authorized private witness uses the clean Mesen BWRAM fixture and a
102-edge timestamped NMI schedule (the schedule and fixture remain ignored).
A prefix sweep found that the first `24` edges are sufficient and necessary for
the current bounded route: prefix `23` stops at an SA-1 unknown-block boundary
with `2,270 / 2,273` identities, while prefix `24` reaches the inventory
checkpoint with no missing identity.

- the generated route continuation reaches its explicit inventory checkpoint
  (`post_status=checkpoint_reached`) after `1,433,720` whole blocks;
- inventory `2,273` has `2,273` unique identities dispatched and `0` missing;
  this closes the current compact reset-to-visible corpus, not the whole game;
- `4,096` pre-frame upload blocks are retained, both modeled CPUs remain live,
  and the final route endpoints are S-CPU `$00002C` and SA-1 `$00A6E7`;
- the clean 24-edge prefix run records final SA-1 `$2209` message-latch state
  `0`, so the S-CPU `$2300` poll at `$00002C` is waiting for a later causal
  message edge rather than crashing or fabricating a ready value;
- the minimum-prefix run returns `expected_frontier_reached` after processing
  `24` NMI edges. Supplying the complete `102`-edge schedule reaches the same
  inventory checkpoint and endpoint, then returns outer status `timing_debt`
  because `78` later timestamped edges remain outside the finite continuation;
  this is not an unknown-block or static-generation failure;
- the final APU port-0 latch is `0xC3` (`195`) with the authentic `0xCC`
  acknowledgement observed, and the route retires `5,515,447` S-CPU cycles
  and `18,024,378` SA-1 cycles before the checkpoint;
- a clean Mesen BWRAM capture and the private 8 KiB SRAM snapshot are
  byte-identical. The generator treats the `$0084A1` RTI as a runtime-derived
  return, allowing measured NMI returns and the later dynamic-vector route;
- the SPC reaches uploaded RAM through the authentic IPL-driven protocol with
  no architectural state patched or rewound. Raw ROM, IPL, schedule, fixture,
  and route traces remain ignored/private.

## Message-latch causal summary

The route probe now exports a value-free summary of the message handoff: counts
of SA-1 `$2209` writes, counts of S-CPU `$2300` reads, and an FNV-1a digest of
the ordered four-bit values plus producer/consumer direction. It never exports
the raw event stream. At the prefix-23 control boundary the static route has
`94` writes and `9,400` reads (`e71e3383ca2592fb`); the decisive prefix-24
handoff reaches `95` writes and `9,405` reads (`036824b1d673bf63`) at
S-CPU `$00002C` / SA-1 `$00A6E7` with the final latch still `0`.

A separate private Mesen witness, seeded from the same clean BWRAM fixture and
cut at the corresponding 24th-NMI master-clock neighborhood, currently reports
`76` writes and `8,720` reads with digest `c9d5b52ab4d84ff1`. These sanitized
signatures are intentionally published as a parity target: they do not yet
match, and the difference is the active causal-boundary investigation rather
than a claim of transient parity.

This is a measured compact-route closure, not a gameplay-completion claim.
The optional public latch snapshot is an endpoint observation only; it does not
replace a transient event proof. The next proof is causal parity at the new
`$00002C`/`$00A6E7` handoff: reconcile the two sanitized signatures, then close
ordered CPU/SA-1 micro-accesses, the remaining timestamped signal schedule,
SPC acknowledgement timing, dynamic returns, and event-chain digests before M3
can close.

## Verification gates

- Python suite: `227 passed`.
- Public Windows native suite: `25 / 25 passed`.
- Private generated Windows native suite: `25 / 25 passed`.
