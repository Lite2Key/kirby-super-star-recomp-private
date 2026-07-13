# SPC700 implemented-opcode inventory

This inventory describes architecture implemented in `Spc700Core`; it is not a
claim that Kirby Super Star reaches or verifies these instructions. Semantics
and cycle counts were audited against the local ISC-licensed ares SPC700 core.
The explicit total is **256 of 256 opcodes**.

## Eight-bit arithmetic and logic

Each row implements every listed addressing-mode suffix. `dp,dp`, `dp,#imm`,
and `(X),(Y)` modify the left/destination operand except for CMP, which only
sets flags.

| Family | Opcodes |
| --- | --- |
| OR | `04 05 06 07 08 09 14 15 16 17 18 19` |
| AND | `24 25 26 27 28 29 34 35 36 37 38 39` |
| EOR | `44 45 46 47 48 49 54 55 56 57 58 59` |
| CMP | `64 65 66 67 68 69 74 75 76 77 78 79` |
| ADC | `84 85 86 87 88 89 94 95 96 97 98 99` |
| SBC | `A4 A5 A6 A7 A8 A9 B4 B5 B6 B7 B8 B9` |

Suffix modes are, in order: `A,dp`; `A,abs`; `A,(X)`; `A,[dp+X]`; `A,#imm`;
`dp,dp`; `A,dp+X`; `A,abs+X`; `A,abs+Y`; `A,[dp]+Y`; `dp,#imm`; `(X),(Y)`.

Word/special arithmetic: `5A CMPW`, `7A ADDW`, `9A SUBW`, `9E DIV`, `9F XCN`,
`BE DAS`, `CF MUL`, and `DF DAA`.

## Calls, returns, stack, vectors, and control transfer

- TCALL 0-15: `01 11 21 31 41 51 61 71 81 91 A1 B1 C1 D1 E1 F1`
- Stack: `0D PUSH PSW`, `2D PUSH A`, `4D PUSH X`, `6D PUSH Y`, `8E POP PSW`,
  `AE POP A`, `CE POP X`, `EE POP Y`
- Interrupt/subroutine: `0F BRK`, `3F CALL`, `4F PCALL`, `6F RET`, `7F RETI`
- Jump/branch: `1F JMP [abs+X]`, `2F BRA`, `5F JMP abs`, `D0 BNE`
- Flags/control: `00 NOP`, `60 CLRC`, `80 SETC`, `A0 EI`, `C0 DI`, `E0 CLRV`,
  `ED NOTC`

## IPL-upload load/store subset

`1D DEC X`, `5D MOV X,A`, `7E CMP Y,dp`, `8F MOV dp,#imm`, `BA MOVW YA,dp`,
`BD MOV SP,X`, `C4 MOV dp,A`, `C6 MOV (X),A`, `CB MOV dp,Y`, `CD MOV X,#imm`,
`D7 MOV [dp]+Y,A`, `DA MOVW dp,YA`, `DD MOV A,Y`, `E4 MOV A,dp`,
`E8 MOV A,#imm`, `EB MOV Y,dp`, `FC INC Y`.

## Expanded move and compare families

- A loads: `7D BF E5 E6 E7 F4 F5 F6 F7`; A stores: `AF C5 C7 D4 D5 D6`
- X loads/transfers: `9D E9 F8 F9`; X stores: `C9 D8 D9`
- Y loads/transfers: `8D EC FB FD`; Y stores: `CC DB`
- Direct copy: `FA MOV dp,dp`
- Register compare: `1E 3E C8 CMP X`; `5E 7E AD CMP Y`

## Branch, bit, shift, and increment families

- Conditional branches: `10 30 50 70 90 B0 D0 F0`; compare/decrement branches:
  `2E DE 6E FE`; unconditional `2F`
- SET1/CLR1 for bits 0-7: `02 12 22 32 42 52 62 72 82 92 A2 B2 C2 D2 E2 F2`
- BBS/BBC for bits 0-7: `03 13 23 33 43 53 63 73 83 93 A3 B3 C3 D3 E3 F3`
- ASL: `0B 0C 1B 1C`; ROL: `2B 2C 3B 3C`; LSR: `4B 4C 5B 5C`;
  ROR: `6B 6C 7B 7C`
- Word INC/DEC: `1A 3A`; byte INC/DEC: `1D 3D 8B 8C 9B 9C AB AC BB BC DC FC`

## Bit carry, direct-page controls, and halts

- Carry/absolute-bit operations: `0A OR1`, `2A OR1 /bit`, `4A AND1`,
  `6A AND1 /bit`, `8A EOR1`, `AA MOV1 C,bit`, `CA MOV1 bit,C`, `EA NOT1`
- Absolute test-and-modify: `0E TSET1`, `4E TCLR1`
- Direct-page controls: `20 CLRP`, `40 SETP`
- Architectural halts: `EF SLEEP` returns `sleeping`; `FF STOP` returns
  `stopped`. Both consume three cycles, retain their halt state, and return the
  same terminal status without consuming more cycles on subsequent steps.

## Deliberate boundary

No opcode is classified as `unsupported_opcode`. The implemented `$F0-$FF`
boundary now includes TEST/CONTROL, DSPADDR/DSPDATA, four CPU ports, two
auxiliary latches, three timer targets, and three read-clear timer outputs.
DSPDATA exposes a 128-byte register latch (bit-7 addresses are read mirrors and
ignore writes); this is not a DSP synthesis, envelope, BRR, or voice-timing
claim. Timer 0/1 advance every 128 retired SPC cycles and timer 2 every 16;
target zero means 256 ticks and outputs wrap at four bits.

TEST accepts timer gating with normal writable/enabled RAM. Requests for RAM
disable, RAM write-protect, or non-default wait states stop the current
instruction with `unsupported_io`; TEST writes are ignored while P is set.
Timer/DSP register I/O itself is executable.

Runtime provisioning of the 64-byte IPL remains mandatory; no IPL bytes are
embedded. External IRQ/NMI scheduling is not implemented—BRK/RETI only provide
their architectural stack/vector behavior.

Native tests exhaust every input pair for the six immediate ALU opcodes (and
both carry inputs for ADC/SBC), execute all 72 ALU addressing opcodes, cover
stack-pointer wrap, all 16 TCALL vectors, BRK/RETI restoration, word carry and
borrow boundaries, multiply, divide-by-zero hardware behavior, and decimal
adjust boundaries. They also execute every opcode in the expanded families with
exact instruction-cycle assertions and check representative move, bit, branch,
and shift state transitions.
An exhaustive dispatch-classification test steps every value from `00` through
`FF` and proves 254 normal executions, one SLEEP, one STOP, and zero unsupported
opcode classifications.
Focused I/O tests cover DSP mirrors and ignored writes, auxiliary/control
latches, timer enable transitions, exact 128/16-cycle divisors, read-clear
outputs, target-zero wrap, TEST timer gating, and fail-closed TEST modes.
