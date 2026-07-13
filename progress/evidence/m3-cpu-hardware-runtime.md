# M3 CPU closure, hardware boundary, and executable boot probe

This checkpoint closes the unsupported-CPU-semantic inventory observed before
the first SNES `endFrame`, connects generated execution to the Windows runtime,
and establishes bounded hardware and SPC700 models. M3 remains in progress:
cross-domain scheduling and native pixel parity are not yet proven.

## Dual-CPU generated semantics

- First-frame graph: `254 / 254` identities lift and generate.
- Observed unsupported identities: `0` S-CPU, `0` SA-1.
- Executable 65C816 opcode semantics: `256 / 256` definitions with exact
  whitelist equality. Architecture tests cover every addressing class,
  M/X/E-specific widths and timing, indirect and stack-relative wrapping,
  software interrupts, block moves, WAI, and STP. Explicit IRQ/NMI/reset entry
  handles native/emulation stack frames, vectors, flags, and wait/stop wake
  behavior without pretending scheduler-owned signal timing is an opcode.
- The authorized-ROM private generator deterministically emits all `254 / 254`
  observed block functions with zero unresolved identities. Its separate
  `windows-msvc-private` build compiles that ignored generated source and passes
  the same native suite; no ROM-derived source enters Git.
- S-CPU reference slices:
  - complete reset block through `$00:816D`;
  - `$00:8172` JSL through `$00:D66E` (`16` instructions, `58` cycles,
    `11` writes);
  - `$00:D66E` through `$00:D65A` (`73` instructions, `246` cycles,
    `7` writes), followed by architecture-tested `INC A`.
- SA-1 TCD is reference verified at two cycles, and every identity observed
  before the first frame now has implemented semantics.
- The full boot probe records unique processor/PC/E-M-X dispatch identities:
  `236 / 254` currently execute (`196 / 214` S-CPU and `40 / 40` SA-1), with
  the exact 18-identity gap stored in the ROM-free coverage artifact. Of the
  observed inventory, 180 identities also sit inside current reference-verified
  slices. Execution coverage does not imply state, event, timing, or pixel parity.

Raw states, addresses, values, and APU transfer data remain under `.private/`.
Committed differential artifacts contain bounded counts and SHA-256 chains.

## Functional hardware boundary

- Reset DMA and WRAM ports remain verified.
- Shared SA-1 I-RAM `$3000-$37FF`, disconnected upper window, page write
  masks, reset release, reset vector, messages, and bounded status registers
  are implemented.
- First-frame PPU register latches, forced blank/brightness, VRAM, and CGRAM
  functional storage are implemented. The renderer now supports a strict
  Mode 1 BG1/BG2 4bpp plus BG3 2bpp compositing with independent tilemap/CHR
  bases, palettes, flips, scrolling, backdrop transparency, brightness, and
  both SNES BG3 priority orders. Independent main/subscreen selection, layer
  and color windows, fixed/subscreen operands, per-source color math, clipping,
  saturated add/subtract, and half-color conditions are architecture-tested.
  Mode 1 also supports per-BG 8x8/16x16 tiles, all four 32/64-tile map sizes,
  quadrant selection, full-tile flips, coordinate wrapping, and mosaic sizes
  1-16. BG4, interlace, overscan, and pseudo-hires remain fail-closed.
- Main-screen OBJ rendering decodes all 128 OAM entries, all eight size tables,
  signed/wrapped coordinates, 4bpp name selection, palettes, four priority
  levels, flips, tie-breaking, and object/sliver overflow limits against the
  Mode 1 background priority lattice.
- SPC input and output ports are separate; no immediate-echo shortcut exists.
- Address-speed timing uses the 6/8/12-master-clock S-CPU table. SA-1 cycle
  conversion is exactly two master clocks per cycle, and the first reference
  frame boundary is master clock `306900`.
- The isolated multi-clock coordinator now orders bus commits, device samples,
  and frame boundaries, and fails with timing debt when S-CPU micro-accesses
  are unavailable instead of converting whole instructions approximately.
- The executable boot probe is connected to that coordinator. It preserves the
  functional CPU checkpoint, records `558` value-free S-CPU accesses in exact
  generated execution order, reaches S-CPU master `4218`, charges SA-1 to master
  `152336`, and accepts the fixed frame boundary without approximate timing.
  Runtime IPL plus explicit SPC phase points are still required for sound-CPU
  participation in the full parity proof.
- Opt-in timestamped IRQ, NMI, and reset events are ordered after same-time bus
  commits and before the frame boundary, then dispatched through the exact
  lifted async-signal service. The default first-frame probe schedules none.

## SPC700 boundary

- Mesen first-frame inventory: `3661` SPC instructions, `17` observed opcodes,
  `24` PCs, and `462` port events.
- The isolated SPC700 core implements and classifies all `256 / 256` opcodes:
  254 retire normally, SLEEP enters an explicit sleeping state, and STOP enters
  an explicit stopped state. Architecture tests cover ALU/addressing families,
  calls/returns/stack, interrupts, word arithmetic, multiply/divide, bit-carry
  operations, and the IPL upload subset. The exact inventory is recorded in
  `analysis/hardware/spc700-opcode-inventory.md`.
- TEST/CONTROL, DSP address/data, auxiliary I/O, and all three timers are
  modeled at the register boundary, including timer divisors, target-zero,
  read-clear outputs, port clearing, and IPL-overlay control. The DSP register
  bridge now drives BRR, Gaussian interpolation plus ADSR/GAIN, eight ordered
  stereo voices with noise and pitch modulation, and echo/FIR at explicit
  deterministic 32 kHz sample clocks. KON/KOFF, DIR/SRCN, ENVX/OUTX/ENDX,
  FLG, mute, reset, and echo-write-disable behavior are integration-tested.
- The S-CPU `$2140-$2143` boundary delegates to the provisioned core's distinct
  input/output latches. Port access never auto-steps the SPC; the coordinator
  must explicitly request and charge one instruction at a time.
- DSP register activity is atomic between sample clocks. The internal 32 DSP
  microcycles and their one-to-two-cycle register hazards are not approximated;
  host audio output also remains future work.
- The 64-byte IPL is runtime-provisioned and is not stored in the repository.
- Private replay matches architectural state/timing through ordinal `2600`.
  The next divergence proves that the old trace lacks enough master/SPC phase
  information to order an S-CPU port write against an in-flight SPC read; a
  clock-qualified recapture is required and is underway.

## Save boundary

- Board metadata fixes persistent RAM at `0x2000` bytes; the larger SA-1
  BW-RAM address space is not treated as fully persistent.
- Raw `.srm` load/store initializes missing saves, rejects malformed sizes
  without mutating live data, and replaces through a flushed same-directory
  temporary file.
- The exact persistent BW-RAM window is imported/exported through the boot bus,
  with a ROM-adjacent default path and `--save` override. The lifecycle gate
  writes only after a future clean shutdown; ROM rejection, validation,
  execution failure, and the current translated frontier never create or alter
  a save file.

## Input boundary

- Two standard pads have host-facing 12-button state, `$4016` strobe and serial
  reads, `$4017` port-two reads, SNES open-bus masks, and `$4218-$421B`
  auto-joypad words. Disconnected multitap result lines remain zero.
- Auto-joypad words are immediate functional snapshots; VBlank polling latency,
  NMITIMEN behavior, and HVBJOY busy timing remain scheduler work.

## First native frame surface

- Reference and native dimensions: `256 x 239` RGBA8888 (`61184` pixels).
- The first frame is forced blank at brightness 15 and contains zero non-black
  pixels.
- Native forced-blank digest matches the reference:
  `c664df7cb2d0d7512f75d4eb998776a29980a051456da362c8152cc14c5416ec`.
- Mode 1 BG/OBJ rendering is architecture-tested synthetically, but the black
  reset frame is not used to imply that a visible Kirby route has pixel parity.
- `kss-native.exe` presents this 256x239 surface through a Win32/GDI host at
  centered integer scale and maps keyboard plus two XInput pads to the existing
  controller boundary. Closing at the development frontier remains non-saving.

## Executable Windows probe

`run-port.cmd` now validates the authorized external ROM and executes generated
code causally through S-CPU setup, SA-1 shared-memory initialization, and the
current hardware-gated S-CPU checkpoint:

- S-CPU setup dispatches: `160`.
- SA-1 initialization dispatches: `10018`.
- Bounded SA-1 `$8C58 -> $8C5B -> $8C58` poll observation: `2` blocks,
  reading the real shared-I-RAM value `$00`.
- S-CPU post-wait dispatches: `18`.
- Bounded S-CPU APU acknowledgement observation: `20` blocks, returning to
  `$00:D68E` because the real SPC output latch remains at IPL-ready `$AA`.
- Result: S-CPU `$00:D68E`, SA-1 `$00:8C58`.

The probe deliberately returns a development-frontier exit code. Its local
generated S-CPU access timing is accepted, but full cross-domain frame parity
still depends on the clock-qualified SPC acknowledgement/event replay. No PC,
port, or memory value is forced to claim the additional execution coverage.

## Verification

- Python: `149 passed`.
- Windows warnings-as-errors build: passed.
- Windows standard CTest: `20 / 20 passed`.
- Windows private-generated CTest: `20 / 20 passed`.
- Authorized-ROM executable probe: reached the expected hardware frontier.
- Asset-boundary and diff checks: passed.

The next proof is clock-qualified SPC port replay and integration of the
multi-domain coordinator with generated execution through the first boundary.
