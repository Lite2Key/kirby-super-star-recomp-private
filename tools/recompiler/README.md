# Recompiler foundation

This directory contains the ROM-safe analysis core for the Kirby Super Star
static recompiler. It never stores ROM payloads: inputs are read in memory and
outputs contain identity, header, control-flow, and generated-code metadata.

Run the synthetic test suite from the repository root with:

```powershell
python -m pytest tests/recompiler
```

The first dual-CPU graph is generated entirely from sanitized execution
coverage. It preserves the processor, 24-bit PC, and E/M/X mode in every node
and edge; it intentionally contains no instruction bytes yet:

```powershell
python -m recompiler.kssrecomp.trace_cfg analysis/coverage/bootstrap-dual.json analysis/cfg/bootstrap-dual.trace-cfg.json
```

Pass `--check` to verify that the committed graph is byte-for-byte current.

## Private static generation

`prepare-private-recomp.cmd` validates the authorized USA ROM, combines it
with sanitized first-frame coverage, lifts every observed processor/mode/PC
identity, and emits deterministic C++ registration plus manifests below
`.private/generated/first-frame`. ROM-derived instruction bytes never leave
the ignored `.private` tree. Run the command again with the same ROM to refresh
the outputs; the Python entry point also supports `--check` for reproducibility.

Future route captures can be passed as repeated `--coverage` arguments. They
are conservatively unioned only when every capture has the same reset entries;
each block and edge retains route provenance, while overlapping hit counts are
combined. This gives the static generator and progress map an expanding,
auditable denominator instead of treating one trace as the whole game.
