# Architecture decision record

## Authoritative execution path

The first complete implementation statically recompiles both ROM-resident 65C816-family instruction streams: the SNES S-CPU and cartridge SA-1. Generated blocks are specialized by processor, 24-bit program counter, and E/M/X width state. The original guest stack and memory-visible behavior remain authoritative.

The production lifter is project-owned. Ghidra provides discovery, annotation, mapping, and cross-reference assistance but does not generate shipping code.

## Hardware boundary

PPU, SPC700/S-DSP, DMA/HDMA, timers, controllers, memories, buses, and SA-1 peripheral functions remain hardware models. A deterministic cooperative scheduler advances all components on a shared master-clock timeline. Host threads are not used to run guest processors.

The initial hardware backend will adapt only license-audited Super Famicom components from pinned ares source. MesenCE remains an external trace oracle and is not linked into the shipped runtime.

## Correctness policy

An unknown decode mode, indirect target, executed RAM code mapping, or required interpreter fallback is a hard release failure. Machine evidence—not task completion or subjective observation—controls dashboard gate status.
