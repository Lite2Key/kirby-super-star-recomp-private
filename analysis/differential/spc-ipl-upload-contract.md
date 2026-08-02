# SPC IPL upload acknowledgement contract

This is a ROM-free extraction from the ignored first-frame Mesen V1 capture.
The extractor deliberately omits every uploaded payload value. The machine-
readable result is `spc-ipl-upload-contract.json`.

## Exact start sequence

1. The IPL publishes `$AA` on output `$F4` at SPC counter 4802 and `$BB` on
   output `$F5` at 4812, then repeats `CMP $F4,#$CC` at `$FFCF` and `BNE` at
   `$FFD2`.
2. KSS writes destination `$0700` through input `$F6/$F7`, writes transfer flag
   1 through `$F5`, and finally commits start token `$CC` through `$F4`.
3. The `$CC` commit occurs after one `$FFCF` compare has produced a non-equal
   result but before its `$FFD2` branch. That branch therefore takes once. The
   next `$FFCF` compare sees `$CC`; the following branch falls through.
4. The IPL branches to `$FFEF`, installs the `$0700` destination pointer, reloads
   the start token/flag from `$F4/$F5`, and executes `MOV $F4,A` at `$FFF5`.
   This is the acknowledgement write: output `$F4` becomes `$CC` at SPC counter
   21702, before the `$FFF7` instruction boundary.

From the first post-commit SPC instruction boundary at counter 21634 through
the acknowledgement write is 68 Mesen SPC counter units, or 34 architectural
SPC cycles. This is not a master-clock latency claim: V1 logs S-CPU writes in
S-CPU counter space. The V2 capture remains necessary to bind the input commit
to an exact SPC bus-read phase and master tick.

## Byte transfer phase

After observing output `$CC`, KSS writes `(counter, payload)` through input
`($F4,$F5)`, starting with counter zero. The IPL waits for the expected counter,
checks it for stability, reads `$F5`, echoes the counter through output `$F4`,
stores the payload at `destination + counter`, and increments its expectation.
The echo—not the input write—completes one transfer transaction.

At the first `endFrame`, KSS had committed 152 contiguous counters, 0 through
151. The IPL had echoed 151 counters, 0 through 150, leaving counter 151 as the
single legitimate in-flight transaction. No payload bytes appear in either
tracked artifact.

## Scheduler acceptance rule

The runtime must order the S-CPU `$2140`/SPC `$F4` input commit against the
SPC `$FFCF` read phase. It must publish the `$FFF5` write to the independent
SPC-to-S-CPU latch before the next `$FFF7` boundary. S-CPU reads may see only
committed output-latch state. Instruction-atomic SPC stepping is sufficient for
this exact start only if no external input commit can fall between an
instruction's observable bus phases; otherwise the step must yield at the port
read/write phase. A focused extractor test locks the eight-instruction
acknowledgement path and permits no more than one pending counter at a capture
boundary.
