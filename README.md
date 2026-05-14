# Datadiode Staging Repository

## Overview

This repository prepares content that must cross a datadiode or similar one-way transfer boundary. It is not a single application. It is an operations repository that stages several different content types so they can be transferred into a restricted or air-gapped environment in a predictable way.

The repository currently covers four transfer classes:

1. Repository transfer
   Oracle Linux and related RPM repositories mirrored under `/trunk/repository`.
2. Registry transfer
   Container images, OCI-backed Helm charts, and scanner databases staged into a local Zot registry under `/trunk/registry`.
3. File transfer
   General file content staged with Hauler under `/trunk/hauler`.
4. Nexus backup transfer
   A file-based Nexus backup archive staged for transfer from a configured server files directory.

The first three are active operational flows in this repository. The Nexus backup flow is also implemented, but it depends on the operator providing the correct Nexus data path and handling service consistency outside the script itself.

## Design

The repository is organized around transfer areas rather than technology stacks. Each directory represents a transfer responsibility:

```text
.
├── config.env         Shared settings used by all active flows
├── hauler/            Hauler-based file transfer
├── lib/               Shared shell helpers
├── nexus/             Nexus backup/export transfer
├── registries/        Zot-based image, chart, and scanner transfer
├── repositories/      RPM repository transfer
├── sync-all.sh        Top-level orchestration entrypoint
└── git-repos/         Vendored source snapshots kept in the repo
```

Active per-flow configuration lives next to the scripts that use it:

```text
config.env
hauler/settings.env
registries/config.env
registries/charts.yaml
repositories/config.env
nexus/config.env.example
```

This keeps each transfer definition close to its operational logic and avoids a large disconnected config tree.

## Transfer Model

The intended `/trunk` layout is:

```text
/trunk
├── hauler/
├── nexus-backup/
├── registry/
├── repository/
└── transfer/
```

The meaning of those paths is important:

- `/trunk/hauler`, `/trunk/repository`, `/trunk/registry`, and optionally `/trunk/nexus-backup` are persistent local staging areas.
- `/trunk/transfer` is the datadiode pickup area for file-based transfers.
- The datadiode software is expected to read from `/trunk/transfer`, move the staged content across, and remove transferred files afterwards.
- `/trunk/hauler` is different from the other flows because it can be transferred as a directory directly by the datadiode configuration rather than copied into `/trunk/transfer`.

For the file-based flows, the repository follows this rule:

- local source of truth lives under `/trunk/<name>`
- transfer copy is staged under `/trunk/transfer/<name>`

That means:

- repositories stage from `/trunk/repository` to `/trunk/transfer/repository`
- registries stage from `/trunk/registry` to `/trunk/transfer/registry`
- nexus stages archive files into `/trunk/transfer/nexus`

The older nested pattern `/trunk/transfer/trunk/...` is not intended and is no longer used by the active scripts.

## Transfer Flows

### Repository Transfer

`repositories/sync-repositories.sh` mirrors the configured OL8 and OL9 repositories into `/trunk/repository`. It also supports a manual NISP import path: an operator places a NISP ISO in `/tmp`, and the script imports it into `/trunk/repository/<version>/` using `bsdtar`, generates `/trunk/repository/<version>.repo`, copies discovered GPG keys into `/trunk/repository/keys/`, and regenerates the top-level `list` files used by the downstream repository service.

The repository output now includes:

- `OL8/` and `OL9/` mirrored RPM content
- `<version>/` extracted NISP content for each imported ISO version
- `OL8.repo`, `OL9.repo`, and `<version>.repo` repo definition files
- `keys/` with published GPG keys
- `list` and `keys/list` indexes for downstream consumption

The OL9 sync still uses a containerized path because that matches the upstream tooling assumptions more reliably than forcing the host to carry all dependencies directly.

### Registry Transfer

`registries/sync-registries.sh` is the main registry flow. It starts a local Zot registry backed by `/trunk/registry/data`, then performs four types of content sync:

1. Container images from `registries/outside.yaml`
2. Optional kubeadm images from `kubeadm config images list`
3. Scanner database updates from `registries/security-*.sh`
4. Helm chart sync from `registries/sync-helm-*.sh`

Helm charts are defined in `registries/charts.yaml`. They are pushed into the `public` namespace and can preserve subpaths using `targetPath`, for example `public/charts/platform` or `public/charts/gitlab`.

The active chart sync uses Helm through a Podman container image configured in `registries/config.env`. The host does not need a local Helm installation.

### File Transfer with Hauler

`hauler/sync-hauler.sh` now has a narrow role: sync the file content described in `hauler/content-manifest.yaml` into `/trunk/hauler`, then stage that content for transfer.

The active Hauler flow no longer:

- downloads the Hauler binary at runtime
- mixes in kubeadm image generation
- serves as a chart/image catch-all

Instead:

- runtime settings live in `hauler/settings.env`
- file content definition lives in `hauler/content-manifest.yaml`
- execution happens through a pinned Podman image

That makes the Hauler path much easier to reason about. In this repository, Hauler should be treated as the file transfer channel for things such as Kubernetes YAML, deployment bundles, bootstrap files, and similar content intended to land on a downstream Kubernetes node.

### Nexus Backup Transfer

`nexus/export-nexus-backup.sh` creates a compressed archive from a configured Nexus server files directory, writes checksum and metadata files, and stages them as a file-based transfer.

`nexus/restore-nexus-backup.sh` restores a transferred backup into a target directory and can verify the checksum first.

This flow is intentionally file-based. It does not manage the Nexus service lifecycle for you. If a consistent backup requires Nexus to be quiesced or stopped, that is an operational step outside the script.

## Running the Repository

Use the top-level orchestrator when you want to run the active flows together:

```bash
bash sync-all.sh
```

It starts the following jobs in parallel:

- repository sync
- registry sync
- hauler sync
- nexus backup sync, if `nexus/config.env` exists

You can also run the flows individually:

```bash
bash repositories/sync-repositories.sh
bash registries/prefetch-registries.sh
bash registries/sync-registries.sh
bash registries/reconcile-zot-images.sh
bash hauler/sync-hauler.sh
bash nexus/export-nexus-backup.sh
```

Use `registries/prefetch-registries.sh` when you want a preflight check before the full registry sync. It expands the current image, kubeadm, and chart inputs into a concrete check list, verifies that the upstream sources can be accessed, and writes a report to `registries/tmp/prefetch/registry-source-check.tsv`.
It also writes a failed-only report to `registries/tmp/prefetch/registry-source-check-failed.tsv` for quicker operator review.

For the NISP repository path, the upstream acquisition step is manual. Place the ISO in `/tmp` before running `repositories/sync-repositories.sh`. The script will import only versions that do not already have a matching `/trunk/repository/<version>.repo`, then delete the processed ISO from `/tmp`.

Use `registries/reconcile-zot-images.sh` when you want to compare the live Zot image content under `public` with the desired image set from `registries/outside.yaml` and optional kubeadm images. It defaults to dry-run and only applies deletions when run with `--apply`.

## Upstream Staging and Transfer Manifests

Before each file-based `rsync` into `/trunk/transfer/<name>`, the shared staging layer now generates a manifest file named `.transfer-manifest.tsv`.

That manifest contains one line per file:

```text
<sha256><TAB><relative-path>
```

Example:

```text
1d54...eaa2    OL8.repo
7fa1...bc30    OL8/ol8_appstream/repodata/repomd.xml
```

This manifest serves two purposes:

1. It gives the downstream side a precise list of files that are expected to exist.
2. It gives the downstream side a checksum-based verification source before cleanup is attempted.

The manifest is created automatically by the shared helper functions in `lib/common.sh`. Operators do not need to generate it manually.

## Downstream Reconciliation

The downstream side should not blindly delete old files just because a new transfer arrived. It should first verify that the received files match the manifest.

That is what `reconcile-transfer.sh` is for.

Run it against the received transfer directory:

```bash
bash reconcile-transfer.sh /path/to/received-transfer-dir
```

What it does:

1. Reads `.transfer-manifest.tsv`
2. Verifies that every listed file exists
3. Verifies that every listed file matches the expected SHA-256
4. Deletes files in the target directory that are not listed in the manifest
5. Removes empty directories left behind by those deletions

What it does not do:

- it does not accept missing files
- it does not accept checksum mismatches
- it does not delete anything if verification fails first

That last point matters. The safety model is:

- verify first
- reconcile second

So the downstream deletion behavior is controlled and repeatable rather than based on assumptions about what the datadiode software may or may not have transferred fully.

## Host Requirements

The active flows assume a host that already has the following tools available:

- `bash`
- `curl`
- `jq`
- `yq`
- `rsync`
- `reposync`
- `kubeadm`
- `podman`
- `buildah`
- `skopeo`
- `sha256sum`

Some tooling is intentionally containerized to keep the scripts portable:

- Hauler runs through a Podman container image
- Helm runs through a Podman container image
- ORAS runs through a Podman container image in the shared helper layer

That reduces the number of direct host dependencies and keeps the flows closer to self-contained.

## Running with User Systemd

If this staging host should run the sync jobs as a user-managed service, `sync-all.sh` can be started from a user-level systemd unit.

Create the user service directory if it does not already exist:

```bash
mkdir -p ~/.config/systemd/user
```

Create `~/.config/systemd/user/datadiode-sync.service` with content like:

```ini
[Unit]
Description=Datadiode Staging Sync
After=default.target

[Service]
Type=oneshot
WorkingDirectory=/path/to/this/repository
ExecStart=/usr/bin/bash /path/to/this/repository/sync-all.sh

[Install]
WantedBy=default.target
```

Replace `/path/to/this/repository` with the real checkout path.

A ready-to-copy example is also included in the repository:

- [systemd/datadiode-sync.service.example](/home/ken/gitdir/sync/systemd/datadiode-sync.service.example)
- [systemd/datadiode-sync.timer.example](/home/ken/gitdir/sync/systemd/datadiode-sync.timer.example)

Then reload and enable it:

```bash
systemctl --user daemon-reload
systemctl --user enable datadiode-sync.service
systemctl --user start datadiode-sync.service
```

To check status and logs:

```bash
systemctl --user status datadiode-sync.service
journalctl --user -u datadiode-sync.service
```

If the service should continue to run even when the user is not logged in, enable lingering for that user:

```bash
loginctl enable-linger "$USER"
```

For a scheduled run rather than a one-time service, use a user-level timer pair such as `datadiode-sync.service` and `datadiode-sync.timer`.

Example timer behavior included in the repository:

- start 5 minutes after boot
- run again every hour

To enable the timer:

```bash
systemctl --user daemon-reload
systemctl --user enable datadiode-sync.timer
systemctl --user start datadiode-sync.timer
```

To inspect the timer:

```bash
systemctl --user list-timers
systemctl --user status datadiode-sync.timer
```

## Ansible Host Setup and Monitoring

An Ansible playbook is provided to bootstrap and run this setup on a host:

- creates required `/trunk` and `/trunk/transfer` directories
- configures inotify monitoring for repository and registry staging paths
- installs user systemd units for:
  - Zot registry sync startup
  - Reposilite startup
  - scheduled repository sync timer

Playbook path:

```text
server-config/ansible/setup-monitor.yaml
```

Example usage:

```bash
ansible-playbook -i inventory.ini server-config/ansible/setup-monitor.yaml \
  -e datadiode_user="$USER" \
  -e osddtransfer_checkout=/path/to/this/repository
```

Important variables:

- `datadiode_user`: Linux user that owns and runs user-level systemd units
- `osddtransfer_checkout`: checkout path used by sync unit `ExecStart`
- `trunk_root` and `transfer_root`: root staging paths (default `/trunk` and `/trunk/transfer`)
- `inotify_watch_paths`: directories monitored by the inotify service
- `reposilite_image` and `reposilite_port`: Reposilite container runtime settings
  - default image is pinned to `docker.io/dzikoysk/reposilite:3.5.22` in the playbook; change it explicitly if you need a different tested version
  - if you change versions, validate against the Reposilite release notes and run a playbook dry run plus service start check on your target host

## Current State

The repository is already in a workable state for the main transfer classes, but a few realities are worth keeping in mind:

- Active entrypoints now follow a more uniform lowercase kebab-case naming style.
- Some legacy files still exist beside the active ones, especially in `hauler/`.
- Generated artifacts, vendored source, and operational inputs still coexist in the same repository.
- `sync-all.sh` is intentionally thin. Most detailed behavior still lives inside the per-flow scripts.

That is acceptable for now, but it means the repository is best understood as an operational staging workspace rather than a polished product.

## Recommended Use

Use this repository as:

- the source of truth for what should be staged
- the source of truth for how it should be staged
- a reproducible handoff point into the datadiode process

Do not use it as:

- the long-term archive of generated transfer outputs
- a substitute for downstream acknowledgment logic
- a substitute for service-specific backup discipline where consistency matters

For additional cleanup and follow-on work, see `TODO.md`.
