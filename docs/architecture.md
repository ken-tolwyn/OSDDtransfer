# OSDDtransfer Architecture (Draft v0.1)

## 1. System Context

OSDDtransfer enables controlled one-way content replication across a data diode.

**Low side (sender domain):**
- Discovery & staging of source content (OCI, RPM, Maven).
- Packaging into chunked transfer sets.
- UDP one-way transmission with parity.

**High side (receiver domain):**
- Receive, validate, and reconstruct transfer sets.
- Publish content to destination systems.
- Record audit and lifecycle outcomes.

## 2. Major Components

### 2.1 Control Plane (Management)

- Backed by **Oracle Autonomous Database**.
- UI provided by **Oracle APEX**.
- SQL lifecycle managed with **SQLcl project** artifacts.

Main responsibilities:
- Transfer agent definitions.
- UDP port assignments.
- Source and destination endpoint configuration.
- Registry/repo credentials (encrypted secret references).
- Policy definitions (retention, replay windows, retry intervals).
- Audit and health dashboards.

### 2.2 Sender Data Plane Services

1. **Collector adapters**
   - OCI adapter: registry catalog + manifest/layer resolution.
   - RPM adapter: repository metadata + package retrieval.
   - Maven adapter: POM parsing + transitive dependency resolution.

2. **Dedup index service (per transfer_agent_id)**
   - Tracks previously transferred object hashes.
   - Determines "send / skip / resend" decisions.

3. **Packager service**
   - Produces immutable transfer batches.
   - Emits manifest + chunk map + hash tree.

4. **Parity encoder**
   - Splits payload into `k` data shards + `m` parity shards.
   - Configurable profile per agent (e.g., 8+2, 16+4).

5. **UDP emitter**
   - Sends stripe packets over assigned agent UDP port.
   - Includes monotonic sequence and batch/stripe identifiers.

### 2.3 Receiver Data Plane Services

1. **UDP ingest**
   - Listens on approved per-agent ports.
   - Buffers packets by transfer session.

2. **Reconstruction and validator**
   - Rebuilds missing shards from parity.
   - Verifies shard and batch checksums.
   - Produces completion proof.

3. **Materializer adapters**
   - OCI writer (e.g., Zot).
   - Maven publisher (e.g., Nexus).
   - RPM publisher (filesystem + metadata + rsync/http delivery).

4. **Lifecycle reconciler**
   - Handles delete-if-not-seen policy.
   - Tracks stale artifacts and downstream sync status.

## 3. Protocol Outline

Each transfer batch contains:

- `batch_id`, `transfer_agent_id`, timestamp window.
- Content manifest entries with type + coordinates + checksums.
- Stripe metadata:
  - shard size
  - parity profile (`k`, `m`)
  - total stripes

Packet header fields (minimum):

- protocol version
- agent id
- batch id
- stripe id
- shard index
- shard kind (`data` / `parity`)
- payload length
- shard checksum

Receiver acceptance criteria:

1. Enough shards exist per stripe (`>= k`).
2. Stripe checksums match manifest.
3. Batch Merkle root/checksum matches signed control record.
4. Replay protection checks session uniqueness/time window.

## 4. Deduplication Strategy

### 4.1 OCI

- Dedup key: OCI layer digest (`sha256`).
- Scope: per transfer agent segment.
- Skip transmitting known layers; always transmit missing manifests/index documents as needed to reference layers.

### 4.2 RPM

- Dedup key: checksum + NEVRA + repository context.
- Send logic:
  - skip when already present and recent.
  - retransmit when deleted downstream, TTL exceeded, or policy requires refresh.

### 4.3 Maven

- Dedup key: GAV + checksum + classifier/extension.
- POM-driven pull resolves dependency graph; only missing artifacts are added to batch.

## 5. Oracle Data Model (Initial)

Core tables (conceptual):

- `transfer_agent`
- `agent_port_assignment`
- `source_endpoint`
- `destination_endpoint`
- `credential_ref`
- `policy_profile`
- `transfer_batch`
- `batch_object`
- `batch_stripe`
- `batch_shard`
- `receive_session`
- `reconstruction_event`
- `publish_event`
- `artifact_presence`

Support tables:

- `scheduler_job`
- `health_signal`
- `audit_event`

## 6. Security and Compliance Notes

- No reverse-channel dependency in the core transfer path.
- Credential material referenced via secure secret mechanism; avoid plaintext in database rows.
- Immutable transfer manifests + signed metadata for forensic traceability.
- Strict port allow-list per transfer agent.

## 7. Deployment Model

- Podman containers for sender and receiver service bundles.
- Optional Kubernetes deployment for management and destination publishing components.
- Distinct deployment units:
  - control-plane app
  - sender worker set
  - receiver worker set

## 8. Delivery Phases

1. **Phase 1 (MVP):**
   - One agent, one UDP stream, generic file transfer, parity recovery, APEX status page.

2. **Phase 2:**
   - OCI layer-aware transfer + Zot publish.

3. **Phase 3:**
   - RPM repository support with delete-if-not-seen lifecycle.

4. **Phase 4:**
   - Maven POM resolution + Nexus publish.

5. **Phase 5:**
   - Multi-agent scaling, policy tuning, observability hardening.
