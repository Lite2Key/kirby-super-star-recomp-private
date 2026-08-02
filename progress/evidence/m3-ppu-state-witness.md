# M3 bounded PPU state witness

This is a ROM-free summary of the latest private route probe. The authorized
ROM, BWRAM fixture, timestamped NMI schedule, and raw PPU byte dumps remain
runtime-only inputs. No ROM-derived pixels, VRAM, CGRAM, OAM, or trace lines
are committed here.

## Endpoint state

The route-only prefix-24 probe now carries an exact first-frame render and a
separate runtime-only final route snapshot. The first-frame source state is
captured before route continuation, so the native "first-frame" surface cannot
silently become a later diagnostic state. The endpoint snapshot still
separates an absent video payload from a renderer limitation without widening
the public artifact boundary.

The exact first-frame boundary remains the forced-blank event at master clock
`306,900`; its public render is therefore an intentionally black, valid frame.
The later endpoint is the populated Mode 7 witness described below and is not
being presented as first-frame parity.

- `ppu_present=1`: the route reached a populated functional PPU snapshot.
- `forced_blank=0`, brightness `1`: the endpoint is not the reset/forced-blank
  surface.
- BGMODE `$2105=07`: the endpoint selects SNES Mode 7, not the currently
  supported Mode 1 renderer path.
- Main-screen latches: `$212C=11`, `$212D=00`, `$212E=00`, `$212F=00`.
- Non-zero private payload counts: `15,144` VRAM bytes, `314` CGRAM bytes,
  and `128` OAM bytes.
- The bounded renderer now has a synthetic-tested Mode 7 BG1 path, including
  interleaved map/pixel addressing, signed 13-bit offsets, matrix truncation,
  and the `$211A` wrap/fill modes. The full route still reports
  `unsupported_feature` because `$212C=11` enables OBJ and the endpoint's
  sprite-limit chronology is not yet proven. That is an honest remaining
  composition blocker, not a missing-state diagnosis; a private BG1-only
  preview is used only for inspection and is not parity evidence.

The private state dump also confirms that the final register stream includes
Mode 7 matrix/center-register writes. The current public snapshot retains the
raw PPU register latches for evidence. Mode 7 write-twice latch semantics are
now implemented and synthetic-tested; exact transient write ordering and
later visible-frame timing remain separate parity tasks.

## Boundary interpretation

The same route still reaches `2,273 / 2,273` compact identity blocks after
`1,433,720` whole blocks, with S-CPU endpoint `$00002C`, SA-1 endpoint
`$00A6E7`, `95` SA-1 message writes, `9,405` S-CPU reads, digest
`036824b1d673bf63`, and a `+52` measured message-poll cadence. The private
Mesen witness remains a distinct `76 / 8,720` signature with `+58` cadence;
that unresolved handoff is still upstream of a full visible-frame proof.

The route-only continuation now has an explicit diagnostic mode that may pass
the `2,273 / 2,273` identity frontier instead of treating coverage as a time
boundary. With the private prefix-31 witness, that mode consumes NMI 28 and
fails closed at the next unregistered S-CPU target `$00D0FF` (post-route
status `unknown_block`) at approximately master `37,495,688`. This is a
useful next lift target, not a claim of visible-frame parity.

The first end-frame PPU event chain remains exact at `54 / 54`, but it is the
forced-blank boundary at master clock `306,900`. The HALKEN visible-frame
reference is later at master clock `38,545,064` with `3,032` non-black pixels.
The endpoint snapshot is useful evidence that the route has reached real video
state, but it does not prove causal pixel parity at that later boundary.

## Next bounded proof

The next safe step is to lift and privately validate the `$00D0FF` handoff,
replay through the next measured NMI boundary, and then close Mode 7
composition around the enabled OBJ path. Only after those surfaces are
covered by synthetic tests and a causal route witness will they be promoted to
the visible-frame milestone.
No private image payload is a release artifact.

## Verification gates

- Public Windows native build: passed after adding the exact first-frame
  render and bounded Mode 7 path.
- Public Windows native suite: `25 / 25` passed.
- Private generated Windows suite: `25 / 25` passed.
- Python suite: `227 passed`.
