"""SNES ROM identity and internal-header inspection without payload export."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import hashlib
import zlib

from .errors import RomFormatError


COPIER_HEADER_SIZE = 512
HEADER_SIZE = 64
HEADER_CANDIDATES = (0x7FC0, 0xFFC0, 0x40FFC0)


@dataclass(frozen=True)
class RomIdentity:
    size: int
    payload_size: int
    copier_header: bool
    crc32: str
    md5: str
    sha1: str
    sha256: str


@dataclass(frozen=True)
class InternalHeader:
    file_offset: int
    title: str
    map_mode: int
    cartridge_type: int
    rom_size_code: int
    ram_size_code: int
    destination_code: int
    version: int
    checksum_complement: int
    checksum: int
    checksum_pair_valid: bool
    reset_vector: int


@dataclass(frozen=True)
class RomInspection:
    identity: RomIdentity
    header: InternalHeader

    def to_dict(self) -> dict[str, object]:
        return {"identity": asdict(self.identity), "header": asdict(self.header)}


def _strip_copier_header(data: bytes) -> tuple[bytes, bool]:
    has_header = len(data) % 0x8000 == COPIER_HEADER_SIZE
    return (data[COPIER_HEADER_SIZE:], True) if has_header else (data, False)


def identify(data: bytes) -> RomIdentity:
    payload, copier_header = _strip_copier_header(data)
    if not payload or len(payload) % 0x8000:
        raise RomFormatError("ROM payload size must be a non-zero multiple of 32 KiB")
    return RomIdentity(
        size=len(data),
        payload_size=len(payload),
        copier_header=copier_header,
        crc32=f"{zlib.crc32(payload) & 0xFFFFFFFF:08X}",
        md5=hashlib.md5(payload).hexdigest().upper(),  # nosec: identity, not security
        sha1=hashlib.sha1(payload).hexdigest().upper(),  # nosec: identity, not security
        sha256=hashlib.sha256(payload).hexdigest().upper(),
    )


def _decode_title(raw: bytes) -> str:
    return raw.decode("ascii", errors="replace").rstrip(" \x00")


def _candidate_score(payload: bytes, offset: int) -> int:
    if offset + HEADER_SIZE > len(payload):
        return -1
    h = payload[offset : offset + HEADER_SIZE]
    complement = int.from_bytes(h[0x1C:0x1E], "little")
    checksum = int.from_bytes(h[0x1E:0x20], "little")
    score = 4 if (checksum ^ complement) == 0xFFFF else 0
    score += 2 if h[0x15] & 0x0F in (0, 1, 2, 3, 5) else 0
    score += 1 if int.from_bytes(h[0x3C:0x3E], "little") >= 0x8000 else 0
    score += sum(0x20 <= byte <= 0x7E or byte == 0 for byte in h[:21]) // 7
    return score


def inspect(data: bytes) -> RomInspection:
    identity = identify(data)
    payload, _ = _strip_copier_header(data)
    candidates = [offset for offset in HEADER_CANDIDATES if offset + HEADER_SIZE <= len(payload)]
    if not candidates:
        raise RomFormatError("ROM is too small to contain a recognized SNES internal header")
    offset = max(candidates, key=lambda item: (_candidate_score(payload, item), -item))
    h = payload[offset : offset + HEADER_SIZE]
    complement = int.from_bytes(h[0x1C:0x1E], "little")
    checksum = int.from_bytes(h[0x1E:0x20], "little")
    header = InternalHeader(
        file_offset=offset,
        title=_decode_title(h[:21]),
        map_mode=h[0x15],
        cartridge_type=h[0x16],
        rom_size_code=h[0x17],
        ram_size_code=h[0x18],
        destination_code=h[0x19],
        version=h[0x1B],
        checksum_complement=complement,
        checksum=checksum,
        checksum_pair_valid=(checksum ^ complement) == 0xFFFF,
        reset_vector=int.from_bytes(h[0x3C:0x3E], "little"),
    )
    return RomInspection(identity=identity, header=header)
