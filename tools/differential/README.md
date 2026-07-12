# Differential validation

Raw register, cycle, and bus-write traces are sensitive reference material and
must remain under ignored `.private/differential/`. Capture the bounded reset
oracle on Windows with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/capture-differential-reference.ps1 -Rom path/to/game.sfc
```

Compare that private oracle to a candidate runtime log with:

```powershell
python -m tools.differential.kss_diff.compare reference.log candidate.log summary.json
```

Only the value-free summary is suitable for progress evidence. It records
digests, counts, pass/fail, and mismatch field names; it never exports register
values, write addresses/values, instruction bytes, disassembly, or assets.

## First-frame oracle

`tools/mesen/first_frame_oracle.lua` stops on the first MesenCE `endFrame`
event and has an independent one-million-write fail-closed ceiling. Its raw
architectural states and write records belong only under `.private/frame-oracle`.
`differential.kss_diff.frame_oracle` converts that log to a strict artifact
containing clocks, counts, and SHA-256 chains. PPU (`$2100-$213F`) and DMA
control/channel (`$420B-$420C`, `$4300-$437F`) events are classified from the
24-bit CPU address with SNES hardware-bank mirroring rules.

```powershell
.\scripts\capture-first-frame-reference.ps1 -Rom "C:\path\to\game.sfc"
```
