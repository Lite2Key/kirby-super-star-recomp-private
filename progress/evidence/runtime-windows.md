# Windows runtime foundation evidence

- Evidence ID: `runtime.windows-foundation`
- Command: `scripts/build-windows.cmd`
- Compiler: MSVC 19.50.35729.0, C++20, `/W4 /WX /permissive-`
- Result: configure, build, and CTest passed
- Coverage: CPU state, block keys, deterministic scheduling, checked dispatch, SHA-256, S-CPU/SA-1 address maps, WRAM/IRAM/BWRAM behavior, ROM protection, placeholders, and independent open-bus state
- Boundary: synthetic memory fixtures only; no ROM bytes stored
