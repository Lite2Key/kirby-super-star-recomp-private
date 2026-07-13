# Kirby Super Star Static Recompilation

Private, ROM-free research project for statically recompiling the SNES S-CPU and SA-1 program streams into portable C++20. The original game ROM is never committed; local tools accept a user-supplied verified ROM and keep all derived material under ignored directories.

## Accepted reference image

- Region/revision: USA, revision 0, headerless
- Size: `4,194,304` bytes
- SHA-256: `4E095FBBDEC4A16B075D7140385FF68B259870CA9E3357F076DFFF7F3D1C4A62`
- Internal map: SA-1 LoROM (`0x23`), cartridge type `0x35`

## Current state

The repository is in M3, reset-to-first-frame. The first-frame trace contains
254 dual-CPU processor/PC/mode identities; all 254 lift and generate, and every
observed identity now has an executable 65C816 semantic; all 256 opcodes have
architecture-tested execution semantics. With a runtime-only authentic IPL,
all 254 identities execute through the real SPC `$CC` acknowledgement with no
patched state. Reset DMA, shared SA-1 I-RAM/control,
strict Mode 1 BG1/BG2/BG3 plus OBJ main/subscreen, windows, and color math, a
256-opcode SPC700 core with timers and DSP-register I/O, two-pad controller
register integration, and board-accurate 8 KiB save persistence exist. The
Windows executables run the verified external ROM through S-CPU setup, SA-1
initialization, the current hardware-gated checkpoint, and an exact value-free
generated S-CPU access stream. `kss-native.exe` presents the resulting 256x239
surface at integer scale and maps keyboard/XInput state onto both SNES controller
ports; execution remains paused at the explicit diagnostic frontier. Complete SPC port replay, visible reference
pixels, and gameplay remain in
progress, so this is not yet a playable port.

Open `progress/site/index.html` after running the progress generator for the visual recompilation map. It includes all 254 currently observed first-frame block identities and a 42-checkpoint port workstream atlas. See [PROGRESS.md](PROGRESS.md) for the Git-safe summary.

On Windows, double-click `show-progress.cmd` to regenerate and open the interactive dashboard.

After building, run the translated boot probe with the default Downloads path
or an explicit ROM path:

```powershell
.\run-port.cmd
.\run-port.cmd "C:\path\to\Kirby Super Star (USA).sfc"
.\build\windows-ninja\kss-native.exe --rom "C:\path\to\Kirby Super Star (USA).sfc" --save "C:\path\to\slot-a.srm"
.\build\windows-ninja\kss-recomp.exe --rom "C:\path\to\Kirby Super Star (USA).sfc" --dump-first-frame ".private\native-first-frame.bmp"
```

The native host uses arrow keys for the D-pad; `Z/X/A/S` for `B/A/Y/X`;
right Shift and Enter for Select/Start; and `Q/W` for L/R. XInput pads 1 and 2
feed SNES ports 1 and 2. Escape or closing the window exits cleanly. Until the
translated runtime advances beyond its current frontier, shutdown intentionally
does not commit `.srm` changes.

To regenerate and compile the ignored static-recompiler output directly from
the authorized ROM, run:

```powershell
.\build-private-recomp.cmd "C:\path\to\Kirby Super Star (USA).sfc"
```

This produces 254 first-frame block functions below `.private/generated`,
builds them in the separate `build/windows-private` tree, and runs the same
native verification suite. No ROM-derived source is added to Git.

The expected development-frontier exit code is `4`; it prevents the diagnostic
from being mistaken for a completed frame/game run.

## Local workflow

```powershell
python -m venv .venv
.\.venv\Scripts\python -m pip install -e ".[dev]"
.\.venv\Scripts\python -m pytest
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Full reference analysis is local-only:

```powershell
.\scripts\validate-rom.ps1 -RomPath "C:\path\to\Kirby Super Star (USA).sfc"
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\ghidra-import.ps1 -RomPath "C:\path\to\Kirby Super Star (USA).sfc" -NoAnalysis
```

Pinned local analysis tools live beneath ignored `.tools/`. Ghidra projects,
raw Mesen traces, screenshots, and extracted assets stay ignored and local.

## Data boundary

Do not commit ROMs, extracted art or audio, save states, screenshots, full
instruction traces, or Ghidra project databases. This private research repo may
track bounded statically recompiled instruction functions and sanitized
identity/control-flow artifacts after the asset-boundary check passes; bulk ROM
bytes and extracted content remain prohibited.
