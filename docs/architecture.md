# OSDD artifact repository architecture

## Purpose and trust boundaries

OSDD is a Kubernetes-native artifact repository for two sites joined by a commercial one-way data diode. The diode is an untrusted byte-stream carrier, not a repository protocol. A connected-side publisher may use bounded, disposable spool space; the isolated receiver owns the durable repository.

The first implementation milestone deliberately concentrates on the durable model: CAS, immutable generation manifests, a rebuildable SQLite projection, and a small C++ API. Ecosystem adapters, transfer framing, Kubernetes reconciliation, and scanner Jobs build on these interfaces rather than introducing alternate stores.

## Authoritative model

Three layers have intentionally different responsibilities:

1. **CAS holds bytes.** An object with digest `D` is stored at `cas/sha256/D[0:2]/D`. Files are verified while ingesting, installed by atomic rename, and never mutated.
2. **Manifests hold meaning.** Each immutable repository generation maps a normalized logical path to a SHA-256 digest and describes logical components. A `CURRENT` pointer is atomically replaced only after every referenced object is present. Hostnames and deployment URLs are excluded.
3. **SQLite answers queries.** It is a transactionally rebuilt materialized view. It may be deleted at any time and reconstructed from committed manifests and CAS validation.

This distinction prevents Maven `.m2`, npm cache directories, OCI layouts, NGINX paths, and SQLite rows from becoming accidental sources of truth.

## Manifest format and atomic publication

Generation files use deterministic JSON Lines (`manifest.v1.jsonl`). The first record is a header; subsequent records are sorted mappings. JSONL keeps rebuilds streamable for large repositories while each generation remains immutable. Logical paths have the form `ecosystem/repository/path`; absolute paths and `.` or `..` segments are rejected.

Publication follows this order:

1. validate the repository and generation identifiers;
2. normalize and sort mappings and reject duplicate paths;
3. validate every lowercase SHA-256 digest and confirm the CAS object exists;
4. write and fsync a temporary generation file;
5. atomically rename it to the immutable generation path;
6. atomically replace `CURRENT`.

A generation that fails validation never becomes visible. Reusing a generation name is idempotent only when its bytes are identical.

## API milestone

`osdd-repository serve` provides read-only endpoints:

- `GET /healthz`
- `GET /api/v1/repositories/{repository}/paths/{logical-path}`
- `GET /api/v1/repositories/{repository}/sbom?format=cyclonedx|spdx`
- `GET /cas/sha256/{digest}` (development only; production adapters authorize and return `X-Accel-Redirect`)

The SBOM endpoint currently emits a deterministic inventory skeleton from logical component identifiers. Scanner outputs will be referenced by future manifest records and merged here; they will not be synthesized per CAS file.

## Protocol adapters

- **Maven:** native Maven Resolver Jobs populate disposable `.m2` directories. The adapter maps each POM, JAR, sources JAR, checksum, or metadata path independently to CAS. Authorized large responses use NGINX `X-Accel-Redirect`.
- **npm:** native npm tooling resolves packages. Original tarballs remain byte-identical and preserve upstream integrity values. Metadata is generated dynamically, advertises only committed versions, and constructs `dist.tarball` from request/deployment configuration.
- **OCI:** integrate an OCI Distribution-compatible service where possible. Its blobs, configs, and manifests are inventoried by digest and ultimately backed by CAS; C++ owns desired state and inventory, not redundant registry behavior.
- **Files:** normalized paths map directly to CAS objects and use the same authorization/static-delivery flow.

## Kubernetes reconciliation

CRDs describe repositories, policies, sources, and desired components. The C++ operator computes a canonical configuration hash and looks for a successful resolution manifest with that hash. If all referenced CAS objects exist, reconciliation is a no-op. Otherwise it creates disposable Jobs using native Maven/npm tooling and independent Trivy and Grype workers. Job outputs are ingested, checked, recorded in a new generation, and then the Jobs may be deleted.

CycloneDX, SPDX, scanner-native findings, provenance, and offline vulnerability databases are immutable artifacts. Manifest component records relate the several files of a Maven/npm/OCI component to one logical component so repository-wide SBOM output does not count files as packages.

## One-way transfer

The publisher frames a versioned envelope containing type, length, digest, and metadata followed by bytes. The receiver bounds lengths, stages each payload on the destination filesystem, calculates SHA-256, checks declared integrity, fsyncs, and atomically installs CAS objects. It stages manifests until every reference is verified, then publishes the generation atomically. Interrupted transfers are safe to retry because CAS objects and generations are immutable.

The commercial diode supplies TCP/UDP delivery and reliability; OSDD does not implement or emulate its transport protocol.

## Security and operations

NGINX terminates TLS, authenticates, rate-limits, and routes `/maven`, `/npm`, `/files`, `/api`, and `/v2`. Repository services authorize logical requests. CAS paths are internal; public URLs do not allow arbitrary digest discovery. Input limits, path normalization, digest verification, least-privilege service accounts, read-only containers, and Kubernetes NetworkPolicies apply at both sites.

Recovery consists of restoring CAS and manifests, deleting any SQLite file, and running `osdd-repository rebuild`. No upstream network access is required.
