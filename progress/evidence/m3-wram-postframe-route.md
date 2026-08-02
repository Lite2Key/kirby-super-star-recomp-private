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

The private event alignment narrows the gap further. The first ordering
divergence is static event `1567`: the static route emits an SA-1 set token
(`0x81`) while Mesen is still reading the zero latch. Before the first NMI,
Mesen has `1 / 1,895` writes/reads versus static `1 / 1,566`; after NMI 3 the
same write count is at `11`, but Mesen has `8,130` reads versus static `8,729`.
The streams remain write-aligned through NMI 20 (`76` writes), then the static
route continues with `19` additional writes and `684` additional reads through
the prefix-24 endpoint while the Mesen witness remains at `76 / 8,721`.
This points to pre-NMI polling and later route timing/control-flow drift, not a
masking error in the `$2209` latch itself.

The route now also exports two value-free `$2300` access-completion anchors.
The static prefix-24 run completes its first two polls at master `27,742,810`
and `27,742,862` (`+52` cadence); the corresponding Mesen witness completes
them at `27,710,936` and `27,710,994` (`+58` cadence). The first-entry gap is
therefore `31,874` master clocks and the second is `31,868`, shrinking by the
six-master cadence difference. This separates a large pre-poll route-entry
deficit from the steady-state poll cadence and is why the rejected `+6`
taken-branch A/B cannot be treated as a complete fix. These are bus-derived
access-completion anchors, not NMI service timestamps or raw event exports.

The same run records the earlier SA-1 poll-release scheduler edge at master
`27,742,664`. The corresponding Mesen witness has its first `$3010=0` write at
`27,710,962`, after the first S-CPU `$0014` poll begins at `27,710,906` and its
`$2300` read completes at `27,710,936`. The cross-witness offset is `31,702`
masters, while the static release edge is only `146` masters before its first
poll completion. Because the static value is a poll-release scheduler edge and
the Mesen value is a bus-write witness, this is an ordering diagnostic rather
than a claim that the two timestamps are identical hardware phases. It places
the large deficit at the upstream SA-1/S-CPU handoff, before the steady-state
branch cadence.

Two additional private-only scoped A/Bs bracketed that rejected result. Adding
`+4` master clocks to the taken `$0017` poll branch preserved endpoint `$A6E7`
and reduced the static summary to `95 / 8,740` with digest
`df2e09856381b4a1`, but its measured cadence was still `+56` and the digest
did not match Mesen. Adding `+5` produced `95 / 8,585` with digest
`cdc199690daee9b7`, cadence `+57`, and moved the endpoint to `$A6E6`. Both
experiments were removed and rejected; they show that count convergence alone
is not a safe timing model.

A third private-only A/B held the released SA-1 at the `$8C5B` whole-instruction
boundary until the first static `$2300` poll completion, matching the observed
Mesen ordering hypothesis. It produced the same `2,273 / 2,273` endpoint,
`95 / 9,405` message summary, digest `036824b1d673bf63`, and `+52` poll
cadence as the baseline. The hold was therefore removed: the remaining drift
is not explained by that single release-versus-first-poll ordering seam.

A narrower private-only A/B then held immediately before the `$89FB` `STZ
$3010` and inserted the evidence-bound 18-master-clock pre-write wait (so the
8-master-clock instruction completion would land 26 clocks after the first
`$2300` completion). It preserved inventory coverage but changed the endpoint
to SA-1 `$00A6E6`, the summary to `95 / 9,408`, and the digest to
`a6d1b234569d0fcd`; it was removed and rejected. The measured write witness is
useful for diagnosis, but this single alignment cannot be promoted to a timing
rule without the adjacent S-CPU poll cadence and later route state proving it.

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
- The value-free S-CPU recorder now has a focused native wait-loop oracle for
  `$0014` BIT `$2300` → `$0017` BEQ `$0014`: seven ordered accesses, eight
  architectural cycles, and the current 52-master-clock bus-only charge. This
  is a regression rail for the future scoped internal-cycle model; it does not
  claim that 52 matches the Mesen cadence yet.
- The coordinator now accepts an explicit, transactional internal-duration
  argument without changing existing callers; the same oracle proves the
  measured `+6` candidate would charge 58 master clocks while preserving its
  seven accesses and eight architectural cycles. A private prefix-24 A/B using
  that candidate was deliberately removed: it kept the `2,273 / 2,273` route
  endpoint but moved the static signature to `95 / 8,437`
  (`e29be5c6e5ac04ab`), overshooting the Mesen `76 / 8,720` witness. This
  rejects a blanket taken-branch correction as the fix and leaves route-entry
  drift and poll cadence as separate proof obligations.
