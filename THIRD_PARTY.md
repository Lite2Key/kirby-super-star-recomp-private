# Third-party boundary

Third-party-informed runtime code is isolated and attributed below.

## External-only analysis tools

- Ghidra 12.1.2 and `ghidra-snes` 1.3.0: local, ignored analysis installation.
- MesenCE 2.2.1: GPL-3.0 external trace oracle; never linked or copied into the runtime.
- GitHub CLI 2.96.0 and Temurin JDK 21.0.11+10: local bootstrap tools.

## Imported/adapted runtime source

- The focused `Spc700Core` in `include/kss/spc700.hpp` and
  `src/runtime/spc700.cpp` is a dependency-free rewrite informed by ares v148
  `ares/component/processor/spc700/{instruction,instructions,algorithms,memory}.cpp`
  and `ares/sfc/smp/{memory,io}.cpp`. License: ISC. Local modifications reduce
  the implementation to the observed first-frame opcode/port boundary and add
  fail-closed handling. Required notice: `licenses/ares-ISC.txt`.

## Pinned runtime-source candidates

- ares v148, commit `0aafd85789215e84e1e43415c07d4c88461b7899`: cloned locally; the SPC700/SFC SMP adaptation above is the first audited import.
- SDL 3.4.8, commit `d9d5536704d585616d4db3c8ba3c4ff6fc2757e1`: cloned locally for the future host window/input/audio layer. No source imported yet.

Any imported third-party file must be listed here with its original path, license, local destination, modifications, and required notices before it enters a commit.
