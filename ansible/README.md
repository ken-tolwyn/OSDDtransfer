# Ansible Quick Fix

This directory replaces the current host setup shell flow with Ansible-managed tasks.

The recommended flow is:

```bash
ansible-playbook -i ansible/inventory.example.ini ansible/playbooks/bootstrap.yml
ansible-playbook -i ansible/inventory.example.ini ansible/playbooks/upstream.yml
```

It currently manages:

- privileged bootstrap for:
  - required host packages
  - `/trunk` ownership and layout
  - linger for user services
- user-scoped runtime deployment for:
  - `~/.local/share/datadiode-sync`
  - `~/.config/datadiode-sync`
  - `~/.config/systemd/user`
- hard-link, inode, and inotify preflight checks
- Reposilite as a host `java -jar` service
- automatic download of the Reposilite JAR when it is missing
- Zot as a rootless Podman-managed user service
- a transfer watcher service using `inotifywait` and `cp -ln`
- Verdaccio as an npm pull-through cache
- a daily repository sync timer
- a daily registry sync timer
- a daily full hard-link transfer sync timer
- Netdata and Cockpit runtime checks

The repository sync timer intentionally keeps the existing `repositories/sync-repositories.sh` logic for now.
The vendored registry runtime currently covers Zot startup, image copy tooling, and Helm chart sync without the old Buildah-based OVAL helper.

All managed helper scripts are deployed under `~/.local/share/datadiode-sync`.

The intended trunk layout is:

- `/trunk/stage` for transferable staged content
- `/trunk/transfer` for the datadiode pickup tree
- `/trunk/iso` for operator-managed ISO input that is not part of file transfer staging

Reposilite uses `/trunk/stage/maven` as its storage root. If `/trunk/stage/maven/configuration.json` exists, the launcher will prefer that config file and keep storage/config in the same folder.
The Ansible runtime now deploys a starter `configuration.json` into that folder; adjust its repository definitions to match production if needed.

They are shipped from `ansible/files/` as standalone scripts and source a common runtime file at `~/.config/datadiode-sync/datadiode.env`.

The main deployed scripts are:

- `start-reposilite.sh`
- `start-zot-container.sh`
- `watch-transfer-links.sh`
- `run-reposync.sh`
- `inode-check.sh`
- `inotify-check.sh`
