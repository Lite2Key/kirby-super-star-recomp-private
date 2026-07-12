# M1/M2 implementation verification

- Python command: `.venv/Scripts/python.exe -m pytest -q`
- Python result: 96 tests passed
- Native command: `scripts/build-windows.cmd`
- Native result: MSVC warnings-as-errors build passed; 5 of 5 CTests passed
- Native suites: runtime foundation, lifted semantics, SA-1 reset timing, generated-block dispatch, and MVN semantics
- Trace CFG freshness: passed against `analysis/coverage/bootstrap-dual.json`
- Lifted reset CFG freshness: passed against the authorized local ROM, sanitized vectors, and trace CFG
- Asset boundary: passed
- Git diff whitespace check: passed

The local ROM was used only as a private input to the freshness check. It and
the raw differential logs remain ignored and are not part of this evidence.
