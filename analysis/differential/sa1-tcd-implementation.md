# SA-1 TCD slice implementation recommendation

The private MesenCE oracle begins at SA-1 `$00:8C36` and stops before executing
the next unsupported identity, JML at `$00:8C60`. The committed JSON contains
only identities, clocks, semantic labels, counts, and SHA-256 chains.

## TCD (`$5B`)

Implement TCD as an implied, operand-free instruction:

1. Copy the full 16-bit accumulator into the 16-bit direct-page register.
2. Set `N` from bit 15 of the copied value.
3. Set `Z` when the copied 16-bit value is zero.
4. Preserve A, X, Y, SP, DBR, E, and every status bit other than N/Z.
5. Advance PC by one byte and add two architectural cycles.
6. Perform no data-bus read or write.

At this exact SA-1 reset identity, MesenCE measured two cycles from TCD to the
next instruction: two base cycles and zero observed arbitration wait. This is
evidence for `$00:8C36`, not a universal SA-1 wait-state rule.

After TCD, the already-supported LDA/STA sequence emits sixteen ordered SA-1
IRAM writes. Execution then polls at `$00:8C58/$00:8C5B`. The scripted reference
spent 13,742,587 cycles between the first BPL boundary and the STZ exit. Treat
that duration as scenario scheduling evidence, not instruction timing: repeated
poll iterations experience changing SA-1 arbitration.

STZ at `$00:8C5D` measured five cycles to `$00:8C60`, consistent with a 16-bit
absolute store and no extra wait in this observation.

## Next frontier: JML (`$5C`)

The capture stops before JML executes, so its state transition and SA-1 wait
component are not reference-verified here. The architectural implementation
should consume a 24-bit little-endian target, replace PBR:PC with that target,
preserve registers and flags, and use the standard four base cycles. Keep any
SA-1 wait addition evidence-bound until a post-JML instruction boundary is
captured.
