# M3 bounded PPU state witness

This is a ROM-free summary of the latest private route probe. The authorized
ROM, BWRAM fixture, timestamped NMI schedule, and raw PPU byte dumps remain
runtime-only inputs. No ROM-derived pixels, VRAM, CGRAM, OAM, or trace lines
are committed here.

## Endpoint state

The route-only prefix-24 probe now carries a runtime-only copy of the final
functional PPU snapshot alongside the render result. That separates an absent
video payload from a renderer limitation without widening the public artifact
boundary.

- `ppu_present=1`: the route reached a populated functional PPU snapshot.
- `forced_blank=0`, brightness `1`: the endpoint is not the reset/forced-blank
  surface.
- BGMODE `$2105=07`: the endpoint selects SNES Mode 7, not the currently
  supported Mode 1 renderer path.
- Main-screen latches: `$212C=11`, `$212D=00`, `$212E=00`, `$212F=00`.
- Non-zero private payload counts: `15,144` VRAM bytes, `314` CGRAM bytes,
  and `128` OAM bytes.
- The renderer therefore reports `unsupported_visible_mode` rather than
  silently returning a black frame. This is an honest visual blocker, not a
  missing-state diagnosis.

The private state dump also confirms that the final register stream includes
Mode 7 matrix/center-register writes. The current public snapshot retains the
raw PPU register latches for evidence, while exact transient write ordering and
Mode 7 latch semantics remain a separate parity task.

## Boundary interpretation

The same route still reaches `2,273 / 2,273` compact identity blocks after
`1,433,720` whole blocks, with S-CPU endpoint `$00002C`, SA-1 endpoint
`$00A6E7`, `95` SA-1 message writes, `9,405` S-CPU reads, digest
`036824b1d673bf63`, and a `+52` measured message-poll cadence. The private
Mesen witness remains a distinct `76 / 8,720` signature with `+58` cadence;
that unresolved handoff is still upstream of a full visible-frame proof.

The first end-frame PPU event chain remains exact at `54 / 54`, but it is the
forced-blank boundary at master clock `306,900`. The HALKEN visible-frame
reference is later at master clock `38,545,064` with `3,032` non-black pixels.
The endpoint snapshot is useful evidence that the route has reached real video
state, but it does not prove causal pixel parity at that later boundary.

## Next bounded proof

The next safe step is to preserve the public state witness and add exact Mode 7
register-latch semantics plus a private exploratory renderer. Only after that
surface is covered by synthetic tests and a causal route witness will it be
promoted to the visible-frame milestone. No private image payload is a release
artifact.

## Verification gates

- Public Windows native build: passed after adding the optional PPU snapshot.
- Public Windows native suite: `25 / 25` passed.
- Private generated Windows suite: `25 / 25` passed.
- Python suite before this evidence-only update: `227 passed`.
