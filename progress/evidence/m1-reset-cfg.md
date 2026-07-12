# M1 dual-CPU reset discovery evidence

- Sanitized vector artifact: `analysis/vectors/reset-vectors.json`
- Trace-seeded CFG artifact: `analysis/cfg/bootstrap-dual.trace-cfg.json`
- Decoded reset CFG artifact: `analysis/cfg/bootstrap-dual.lifted-reset.json`
- Cartridge mapping: SA-1 LoROM, map mode `$23`
- S-CPU reset entry: `00:8004`, reset mode `e1m1x1`, cycle 0
- SA-1 observed reset entry: `00:8BF4`, reset mode `e1m1x1`, cycle 1454
- Graph inventory: 183 processor/mode-aware nodes and 184 observed edges
- ROM-backed decode inventory: 160 S-CPU nodes decoded, 18 SA-1 nodes decoded, and 1 SA-1 node explicitly unresolved
- Every node and edge preserves processor, 24-bit PC, E, M, and X
- The static S-CPU reset vector agrees with the first S-CPU trace event
- The SA-1 entry is trace-observed and modeled as runtime-programmed through `$2203/$2204`
- Schema and consistency tests reject cross-CPU edges, invalid modes, missing endpoints, duplicate records, and trace-summary mismatches

This closes the initial discovery/CFG gate. The graph contains observed control
flow and bounded instruction metadata, not a claim that all decoded semantics
have been implemented or verified.
