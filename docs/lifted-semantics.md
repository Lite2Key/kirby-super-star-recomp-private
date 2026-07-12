# Lifted instruction semantics v1

`execute_lifted` is the first architecture-state execution boundary between the
Python decoder and generated C++ blocks. It consumes decoded bytes, never a ROM,
and makes PC, flags, mode, cycle count, and bus writes explicit.

The v1 implementation covers 30 opcodes: `NOP`, `CLC`, `SEC`, `CLI`, `SEI`,
`CLV`, `CLD`, `SED`, `XCE`, `REP`, `SEP`, immediate `LDA`/`LDX`/`LDY`, absolute
`STA`/`STX`/`STY`/`STZ`, `BRA`, and absolute `JMP`. Both 8- and 16-bit M/X paths
are tested where applicable. The real S-CPU reset path additionally lifts `TXS`,
`PHK`, `PLB`, `PEA`, `PLD`, and direct-page `STA`/`STZ`, including stack bus order,
emulation/native stack wrapping, direct-page penalties, and NZ effects. Invalid
operand widths and unsupported opcodes are rejected without mutating CPU or bus
state.

The first complete reset blocks additionally use absolute `LDA`, `BPL`, and
restartable `MVN`. Generated functions validate each successor's processor,
24-bit PC, and E/M/X identity before continuing.

The JSON differential-vector schema is intentionally strict and records the
reference kind. Architecture-contract vectors and the game-specific MesenCE
reset-prefix corpus are stored separately, so manual-derived and emulator-derived
evidence cannot be confused.
