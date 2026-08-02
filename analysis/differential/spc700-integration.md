# SPC700 first-frame integration boundary

The sanitized MesenCE oracle observed 3,661 SPC instructions across 24 PCs and
17 opcode values before the first SNES `endFrame`, plus 462 bidirectional CPU
port events. Register states and port values remain private behind SHA-256
chains in `spc-first-frame-reference.json`.

`Spc700Core` remains isolated and fail-closed, but now implements and classifies
all 256 opcodes. It provides 64 KiB APURAM, runtime-only 64-byte IPL
provisioning with write-through overlay semantics, CONTROL port
clears/overlay selection, all three timers, and four independent bidirectional
CPU port latches. `$F2/$F3` drive a live DSP core with explicit 32 kHz sample
clocks, BRR/voice/envelope/mixer/noise/echo processing, and visible DSP register
state. No IPL bytes or host audio backend are embedded in the repository.

The remaining first-frame integration gate is temporal, not opcode support:
schedule SPC bus accesses and CPU port commits at their clock-qualified phases,
produce the real `$CC` acknowledgement required by the S-CPU upload loop, and
compare the complete execution/state/port digests. The current SPC step is
instruction-atomic and the current DSP clock is sample-atomic; neither invents
mid-instruction or internal 32-microcycle ordering.

## Private replay audit

With CPU-to-SPC writes applied after their surrounding SPC instruction, the
core matches PC, A, X, Y, SP, P, and the observed 2x counter relationship
through execution record 2600. Record 2601 diverges in Y because a port-1 write
arrived during the preceding MOV Y,dp instruction. Moving all such writes
before the surrounding instruction is not a valid workaround: that policy
diverges earlier at record 2589 in P because a port-0 write arrived after the
preceding CMP had already read the port.

The current private oracle timestamps CPU-to-SPC writes only in S-CPU cycle
space. It cannot determine their phase relative to an in-flight SPC memory
read. A full 3,661-record replay therefore remains unproven. Recapture each
port event with master clock plus SPC cycle or explicit port-read phase, and
define a deterministic same-clock tie break before integrating the core into
the scheduler. See `spc700-private-replay-audit.json` for the value-free exact
divergence identities.

The implementation is a dependency-free adaptation informed by the
ISC-licensed ares SPC700 and SFC SMP components. The required notice is at
`licenses/ares-ISC.txt`; no ares framework code or Nintendo IPL data is copied.

## Clock-qualified oracle V2

`spc_first_frame_oracle.lua` now emits one capture-wide callback ordinal plus
Mesen's master clock, SPC counter, and S-CPU counter on every SPC execution
boundary, S-CPU `$2140-$2143` write, and SPC `$F4-$F7` read/write. The callback
ordinal is the authoritative tie-break for callbacks at the same master clock.
The strict sanitizer accepts this V2 form, rejects discontinuous event/I/O/exec
ordinals and non-monotonic master clocks, and exports only counts and SHA-256
chains. It retains V1 support so the committed reference does not falsely
claim a recapture that has not happened.

The private V2 replay consumes events in callback-ordinal order. For an atomic
SPC instruction it applies S-CPU writes before the first recorded port read,
checks the read latch, executes the instruction, validates SPC writes, then
applies writes recorded after the read. It fails with `MICROPHASE_REQUIRED` if
an S-CPU write occurs between two SPC reads in one instruction, because that
case cannot be reproduced by the current instruction-atomic core.

The MesenCE recapture could not be launched in this run: the required external
process approval was rejected because the execution-account usage limit was
reached. No alternate launch path was attempted. Consequently, the V1
2,600-record prefix remains the evidence boundary until a V2 private capture
is produced and replayed.

## Shared scheduler contract

The runtime scheduler should own timing; `Spc700Core` should own only SPC
architectural state and the two four-byte port-latch directions.

1. Represent time as an integer SNES master-clock tick plus an integer rational
   APU remainder. Do not derive deadlines with floating point.
2. Queue every S-CPU `$2140-$2143` write as `{master_tick, phase, sequence,
   port, value}` when the bus write commits. `sequence` must be monotonic and is
   the deterministic tie-break for equal tick/phase events.
3. Advance the SPC only to the next of: queued port commit, SPC bus-access
   phase, requested synchronization deadline, or frame boundary. Port reads
   sample the CPU-to-SPC latch at their bus-read phase; SPC writes update the
   opposite latch at their bus-write phase.
4. Define and test a stable equal-tick phase ordering against the V2 oracle.
   The trace callback ordinal supplies the oracle ordering; it must not be
   guessed from CPU cycle counters.
5. Before accepting arbitrary game execution, split `step()` at observable bus
   accesses (or add a resumable bus callback). Instruction-atomic stepping is
   allowed only when the scheduler proves no external commit falls between the
   instruction's observable port accesses.
6. CONTROL `$F1` latch clears occur in SPC bus-write order. S-CPU reads of
   `$2140-$2143` return the latest committed SPC-to-CPU latch without aliasing
   the CPU-to-SPC direction.
7. Synchronization returns a structured reason (`deadline`, `port_access`,
   `unsupported_opcode`, `unsupported_io`, or `stopped`) and the exact master
   tick/remainder. Unsupported behavior remains fail-closed.

The scheduler acceptance gate is a complete V2 replay of all 3,661 execution
records, matching register state, opcode, SPC counter relation, every recorded
port read/write, and the final state at the first `endFrame`.
