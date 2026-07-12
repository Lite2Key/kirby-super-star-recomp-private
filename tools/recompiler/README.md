# Recompiler foundation

This directory contains the ROM-safe analysis core for the Kirby Super Star
static recompiler. It never stores ROM payloads: inputs are read in memory and
outputs contain identity, header, control-flow, and generated-code metadata.

Run the synthetic test suite from the repository root with:

```powershell
python -m pytest tests/recompiler
```
