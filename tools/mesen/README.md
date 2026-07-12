# MesenCE trace harness

`dual_cpu_trace.lua` uses MesenCE's official execution callbacks for both the
SNES S-CPU and SA-1. It records only CPU identity, cycle, 24-bit execution
address, and E/M/X mode flags. It does not record opcode callback values,
register contents, memory, screenshots, or ROM data.

The capture stops after 10,000 combined events. Always launch it through
`scripts/run-mesen-trace.ps1`, which confines raw logs to `.private/traces`.
