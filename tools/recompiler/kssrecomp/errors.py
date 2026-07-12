"""Typed failures emitted by the analysis pipeline."""


class RecompilerError(Exception):
    """Base class for deterministic, user-actionable analysis failures."""


class RomFormatError(RecompilerError):
    """The input cannot be interpreted as a supported SNES ROM image."""


class DecodeError(RecompilerError):
    """An instruction cannot be decoded safely."""


class UnsupportedOpcodeError(DecodeError):
    """An opcode is not implemented by the current decoder."""


class AmbiguousModeError(DecodeError):
    """Runtime processor state is required to decode or transition safely."""
