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
