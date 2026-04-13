from __future__ import annotations

import argparse
import hashlib
import socket
from collections import defaultdict
from pathlib import Path

from .manifest import FileManifest
from .parity import reconstruct_single_missing
from .protocol import parse_packet


class BatchState:
    def __init__(self, k: int):
        self.k = k
        self.data: dict[int, list[bytes | None]] = defaultdict(lambda: [None] * k)
        self.parity: dict[int, bytes] = {}
        self.manifest: FileManifest | None = None


def run_receiver(bind_host: str, bind_port: int, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    state_by_batch: dict[str, BatchState] = {}

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((bind_host, bind_port))
    print(f"receiver listening on {bind_host}:{bind_port}")

    while True:
        datagram, _addr = sock.recvfrom(65535)
        header, payload = parse_packet(datagram)

        if header.kind == "manifest":
            manifest = FileManifest.from_json_bytes(payload)
            batch = state_by_batch.get(manifest.batch_id)
            if batch is None:
                batch = BatchState(k=manifest.k)
                state_by_batch[manifest.batch_id] = batch
            batch.manifest = manifest
            _flush_batch(manifest.batch_id, batch, output_dir)
            continue

        batch = state_by_batch.get(header.batch_id)
        if batch is None:
            batch = BatchState(k=header.k)
            state_by_batch[header.batch_id] = batch

        if header.kind == "data":
            batch.data[header.stripe_id][header.shard_index] = payload
        elif header.kind == "parity":
            batch.parity[header.stripe_id] = payload


def _flush_batch(batch_id: str, batch: BatchState, output_dir: Path) -> None:
    manifest = batch.manifest
    if manifest is None:
        return

    stripes = []
    for stripe_id in range(manifest.total_stripes):
        data = batch.data.get(stripe_id, [None] * manifest.k)
        parity = batch.parity.get(stripe_id)
        if parity is None:
            return
        repaired = reconstruct_single_missing(data, parity)
        stripes.extend(repaired)

    raw = b"".join(stripes)[: manifest.total_size]
    digest = hashlib.sha256(raw).hexdigest()
    if digest != manifest.file_sha256:
        raise ValueError(f"Manifest checksum mismatch for batch {batch_id}")

    target = output_dir / manifest.transfer_agent_id
    target.mkdir(exist_ok=True, parents=True)
    out_file = target / manifest.filename
    out_file.write_bytes(raw)
    print(f"received {manifest.filename} into {out_file}")


def main() -> None:
    parser = argparse.ArgumentParser(description="OSDDtransfer receiver")
    parser.add_argument("--bind-host", default="0.0.0.0")
    parser.add_argument("--bind-port", type=int, required=True)
    parser.add_argument("--output-dir", default="./received")
    args = parser.parse_args()

    run_receiver(args.bind_host, args.bind_port, Path(args.output_dir))


if __name__ == "__main__":
    main()
