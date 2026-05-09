# Upstream Services

This directory contains the upstream runtime helpers for:

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
- `systemd/`
  Example user-systemd units for the upstream stack.

## Expected Binaries

- Zot binary: `${HOME}/bin/zot`
- Reposilite JAR: `${HOME}/bin/reposilite.jar`

Adjust those paths in `config.env` if your installation differs.

## Systemd

Copy the unit files from `upstream/systemd/` into `~/.config/systemd/user/`, replace `/path/to/this/repository` with the real checkout path, then reload and enable them with `systemctl --user`.
