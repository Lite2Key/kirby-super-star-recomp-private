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
