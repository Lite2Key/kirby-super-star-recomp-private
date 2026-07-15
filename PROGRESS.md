# Recompilation progress

> This is a ROM-free evidence snapshot. Counts marked `evolving` are discovered inventories, not estimates of total project completion.

Snapshot: `2026-07-15T02:53:15Z` | commit `d697d0b04ba12f9369c51593c83e052a1ef3b347` | tree `dirty`

## Milestone map

| Gate | Status | Evidence requirement |
|---|---|---|
| M0 - Bootstrap | `passed` | All bootstrap checks pass without Nintendo-derived data. |
| M1 - Dual-CPU discovery | `passed` | Repeatable traces and an address-correct initial CFG exist for both CPUs. |
| M2 - Lifter proof | `passed` | Registers, writes, and cycles match the scripted reference corpus. |
| M3 - Reset to first frame | `in_progress` | Hardware events and the fixed first-frame checkpoint match. |
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
| Route coverage corpus | captured required routes | 1 / 14 | `fixed` |
| Route coverage corpus | union observed identities | 2273 / 2273 | `evolving` |
| S-CPU control flow | observed mode-aware blocks | 1032 / 1032 | `evolving` |
| S-CPU control flow | verified blocks | 1 / 1032 | `evolving` |
| S-CPU control flow | semantics-supported identities | 214 / 1032 | `evolving` |
| SA-1 control flow | observed mode-aware blocks | 1241 / 1241 | `evolving` |
| SA-1 control flow | verified blocks | 1 / 1241 | `evolving` |
| SA-1 control flow | semantics-supported identities | 40 / 1241 | `evolving` |
| 65C816 decoder and lifter | opcode definitions | 256 / 256 | `fixed` |
| 65C816 decoder and lifter | executable opcode semantics | 256 / 256 | `fixed` |
| 65C816 decoder and lifter | private generated block functions | 254 / 2273 | `evolving` |
| Deterministic scheduler | synchronization classes | 5 / 8 | `fixed` |
| S-CPU and SA-1 address spaces | synthetic mapping groups | 7 / 7 | `fixed` |
| PPU, DMA, APU, input, and save | validated subsystems | 5 / 5 | `fixed` |
| PPU, DMA, APU, input, and save | SPC700 opcode semantics | 256 / 256 | `fixed` |
| PPU, DMA, APU, input, and save | DSP synthesis stages integrated | 4 / 4 | `fixed` |
| PPU, DMA, APU, input, and save | persistent save bytes | 8192 / 8192 | `fixed` |
| Differential validator | passing required scenarios | 0 / 14 | `evolving` |
| Windows package | release gates | 3 / 7 | `fixed` |
| Linux portability | delivery formats | 0 / 2 | `fixed` |

## Snapshot evidence

**Tests**
- `analysis.bootstrap-dual-cpu-traces` [Dual-CPU bounded trace evidence](progress/evidence/bootstrap-dual-cpu-traces.md)
- `bootstrap.tests` [Progress unit tests](progress/evidence/bootstrap-tests.md)
- `ghidra.sanitized-export` [Ghidra sanitized export validation](progress/evidence/ghidra-sanitized-export.md)
- `m1.reset-cfg` [Dual-CPU reset vectors and trace-seeded CFG](progress/evidence/m1-reset-cfg.md)
- `m1m2.verification` [M1/M2 Python and artifact verification](progress/evidence/m1-m2-verification.md)
- `m2.complete-reset-blocks` [Complete generated S-CPU and SA-1 reset-block proof](progress/evidence/m2-complete-reset-blocks.md)
- `m2.reset-prefix` [MesenCE-matched S-CPU reset prefix](progress/evidence/m2-reset-prefix.md)
- `m3.cpu-hardware-runtime` [CPU semantic closure, hardware boundary, SPC core, and executable boot probe](progress/evidence/m3-cpu-hardware-runtime.md)
- `m3.first-frame-frontier` [First-frame oracle, generated frontier, and reset DMA proof](progress/evidence/m3-first-frame-frontier.md)
- `m3.first-visible-route` [Compact first-visible route and repeat-stable visual oracle](analysis/coverage/first-visible-route-coverage.json)
- `m3.local-verification` [212 Python and 25 native tests](progress/evidence/m3-cpu-hardware-runtime.md)
- `recompiler.opcode-matrix` [Complete opcode metadata tests](progress/evidence/recompiler-tests.md)
- `runtime.rom-validation` [External ROM identity validation](progress/evidence/rom-validation.md)

**Builds**
- `bootstrap.dashboard` [Progress dashboard build](progress/evidence/bootstrap-dashboard.md)
- `bootstrap.github-ci` [Green Windows, Linux, Python, and boundary CI](progress/evidence/github-ci-bootstrap.md)
- `m1m2.windows-native` [M1/M2 Windows native build and CTest](progress/evidence/m1-m2-verification.md)
- `m3.private-generated` [Private-generated Windows build: 25/25 tests](progress/evidence/m3-cpu-hardware-runtime.md)
- `runtime.windows-foundation` [Windows runtime foundation build](progress/evidence/runtime-windows.md)

## Next executable proof

**Close post-reset polling and import the visible route** - owner: `runtime/validation`

- [ ] Continue SA-1 polling concurrently from $8C58 through the first endFrame instead of stopping that domain at its first checkpoint
- [ ] Resolve the remaining two extra CPU-to-SPC and one extra SPC-to-CPU events at exact microphase boundaries
- [ ] Preserve exact PPU parity while matching processor state plus CPU, SA-1, DMA, and SPC ordered digests
- [ ] Decode, lift, privately generate, and execute the 2019 newly discovered first-visible-route identities

## Active blockers

- **technical** `post-first-frame-route-expansion`: The compact reset-to-visible corpus contains 2273 identities and 2440 edges. The remaining route blocker is decoding, lifting, privately generating, and executing the 2019 identities beyond the original 254-block seed. (owner: analysis/recompiler)
- **technical** `shared-first-endframe-parity`: Cooperative reset/MVN scheduling is live, reset-DMA side effects are fully observed, and the PPU chain matches exactly. CPU/S-CPU counts are now only +2, while post-reset SA-1 polling and CPU/DMA/SPC digests remain open. (owner: runtime/validation)

Regenerate the interactive dashboard with:

```powershell
python tools/progress/build.py --evidence progress/progress.json --out progress/site --markdown PROGRESS.md
```
