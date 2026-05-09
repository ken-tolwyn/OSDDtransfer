# OSDDtransfer

Data diode transfer management software.

## Features

- Upstream mode stages pull-through cache configuration for Maven Central using NGINX.
- Synchronizes container images listed in a scope file into on-disk OCI-style directories compatible with zot ingestion workflows.
- Reads `.repo` definitions and runs `reposync` to stage RPM repositories.
- Tracks staged outputs and promotes them to a transfer destination using hard-link copy semantics (`cp -ln`) with overwrite fallback.
- Uses a YAML configuration file and provides a user-level systemd service file.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/osddtransfer /etc/osddtransfer/osddtransfer.yaml
```

## RPM packaging

A starter spec file is provided at `packaging/osddtransfer.spec`.
