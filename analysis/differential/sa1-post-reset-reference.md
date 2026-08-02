# SA-1 post-reset reference evidence

The sanitized first-frame Mesen execution trace establishes the complete
20-identity SA-1 route after the reset reference ends:

`$8C26, $8C29, $8C2C, $8C2F, $8C32, $8C33, $8C36, $8C37, $8C3A, $8C3D, $8C40, $8C43, $8C46, $8C49, $8C4C, $8C4F, $8C52, $8C55, $8C58, $8C5B`

All identities remain in native 16-bit accumulator/index mode. The committed
JSON exports only those ROM-free identities, aggregate counts, and SHA-256
chains. It does not export trace records, opcodes, operands, register values,
bus addresses, write values, or cartridge bytes.

The evidence has two deliberately separate tiers:

- **Architectural-state/write reference: 14 identities.** The existing private
  TCD-to-poll slice has per-instruction state records for `$8C36` through
  `$8C5B`, plus a write chain. Only their digests and counts are committed.
- **Identity/order reference only: 6 identities.** `$8C26, $8C29, $8C2C,
  $8C2F, $8C32, $8C33` occur in the Mesen identity route, but the available
  private captures do not contain per-instruction architectural state for them.

Accordingly, the full 20 may be shown as reference-observed at the identity and
order layer, but only 14 should count as architectural-state/write-chain
reference verified. A new narrow Mesen state capture spanning `$8C26` through
`$8C36` would close the remaining higher-tier six without recapturing the rest
of the frame.

Reproducibility check (private inputs remain ignored):

```powershell
$env:PYTHONPATH = 'tools'
.venv\Scripts\python.exe -m differential.kss_diff.sa1_tail `
  .private\traces\first-frame.log `
  .private\differential\sa1-after-tcd.log `
  analysis\differential\sa1-post-reset-reference.json --check
```
