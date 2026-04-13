from __future__ import annotations

import argparse
import hashlib
import os
import socket
import time
import uuid
from pathlib import Path

from .manifest import FileManifest
from .parity import xor_bytes
from .protocol import PacketHeader, packet_bytes


def _chunks(raw: bytes, shard_size: int):
    for start in range(0, len(raw), shard_size):
        yield raw[start : start + shard_size]


def send_file(host: str, port: int, transfer_agent_id: str, file_path: Path, shard_size: int = 1024, k: int = 4):
    raw = file_path.read_bytes()
    file_sha = hashlib.sha256(raw).hexdigest()
    data_shards = list(_chunks(raw, shard_size))

    padding = (k - (len(data_shards) % k)) % k
    data_shards.extend([b"\x00" * shard_size for _ in range(padding)])
    data_shards = [shard.ljust(shard_size, b"\x00") for shard in data_shards]

    total_stripes = len(data_shards) // k
    batch_id = str(uuid.uuid4())
    manifest = FileManifest(
        batch_id=batch_id,
        transfer_agent_id=transfer_agent_id,
        filename=file_path.name,
        file_sha256=file_sha,
        total_size=len(raw),
        k=k,
        shard_size=shard_size,
        total_stripes=total_stripes,
    )

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        for stripe in range(total_stripes):
            base = stripe * k
            stripe_shards = data_shards[base : base + k]
            parity = xor_bytes(stripe_shards)
            for index, shard in enumerate(stripe_shards):
                header = PacketHeader(
                    version=1,
                    transfer_agent_id=transfer_agent_id,
                    batch_id=batch_id,
                    stripe_id=stripe,
                    shard_index=index,
                    k=k,
                    m=1,
                    kind="data",
                    payload_sha256=hashlib.sha256(shard).hexdigest(),
                )
                sock.sendto(packet_bytes(header, shard), (host, port))
            p_header = PacketHeader(
                version=1,
                transfer_agent_id=transfer_agent_id,
                batch_id=batch_id,
                stripe_id=stripe,
                shard_index=k,
                k=k,
                m=1,
                kind="parity",
                payload_sha256=hashlib.sha256(parity).hexdigest(),
            )
            sock.sendto(packet_bytes(p_header, parity), (host, port))

        manifest_raw = manifest.to_json_bytes()
        m_header = PacketHeader(
            version=1,
            transfer_agent_id=transfer_agent_id,
            batch_id=batch_id,
            stripe_id=-1,
            shard_index=-1,
            k=k,
            m=1,
            kind="manifest",
            payload_sha256=hashlib.sha256(manifest_raw).hexdigest(),
            final_manifest_sha256=manifest.digest(),
        )
        sock.sendto(packet_bytes(m_header, manifest_raw), (host, port))
        time.sleep(0.05)
    finally:
        sock.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="OSDDtransfer sender")
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--agent", required=True)
    parser.add_argument("--file", required=True)
    parser.add_argument("--shard-size", type=int, default=1024)
    parser.add_argument("--k", type=int, default=4)
    args = parser.parse_args()

    send_file(args.host, args.port, args.agent, Path(args.file), args.shard_size, args.k)
    print(f"sent file={os.path.basename(args.file)} to {args.host}:{args.port} agent={args.agent}")


if __name__ == "__main__":
    main()
