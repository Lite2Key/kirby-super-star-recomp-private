# Third-party boundary

No third-party source has been imported into the authored runtime yet.

## External-only analysis tools

- Ghidra 12.1.2 and `ghidra-snes` 1.3.0: local, ignored analysis installation.
- MesenCE 2.2.1: GPL-3.0 external trace oracle; never linked or copied into the runtime.
- GitHub CLI 2.96.0 and Temurin JDK 21.0.11+10: local bootstrap tools.

## Pinned runtime-source candidates

- ares v148, commit `0aafd85789215e84e1e43415c07d4c88461b7899`: cloned locally for a file-by-file license and dependency audit. No source imported yet.
- SDL 3.4.8, commit `d9d5536704d585616d4db3c8ba3c4ff6fc2757e1`: cloned locally for the future host window/input/audio layer. No source imported yet.

Any imported third-party file must be listed here with its original path, license, local destination, modifications, and required notices before it enters a commit.
