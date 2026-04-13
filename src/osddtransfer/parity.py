from __future__ import annotations

from typing import Iterable


def xor_bytes(buffers: Iterable[bytes]) -> bytes:
    data = list(buffers)
    if not data:
        raise ValueError("No buffers supplied")
    width = len(data[0])
    if any(len(item) != width for item in data):
        raise ValueError("All buffers must have equal length")
    out = bytearray(width)
    for buf in data:
        for i, value in enumerate(buf):
            out[i] ^= value
    return bytes(out)


def reconstruct_single_missing(shards: list[bytes | None], parity_shard: bytes) -> list[bytes]:
    missing = [i for i, shard in enumerate(shards) if shard is None]
    if len(missing) > 1:
        raise ValueError("Cannot recover more than one missing shard with XOR parity")

    if not missing:
        return [s for s in shards if s is not None]

    candidate = xor_bytes([parity_shard, *[s for s in shards if s is not None]])
    recovered = shards[:]
    recovered[missing[0]] = candidate
    return [s for s in recovered if s is not None]
