# Platform support and delivery roadmap

This page describes what the public repository can build today and what still
has to be proven before distributing a playable native package. The current
runtime is a ROM-free research build at a bounded diagnostic frontier; it is
not yet a complete or playable Kirby Super Star port.

## Current support matrix

| Target | What is available now | Evidence and limits |
|---|---|---|
| Windows native | `kss-native` builds with the Win32/GDI host. The host presents the current `256 x 239` surface, accepts keyboard input, and maps up to two XInput pads. | The Windows build and native test suite pass. The executable still stops at the current translated frontier and returns the documented development-frontier status; it is not a release candidate or a gameplay-complete port. |
| Windows headless | `kss-recomp` runs the portable command-line probe and can write a first-frame BMP when the bounded runtime reaches that surface. | A verified ROM is supplied at runtime. The optional SPC700 IPL is also supplied at runtime as exactly 64 bytes. No ROM, IPL, save, trace, or capture is part of the repository or a future package. |
| Linux headless | The portable `kss-recomp` target is configured, built, and tested by the Ubuntu CI lane through the `linux-ninja` preset. | Linux currently has no native window, input, or audio backend. This is a compile/test portability lane, not a playable Linux build. |
| AppImage | Not shipped. | No Linux host backend, install/package metadata, or AppImage build pipeline exists yet. |
| Flatpak | Not shipped. | No manifest, desktop metadata, runtime policy, or sandboxed external-ROM/save design exists yet. |

The shared runtime models parts of the PPU, SPC700/DSP, controllers, and save
boundary, but host audio output remains future work. A passing build or a
repeatable first-frame diagnostic is not evidence of full gameplay parity.

## External data boundary

The project deliberately keeps copyrighted/reference material outside the
public tree and outside any distributable package:

- The user supplies the verified external Kirby Super Star ROM with `--rom`.
- The optional authentic SNES SPC700 IPL is supplied with `--spc-ipl` and must
  be exactly 64 bytes. It is read transiently and is never copied into the
  repository or generated output.
- Save data is supplied or created through `--save`; when omitted, the current
  runtime derives an `.srm` path beside the ROM. The development frontier does
  not commit a save because the run has not reached a clean shutdown.
- ROMs, IPL images, saves, extracted art/audio, traces, screenshots, Ghidra
  projects, and private generated output remain prohibited package contents.

Any release job must run the asset-boundary check on the source checkout and
separately inspect the staged package contents. The ROM and IPL must remain
user-provided runtime inputs rather than package files.

## Building the current diagnostic targets

On Windows, the public build uses the `windows-msvc` CMake preset:

```text
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

On Linux, the CI-tested headless target uses the `linux-ninja` preset:

```text
cmake --preset linux-ninja
cmake --build --preset linux-ninja-debug
ctest --preset linux-ninja-debug
```

The Windows helper scripts are convenience scripts for the current local
environment, not a cross-platform release interface. In particular, the
native Windows build helper currently assumes a locally installed Visual
Studio Build Tools path. A release lane must discover or pin its toolchain
without relying on that machine-specific path.

## Staged delivery roadmap

### 1. Windows release candidate

Before packaging the current Windows host, add a reproducible x64 Release
configuration, make compiler/runtime dependencies explicit, and exercise the
executable on a clean machine. The acceptance run must use an external ROM,
optionally an external 64-byte IPL, exercise the save path, and verify that no
protected/reference data entered the package. This gate follows gameplay and
hardware-parity work; the current diagnostic frontier is not a release claim.

### 2. Shared native host

Keep the portable `NativeHostPlatform` session boundary and provide a
platform-neutral native backend for window presentation, controller input, and
audio. SDL3 is the pinned candidate for this layer, but it has not been
imported or linked into the public runtime. The existing Win32 host can remain
an explicit compatibility path while the shared backend is validated.

The backend must preserve the runtime's deterministic clock ownership: host
polling and presentation may consume supplied frame boundaries, but must not
advance guest clocks or invent missing frames. Audio needs the same explicit
boundary before it can be advertised as supported.

### 3. Linux AppImage

After the shared host works on Linux, build the executable in a pinned
environment and add install metadata, bundled-library/RPATH rules, desktop and
icon assets, and a versioned AppImage artifact. The package should contain the
runtime and notices only; the launcher must continue to request the user's
external ROM and optional IPL. Clean-machine tests should cover launch,
controller input, first-frame presentation, save handling, and a nonzero
diagnostic exit without silently writing an incomplete save.

### 4. Linux Flatpak

Add a pinned Flatpak runtime/SDK manifest only after the Linux host contract is
stable. The manifest must define an application ID, desktop entry, icons,
licenses, and the minimum device/display permissions. Because the ROM and IPL
live outside the bundle, the UI/launcher must use a deliberate file-access
strategy (portal or narrowly scoped permissions) rather than assuming that an
arbitrary host path is visible inside the sandbox. Save data must have a
documented XDG user-data location and pass the same clean-shutdown checks as
the unsandboxed build.

### 5. Package acceptance and publication

AppImage and Flatpak are separate delivery gates. Each must be built from a
clean checkout, pass the Python/native tests and asset-boundary check, launch
with a user-provided external ROM, verify the save lifecycle, publish checksums
and third-party notices, and state the exact supported diagnostic/gameplay
frontier. No package should be published as a playable port until the
corresponding parity gates are complete.

The progress dashboard tracks these as later Windows-release and Linux-delivery
milestones; this document records their prerequisites rather than marking
either milestone complete.
