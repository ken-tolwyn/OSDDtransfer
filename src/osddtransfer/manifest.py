from __future__ import annotations

import dataclasses
import hashlib
import json


@dataclasses.dataclass(slots=True)
class FileManifest:
    batch_id: str
    transfer_agent_id: str
    filename: str
    file_sha256: str
    total_size: int
    k: int
    shard_size: int
    total_stripes: int

    def to_json_bytes(self) -> bytes:
        return json.dumps(dataclasses.asdict(self), separators=(",", ":")).encode("utf-8")

    @staticmethod
    def from_json_bytes(data: bytes) -> "FileManifest":
        return FileManifest(**json.loads(data.decode("utf-8")))

    def digest(self) -> str:
        return hashlib.sha256(self.to_json_bytes()).hexdigest()
