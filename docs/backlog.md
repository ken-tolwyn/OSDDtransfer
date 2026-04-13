# OSDDtransfer Implementation Backlog

## Epic 1: Foundation

- [ ] Establish mono-repo conventions and service directory layout.
- [ ] Add SQLcl project structure for schema migrations and seed data.
- [ ] Define local development stack with Podman Compose.
- [ ] Add CI checks for linting, tests, and schema verification.

## Epic 2: Control Plane

- [ ] Create Oracle schema objects for transfer agents, ports, policies, batches, and audit.
- [ ] Build APEX app pages for:
  - [ ] Agent CRUD
  - [ ] Port assignment
  - [ ] Endpoint/credential management
  - [ ] Transfer monitoring dashboard
- [ ] Implement service API for management operations.

## Epic 3: UDP + Parity Transport

- [ ] Define wire protocol structures and versioning.
- [ ] Implement sender chunker and parity encoder.
- [ ] Implement receiver shard store and reconstruction logic.
- [ ] Add end-to-end integrity validation and replay protection.

## Epic 4: OCI Support

- [ ] Implement registry auth profiles per agent (multiple accounts).
- [ ] Build OCI discovery and manifest/layer extraction.
- [ ] Deduplicate layers per transfer agent.
- [ ] Receiver publish adapter for Zot.

## Epic 5: RPM Support

- [ ] Implement YUM/DNF metadata fetch and package selection.
- [ ] Dedup/refresh policy based on seen/deleted state.
- [ ] Receiver publication workflow for HTTP(S)+rsync targets.

## Epic 6: Maven Support

- [ ] Implement POM ingestion and dependency resolution.
- [ ] Artifact dedup by GAV+checksum.
- [ ] Receiver publish adapter for Nexus.

## Epic 7: Operations

- [ ] Add metrics, logs, and tracing for all transfer stages.
- [ ] Add disaster recovery runbooks.
- [ ] Add hardening checklist for regulated deployments.
