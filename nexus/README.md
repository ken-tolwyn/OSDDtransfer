# Nexus Transfer

This directory contains file-based staging scripts for a Nexus Community backup/export workflow.

The approach is intentionally simple:

- read a local config file
- archive the configured Nexus data directory
- generate checksum and metadata files
- optionally copy the archive into a transfer directory
- provide a matching restore script for the receiving side

## Files

- `export-nexus-backup.sh`: create a staged backup archive from a Nexus server files directory
- `restore-nexus-backup.sh`: restore a previously created archive into a target directory
- `config.env.example`: example configuration

## Configuration

Copy `config.env.example` to `config.env` and adjust the values for your environment.

The main setting is:

- `NEXUS_SERVER_FILES_DIR`

This should point at the Nexus server files location you want to archive and transfer.

## Notes

- The scripts do not stop Nexus for you.
- For a consistent backup, the Nexus instance should be quiesced or otherwise handled according to your operational procedure before export.
- The scripts are file-based. They do not call Nexus APIs.
