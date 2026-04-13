# Oracle APEX Implementation Guide (MVP)

This repository now includes an APEX-ready data model and PL/SQL package for the management plane.

## SQLcl deployment

```sql
@db/sqlcl/install.sql
```

## APEX app setup (manual, deterministic)

Create a new APEX application named `OSDDtransfer` with these pages:

1. **Transfer Agents**
   - Interactive report on `transfer_agent`.
   - Form page allowing create/update via `osdd_management_api.upsert_agent`.

2. **Agent UDP Ports**
   - Interactive report on `agent_port_assignment`.
   - Form page calling `osdd_management_api.assign_udp_port`.

3. **Registry Accounts**
   - Interactive report and form on `registry_account`.

4. **Artifact Presence**
   - Read-only report on `artifact_presence`.

5. **Transfer Batches**
   - Read-only report on `transfer_batch`.

This keeps APEX configuration thin while business logic stays in versioned SQLcl scripts.
