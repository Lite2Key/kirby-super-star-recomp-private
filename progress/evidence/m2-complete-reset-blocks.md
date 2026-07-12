# M2 complete reset-block proof

- Generated manifest: `analysis/cfg/bootstrap-dual.generated-blocks.json`
- Value-free private-oracle summary: `analysis/differential/reset-block-reference.json`
- Generated registration: 183 exact processor/PC/E/M/X identities
- S-CPU generated coverage: 160 nodes
- SA-1 generated coverage: 23 nodes

## S-CPU first reset block

- Entry: `00:8004`
- Terminator: `BPL` at `00:8170`
- Taken-edge checkpoint: `00:816D`
- Executed instructions: 160
- Final architectural cycle: 516, matching MesenCE
- Direct CPU writes: 124 in the lifted test
- Final state match: A, X, Y, direct page, stack, P, E, PBR, and DBR
- The raw Mesen write stream also contains 16,458 DMA/hardware side-effect writes; those remain an M3 hardware-backend obligation and are not misreported as CPU instruction writes

## SA-1 first reset block

- Entry: `00:8BF4`
- Restartable terminator: `MVN` at `00:8C20`
- Exit checkpoint: `00:8C23`
- Generated dispatches: 2,065, including 2,047 MVN iterations
- Final state: A=`FFFF`, X=`37FF`, Y=`3800`, DBR=`00`
- Ordered instruction writes: 2,055
- Architectural cycles: 14,386
- Evidence-bound SA-1 waits: 6,171
- Observed total: 20,557 cycles, matching MesenCE

The generated runner validates every successor identity and fails closed on
unknown blocks, unsupported semantics, malformed transitions, or step limits.
