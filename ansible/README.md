# Ansible: Software Data Diode Setup

This playbook configures a host as a **software-enforced one-way diode** for UDP transfer traffic.

## What it does

- Creates two macvlan interfaces on a parent NIC:
  - low side interface (sender-facing)
  - high side interface (receiver-facing)
- Assigns static CIDR addresses to both interfaces.
- Enables IPv4 forwarding.
- Configures nftables rules to:
  - allow only low→high UDP traffic on configured ports
  - drop all high→low traffic
  - drop non-UDP low→high traffic
- Installs a systemd oneshot unit so diode rules are re-applied on boot.

## Files

- `playbooks/setup_software_diode.yml` – main playbook
- `inventory/hosts.ini` – sample inventory
- `roles/software_diode/defaults/main.yml` – tunable variables
- `roles/software_diode/templates/osdd-diode-setup.sh.j2` – idempotent setup script

## Usage

1. Edit `inventory/hosts.ini` with your host and SSH user.
2. Override role defaults as needed (group_vars, host_vars, or `-e`).
3. Run:

```bash
ansible-playbook -i ansible/inventory/hosts.ini ansible/playbooks/setup_software_diode.yml
```

## Important note

A software diode is a policy enforcement mechanism and **not equivalent to a hardware diode** for high-assurance environments. Use hardware-enforced one-way controls when mandated.
