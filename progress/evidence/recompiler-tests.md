# Complete opcode metadata evidence

- Evidence ID: `recompiler.opcode-matrix`
- Command: `.venv/Scripts/python -m pytest tests/recompiler`
- Result: all recompiler tests passed as part of the 39-test integrated suite
- Coverage: all 256 official 65C816 opcode definitions, 29 addressing modes, M/X-dependent immediate widths, control-flow classes, strict ambiguity handling, CFG serialization, and deterministic C++ stub generation
- Boundary: synthetic instruction fixtures only; no ROM bytes stored
