# M3 first-frame discovery and DMA checkpoint

This checkpoint records a deterministic, ROM-byte-free boundary through the
first MesenCE SNES `endFrame` event. It does **not** claim that the native
runtime renders the frame yet.

## Reference boundary

- Two authorized-ROM captures produced the same sanitized reference digest.
- Master clock: `306900`.
- S-CPU clock: `31780`; SA-1 clock: `153450`.
- Ordered CPU-write observations: `26906` (`16902` S-CPU, `10004` SA-1).
- PPU-register observations: `54`; DMA-register observations: `23`.
- Raw states, addresses, values, and ROM-derived data remain under `.private/`.

## Static discovery and generation

- First-frame trace: `25087` execution events (`8533` S-CPU, `16554` SA-1).
- Sanitized graph: `254` processor/mode identities and `259` observed edges.
- Lifted and generated: `254 / 254`; unresolved identities: `0`.
- Inventory: `214` S-CPU identities and `40` SA-1 identities.
- Current fail-closed semantics frontier:
  - S-CPU: `JSL` (`$22`) at `$00:8172`, first observed at cycle `21083`.
  - SA-1: `TCD` (`$5B`) at `$00:8C36`, first observed at cycle `112763`.
  - Remaining observed unsupported inventory: 19 S-CPU opcode kinds across 33
    identities, and one SA-1 opcode kind across one identity.

## Reset-time DMA hardware

The runtime now models the WRAM data/address ports, MDMAEN, eight-channel
priority, both directions, source stepping, all B-bus transfer modes, zero
length encoding, and post-transfer register state. The generated reset path
reproduces three channel-1 transfers of `8192`, `23`, and `14` bytes (8229
WMDATA writes total). Committed evidence contains counts, ranges, and digests,
not transferred values.

## Verification

- Python: `108 passed`.
- Windows warnings-as-errors build: passed.
- Windows CTest: `6 / 6 passed`.
- Dashboard generation and JavaScript syntax check: passed.
- Asset-boundary and diff checks: passed.

The next proof is native generated execution through the same first-frame
checkpoint, including the required opcode semantics, PPU/shared-memory effects,
and an explicit CPU/master-clock scheduling contract.
