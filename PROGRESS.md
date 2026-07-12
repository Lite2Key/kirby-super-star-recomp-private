# Recompilation progress

> This is a ROM-free evidence snapshot. Counts marked `evolving` are discovered inventories, not estimates of total project completion.

Snapshot: `2026-07-12T21:08:58Z` | commit `4d42ac9b883261580a80c584549b2888b3847ff9` | tree `clean`

## Milestone map

| Gate | Status | Evidence requirement |
|---|---|---|
| M0 - Bootstrap | `in_progress` | All bootstrap checks pass without Nintendo-derived data. |
| M1 - Dual-CPU discovery | `in_progress` | Repeatable traces and an address-correct initial CFG exist for both CPUs. |
| M2 - Lifter proof | `not_started` | Registers, writes, and cycles match the scripted reference corpus. |
| M3 - Reset to first frame | `not_started` | Hardware events and the fixed first-frame checkpoint match. |
| M4 - Title and menus | `not_started` | All title/menu scenarios pass deterministic replay. |
| M5 - First playable room | `not_started` | The first-room route passes state, pixel, and event checks. |
| M6 - Spring Breeze | `not_started` | Required Spring Breeze routes pass, including helper and save flows. |
| M7 - Full content | `not_started` | Every required content scenario has current evidence. |
| M8 - Full parity | `not_started` | Zero required fallback, unknown executed target, or unexplained critical divergence. |
| M9 - Windows release candidate | `not_started` | Clean-machine Windows acceptance and asset-boundary audit pass. |
| M10 - Native presentation experiments | `not_started` | Compatibility path remains green and selectable. |
| M11 - Linux delivery | `not_started` | Both packages pass external-ROM launch and save checks. |

## Component counters

| Component | Counter | Progress | Denominator |
|---|---|---:|---|
| ROM and address map | classified banks | 0 / 64 | `fixed` |
| S-CPU control flow | observed mode-aware blocks | 214 / 214 | `evolving` |
| S-CPU control flow | verified blocks | 0 / 214 | `evolving` |
| SA-1 control flow | observed mode-aware blocks | 23 / 23 | `evolving` |
| SA-1 control flow | verified blocks | 0 / 23 | `evolving` |
| 65C816 decoder and lifter | opcode definitions | 256 / 256 | `fixed` |
| 65C816 decoder and lifter | reference-verified semantics | 0 / 256 | `fixed` |
| Deterministic scheduler | synchronization classes | 0 / 8 | `fixed` |
| S-CPU and SA-1 address spaces | synthetic mapping groups | 7 / 7 | `fixed` |
| PPU, DMA, APU, input, and save | validated subsystems | 0 / 5 | `fixed` |
| Differential validator | passing required scenarios | 0 / 14 | `evolving` |
| Windows package | release gates | 1 / 7 | `fixed` |
| Linux portability | delivery formats | 0 / 2 | `fixed` |

## Snapshot evidence

**Tests**
- `analysis.bootstrap-dual-cpu-traces` [Dual-CPU bounded trace evidence](progress/evidence/bootstrap-dual-cpu-traces.md)
- `bootstrap.tests` [Progress unit tests](progress/evidence/bootstrap-tests.md)
- `ghidra.sanitized-export` [Ghidra sanitized export validation](progress/evidence/ghidra-sanitized-export.md)
- `recompiler.opcode-matrix` [Complete opcode metadata tests](progress/evidence/recompiler-tests.md)
- `runtime.rom-validation` [External ROM identity validation](progress/evidence/rom-validation.md)

**Builds**
- `bootstrap.dashboard` [Progress dashboard build](progress/evidence/bootstrap-dashboard.md)
- `runtime.windows-foundation` [Windows runtime foundation build](progress/evidence/runtime-windows.md)

## Next executable proof

**Build the initial reset CFG and prove the first lifted blocks differentially** - owner: `analysis/recompiler`

- [ ] Seed an address-correct reset CFG from the observed S-CPU and SA-1 blocks
- [ ] Carry explicit processor, 24-bit PC, E, M, and X state through every reset-path edge
- [ ] Lift the first reachable reset blocks and compare registers, flags, bus writes, and cycles against the reference
- [ ] Increment verified-block counts only for blocks with current differential evidence

## Active blockers

No recorded blockers in this snapshot.

Regenerate the interactive dashboard with:

```powershell
python tools/progress/build.py --evidence progress/progress.json --out progress/site --markdown PROGRESS.md
```
