# Kirby Super Star Static Recompilation

Private, ROM-free research project for statically recompiling the SNES S-CPU and SA-1 program streams into portable C++20. The original game ROM is never committed; local tools accept a user-supplied verified ROM and keep all derived material under ignored directories.

## Accepted reference image

- Region/revision: USA, revision 0, headerless
- Size: `4,194,304` bytes
- SHA-256: `4E095FBBDEC4A16B075D7140385FF68B259870CA9E3357F076DFFF7F3D1C4A62`
- Internal map: SA-1 LoROM (`0x23`), cartridge type `0x35`

## Current state

The repository is in the foundation milestone. It contains the portable runtime contracts, trace-guided recompiler scaffolding, synthetic tests, and an evidence-backed local progress dashboard. It does not contain Nintendo code or assets and is not yet playable.

Open `progress/site/index.html` after running the progress generator for the visual recompilation map. See [PROGRESS.md](PROGRESS.md) for the Git-safe summary.

On Windows, double-click `show-progress.cmd` to regenerate and open the interactive dashboard.

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

Pinned local analysis tools live beneath ignored `.tools/`. Ghidra projects, raw Mesen traces, generated C++, and all other ROM-derived research stay ignored and local.

## Data boundary

Do not commit ROMs, extracted art or audio, save states, screenshots, full instruction traces, Ghidra project databases, or generated game-specific C++. Sanitized counts, hashes, evidence identifiers, and synthetic fixtures are allowed.
