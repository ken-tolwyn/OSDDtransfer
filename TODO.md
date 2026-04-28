# TODO

## Priority 1

- Document the four intended transfer classes explicitly:
  - repository transfer for OL8 and OL9
  - container transfer through Zot
  - misc transfer for non-repo, non-container artifacts
  - Nexus export / backup restore workflow
- Standardize all shell scripts on `#!/usr/bin/env bash` plus `set -euo pipefail`.
- Introduce a shared `lib/common.sh` for logging, command checks, cleanup helpers, and common environment loading.
- Move all hard-coded paths and endpoints into variables:
  - `/trunk/...`
  - `192.168.10.10:5001`
- Add consistent quoting for variable expansion and paths.

## Priority 2

- Rename scripts to one format, preferably lowercase kebab-case:
  - keep active entrypoints on the current lowercase kebab-case convention
  - `security-trivy.sh`, `security-grype.sh`, `security-clair.sh`, `security-ol8-oval.sh` already follow the desired pattern
- Replace dated or ad hoc script names with stable functional names.
- Keep a single top-level orchestrator such as `sync-all.sh` that runs the stages in a documented order.
- Add a top-level workflow document that maps each script to one of the transfer classes.
- Stop generating tracked files at runtime where avoidable:
  - especially temporary download and scan artifacts in transfer directories
- Split source files from output files:
  - logs
  - temporary scanner databases
  - hauler tarballs
  - transfer bundles

## Priority 3

- Keep configuration close to each transfer area while preserving one shared root `config.env`.
- Keep Hauler content definition separate from Hauler runtime settings.
- Keep registry chart definitions separate from registry runtime settings via `registries/charts.yaml`.
- Keep the registry preflight report useful for operators, including a failed-only view for quick review.
- Make versions declarative instead of embedding them directly in multiple scripts.
- Add preflight checks for required tools before each sync stage starts.
- Add structured logging with per-run log files and consistent timestamps.
- Add cleanup traps so failed runs do not leave temporary containers or partial outputs behind.
- Validate the new NISP repository import path end to end with a real ISO, including version detection, key publication, and generated `list` files.

## Priority 4

- Reorganize the repository so staged source and vendored content are clearer:
  - `vendor/` for checked-in external repos and binaries
  - `output/` for generated artifacts
- Decide whether large binaries and tarballs should remain committed or be fetched on demand.
- Decide whether `git-repos/dino` belongs in this repository or should be referenced externally.
- Add a `.gitignore` that excludes transient logs, tmp files, generated databases, and transfer artifacts.
- Define where the future `misc` transfer should live:
  - dedicated `misc/` directory
  - Harbor-backed artifact storage
  - another artifact packaging mechanism
- Define the Nexus transfer boundary clearly:
  - what is exported
  - what is restored on the receiving side
  - which steps belong outside this repository

## Documentation

- Keep the root README focused on purpose, workflow, prerequisites, and structure.
- Add per-directory READMEs where operational detail is needed.
- Document required host permissions, mounts, and network assumptions for each sync stage.
- Document what is authoritative input versus generated output.

## Validation

- Add a shell lint target with `shellcheck`.
- Add formatting with `shfmt`.
- Add a dry-run or validation mode for image and repo sync manifests where possible.
- Add a lightweight smoke test for each script entrypoint.
