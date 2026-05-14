# Upstream Services

This directory contains the current upstream runtime helpers and the target daemon configuration model for:

- a user-systemd managed Zot registry
- a user-systemd managed Reposilite process
- a file watcher that hard-links staged content into `/trunk/transfer`

## Files

- `config.env`
  Runtime settings for Zot, Reposilite, and the transfer watcher.
- `setup-upstream.sh`
  Creates the upstream staging and transfer directories with the configured mode.
- `start-zot.sh`
  Runs Zot directly as a host process and generates a runtime config pointing at `/trunk/registry/data`.
- `start-reposilite.sh`
  Runs Reposilite from a local JAR.
- `watch-transfer-links.sh`
  Watches `/trunk/repository`, `/trunk/registry`, and `/trunk/maven` and mirrors changes into `/trunk/transfer` using hard links.
- `test-trunk-permissions.sh`
  Startup probe for directory writability, same-filesystem checks, and hard-link support between staged trees and `/trunk/transfer`.
- `upstreamd.example.toml`
  Proposed single-daemon configuration model for the RPM-delivered C++ implementation.
- `../container-config/`
  Example directory to mount into the container as `/config`, including `upstreamd.toml`, sample `*.repo`, `*.images`, and `*.charts` files.
- `test-upstreamd-in-container.sh`
  Runs the built `upstreamd` binary inside an Oracle Linux 9 container with a host trunk directory mounted as `/trunk`.
- `test-watch-in-container.sh`
  End-to-end watcher integration test using the containerized runtime and a mounted host trunk directory.
- `systemd/`
  Example user-systemd units for the upstream stack.

## Expected Binaries

- Zot binary: `${HOME}/bin/zot`
- Reposilite JAR: `${HOME}/bin/reposilite.jar`

Adjust those paths in `config.env` if your installation differs.

## Target Runtime Model

The intended C++ daemon model is simpler than the current shell split:

- one `workdir`, normally `/trunk`
- the process creates the full folder structure itself
- Zot and Reposilite are started as managed child processes
- file changes under the staged source trees are hard-linked into `/trunk/transfer`
- delete propagation is not handled by the daemon
- external services remain responsible for downstream cleanup

The `upstreamd.example.toml` file captures that target model.

### Current C++ Test Flow

The active `upstreamd` tests should be run through the container helpers rather than by executing the binary directly on the host.

- startup and config validation:
  `bash upstream/test-upstreamd-in-container.sh upstream/testdata/upstreamd-container-test.toml`
- one-shot promotion:
  `bash upstream/test-upstreamd-in-container.sh upstream/testdata/upstreamd-container-test.toml --promote-once`
- watcher integration:
  `bash upstream/test-watch-in-container.sh`
- child-process supervision:
  `bash upstream/test-upstreamd-in-container.sh upstream/testdata/upstreamd-supervise-container-test.toml --supervise 2`

### Mounted Config Directory

The preferred container model is now:

- mount the work tree to `/trunk`
- mount a separate configuration directory to `/config`

The `/config` directory is discovered by file extension:

- `*.repo` for repository source definitions
- `*.iso` for repository ISO imports such as NISP media
- `*.images` for registry image definitions
- `*.charts` for registry chart definitions

Multiple `*.images` and `*.charts` files are merged in one native registry run.

An example config directory is provided at [container-config](/home/ken/gitdir/sync/container-config). Its `upstreamd.toml` is intended to be passed to the daemon from inside the container as `/config/upstreamd.toml`.

ISO media can be placed elsewhere with an explicit config entry. The current example uses `iso_dir = "/trunk/iso"` so NISP input can be dropped onto the work volume instead of the read-mostly mounted config directory.

The importer must not delete source ISO files. `/trunk/iso` is treated as a persistent input location, not a scratch area.

### Trunk Layout

The daemon should create and manage:

- `/trunk/repository`
- `/trunk/registry`
- `/trunk/maven`
- `/trunk/transfer`
- `/trunk/transfer/repository`
- `/trunk/transfer/registry`
- `/trunk/transfer/maven`
- `/trunk/config`

The `/trunk/config` tree is authoritative input and is not mirrored into transfer. It is intended to hold:

- repository `.repo` source files
- repository ISO files such as NISP input
- registry image YAML definitions
- registry chart YAML definitions
- Reposilite configuration

### Scheduled Sync Work

The target daemon configuration includes cron-style schedules for:

- repository sync
- registry sync

Each sync area also declares a full sync cadence such as `daily` or `weekly`.

Repository sync should keep the current repository logic:

- reuse the existing `.repo`-file driven sync behavior
- keep the ISO import path for repository content
- keep key generation and repository index generation

The target model does not require the file watcher to decide sync timing. The scheduler should trigger full sync cycles independently, while the watcher only promotes changed staged files into transfer.

### Native Registry Coverage

The native registry path in `upstreamd` now covers:

- image sync from discovered `*.images` files
- chart sync from discovered `*.charts` files using a direct `helm` binary
- Trivy DB image collection
- Oracle Linux 8 OVAL collection

The main remaining registry gap is a fuller non-dry-run integration test path for the real external tools.

## Systemd

Copy the unit files from `upstream/systemd/` into `~/.config/systemd/user/`, replace `/path/to/this/repository` with the real checkout path, then reload and enable them with `systemctl --user`.

Each upstream service now runs `test-trunk-permissions.sh /trunk` as an `ExecStartPre` check. If the work directory cannot be written, if required subdirectories are missing, or if hard links cannot be created between the staged trees and transfer trees, the service start will fail with actionable output.
