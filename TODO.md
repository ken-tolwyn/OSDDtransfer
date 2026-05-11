# TODO

## Priority 1

- Port the actual repository mirror execution from shell into `upstreamd`:
  - `reposync` orchestration
  - OL8/OL9 repo list handling
  - parity with the existing shell output layout
- Decide whether the remaining registry scanner helpers should also move into `upstreamd`:
  - Trivy DB refresh
  - Oracle OVAL refresh
  - any remaining registry-side security assets

## Priority 2

- Extend the native repository importer to handle the real NISP ISO layout end to end with production input.
- Add native parsing of discovered `*.repo` source files from mounted `/config`.
- Decide whether `upstreamd` should merge multiple `*.images` and multiple `*.charts` files or keep a single discovered file per type.
- Add native Reposilite configuration generation from mounted `/config`.
- Add native support for multiple discovered `*.images` and `*.charts` files being merged in one run.

## Priority 3

- Add native preflight checks in `upstreamd` for required repository and registry tools before a scheduled run starts.
- Add structured per-run logging for native sync execution.
- Add a clean compatibility plan for phasing out the shell wrappers once native parity exists.
- Add native registry tests that exercise real tool invocation, not just dry-run discovery markers.

## Priority 4

- Decide whether Hauler remains shell-owned or also gets a native `upstreamd` implementation.
- Decide whether Nexus remains script-based or moves under the same scheduler/runtime model.
- Clean out stale test repository artifacts under `/home/ken/trunk` when no longer needed.

## Documentation

- Keep the root README focused on purpose, workflow, prerequisites, and structure.
- Add per-directory READMEs where operational detail is needed.
- Document required host permissions, mounts, and network assumptions for each sync stage.
- Document what is authoritative input versus generated output.
- Keep the README explicit about which functions are native in `upstreamd` and which still rely on shell scripts.

## Validation

- Add a real-ISO validation fixture for the native NISP importer if a redistributable sample can be created safely.
- Add tests for multiple discovered `*.repo`, `*.images`, and `*.charts` inputs under `/config`.
