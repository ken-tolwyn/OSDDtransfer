# OSDDtransfer

OSDDtransfer is a Linux-first, Podman/container-focused platform for one-way data transfer across a data diode over UDP.

## What is now implemented (MVP codebase)

- Python sender and receiver services for one-way UDP transfer.
- RAIDZ-style (XOR) parity support per stripe (`k` data shards + `1` parity shard).
- File manifest with end-to-end SHA256 validation at the receiver.
- SQLcl project with Oracle schema and PL/SQL management APIs.
- Oracle APEX implementation guide wired to the packaged APIs.
- Podman container definitions for sender/receiver.

## Quick start (local)

### 1) Run tests

```bash
python -m pytest -q
```

### 2) Start receiver

```bash
python -m osddtransfer.receiver --bind-port 9001 --output-dir ./received
```

### 3) Send file from another terminal

```bash
python -m osddtransfer.sender --host 127.0.0.1 --port 9001 --agent agent-a --file ./sample.bin
```

Recovered files are written under `./received/<transfer_agent_id>/`.

## Oracle deployment

1. Connect SQLcl to your Oracle Autonomous Database schema.
2. Run:

```sql
@db/sqlcl/install.sql
```

3. Build the APEX app pages described in `db/apex/README.md`.

## Current scope and limitations

- Parity recovery currently supports a single missing data shard per stripe (`m=1`).
- Artifact dedup state is represented at DB/API layer; source adapters for OCI/RPM/Maven are next.
- Receiver currently materializes files to disk; downstream publishers (Zot/Nexus/RPM HTTP) are next.
