# Bootstrap dual-CPU trace evidence

- Evidence ID: `analysis.bootstrap-dual-cpu-traces`
- Source format: `mesen-ce-kss-trace-v1`
- S-CPU source: `analysis/coverage/bootstrap-scpu.json`
- S-CPU result: 10,000 instruction events and 214 unique `(processor, PC, E, M, X)` blocks
- SA-1 source: `analysis/coverage/bootstrap-sa1.json`
- SA-1 result: 5,000 instruction events and 23 unique `(processor, PC, E, M, X)` blocks
- Combined repeatability source: `analysis/coverage/bootstrap-dual.json`
- Combined repeatability result: the fixed harness completed naturally at 10,000 events with exactly 5,000 S-CPU events and 5,000 SA-1 events, observing 160 S-CPU and 23 SA-1 mode-aware blocks
- Corpus total: 25,000 captured instruction events across the two broader per-CPU runs and the fixed combined run; event totals overlap in behavior and must not be read as unique coverage
- Coverage meaning: these blocks were observed in the bounded bootstrap traces; the discovered denominator will grow as additional routes execute
- Verification meaning: **zero blocks are currently claimed as semantics-verified**; observation proves execution reachability and mode identity, not register, flag, bus-write, or cycle correctness of lifted code
- Boundary: the evidence summary contains counts and local paths only; it contains no ROM bytes, instruction bytes, screenshots, audio, or protected assets
