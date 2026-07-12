# Sanitized Ghidra export evidence

- Ghidra: 12.1.2 with the SNES loader extension
- Program language: `65816:LE:24:snes`
- Cartridge mode detected: SA-1
- Sanitized inventory: 68 memory blocks, 173 address/type-only symbols
- Current analysis discovery: 0 functions and 0 entry points
- Validation: 7 exporter schema and allowlist tests passed
- Boundary: the export contains no ROM bytes, disassembly, strings, assets, local paths, or symbol names
- Private source: ignored `.private/ghidra-export/sanitized-program.json`

This proves repeatable import and a safe metadata export lane. It does not yet
claim an address-correct reset CFG; seeding cartridge vectors is the next step.
