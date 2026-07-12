# M2 reset-prefix lifting evidence

- Lifted runtime semantics implemented: 27 of 256 opcodes
- Architecture-contract vector corpus: 5 vectors covering state, widths, cycles, and ordered bus writes
- Game-specific emulator corpus: `tests/runtime/kss_reset_prefix_vectors.json`
- MesenCE-matched S-CPU prefix: 16 consecutive instructions from `00:8004` through arrival at `00:8020`
- Compared fields: PC, A, X, Y, D, S, PBR, DBR, P, E, cycles, and ordered writes
- Result: all 16 prefix transitions match through cycle 48, including stack writes at `00:1FFF/00:1FFE` and direct-page writes at `00:2100/00:2101`
- SA-1 measured timing prefix: 18 instructions from `00:8BF4` through arrival at `00:8C20`
- SA-1 timing decomposition: 57 architectural base cycles plus 39 observed wait/arbitration cycles equals the 96-cycle Mesen interval
- Private differential oracle captured: 256 S-CPU states, 256 SA-1 states, and 19,931 write observations
- Raw oracle location: ignored `.private/traces/reset-differential-reference.log`

No complete game basic block is claimed as verified yet: the observed S-CPU
reset block continues beyond `00:8020`. SA-1 wait timing is evidence-bound to
the measured prefix and is not yet a general contention model.
