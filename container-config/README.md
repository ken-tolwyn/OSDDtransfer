# Container Config

This directory is intended to be mounted into the upstream container as `/config`.

Files are discovered by extension:

- `*.repo`
  Repository source files for repository sync.
- `*.iso`
  Repository ISO input files such as NISP media.
- `*.images`
  Registry image definition file.
- `*.charts`
  Registry chart definition file.

The runtime settings for the daemon live in `upstreamd.toml`.

In the example configuration, ISO imports are read from `/trunk/iso` instead of `/config`, so large transient media can live on the work volume while the static definitions stay in the mounted config directory.

Source ISO files are treated as operator-managed input and are not deleted by the importer.
