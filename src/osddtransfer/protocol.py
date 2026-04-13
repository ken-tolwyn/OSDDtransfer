from __future__ import annotations

import dataclasses
import hashlib
import json
import struct
from typing import Final

MAGIC: Final[bytes] = b"OSDD"
VERSION: Final[int] = 1
HEADER_LEN_STRUCT: Final[str] = "!I"
HEADER_LEN_SIZE: Final[int] = struct.calcsize(HEADER_LEN_STRUCT)


@dataclasses.dataclass(slots=True)
class PacketHeader:
    version: int
    transfer_agent_id: str
    batch_id: str
    stripe_id: int
    shard_index: int
    k: int
    m: int
    kind: str
    payload_sha256: str
    final_manifest_sha256: str | None = None

    def to_bytes(self) -> bytes:
        return json.dumps(dataclasses.asdict(self), separators=(",", ":")).encode("utf-8")

    @staticmethod
    def from_bytes(data: bytes) -> "PacketHeader":
        payload = json.loads(data.decode("utf-8"))
        return PacketHeader(**payload)


def packet_bytes(header: PacketHeader, payload: bytes) -> bytes:
    header_raw = header.to_bytes()
    return MAGIC + struct.pack(HEADER_LEN_STRUCT, len(header_raw)) + header_raw + payload


def parse_packet(datagram: bytes) -> tuple[PacketHeader, bytes]:
    if len(datagram) < len(MAGIC) + HEADER_LEN_SIZE:
        raise ValueError("Datagram too short")
    magic = datagram[: len(MAGIC)]
    if magic != MAGIC:
        raise ValueError("Invalid packet magic")
    header_len = struct.unpack(HEADER_LEN_STRUCT, datagram[len(MAGIC) : len(MAGIC) + HEADER_LEN_SIZE])[0]
    start = len(MAGIC) + HEADER_LEN_SIZE
    end = start + header_len
    header = PacketHeader.from_bytes(datagram[start:end])
    payload = datagram[end:]
    if hashlib.sha256(payload).hexdigest() != header.payload_sha256:
        raise ValueError("Payload checksum mismatch")
    return header, payload
