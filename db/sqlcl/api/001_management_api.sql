create or replace package osdd_management_api as
  procedure upsert_agent(
    p_transfer_agent_id in varchar2,
    p_name in varchar2,
    p_enabled in number default 1
  );

  procedure assign_udp_port(
    p_transfer_agent_id in varchar2,
    p_data_udp_port in number,
    p_management_endpoint in varchar2 default null
  );

  procedure mark_artifact_seen(
    p_transfer_agent_id in varchar2,
    p_artifact_type in varchar2,
    p_artifact_key in varchar2,
    p_artifact_sha256 in varchar2
  );
end osdd_management_api;
/

create or replace package body osdd_management_api as
  procedure upsert_agent(
    p_transfer_agent_id in varchar2,
    p_name in varchar2,
    p_enabled in number default 1
  ) as
  begin
    merge into transfer_agent t
    using (select p_transfer_agent_id transfer_agent_id, p_name name, p_enabled enabled from dual) s
      on (t.transfer_agent_id = s.transfer_agent_id)
    when matched then
      update set t.name = s.name, t.enabled = s.enabled
    when not matched then
      insert (transfer_agent_id, name, enabled)
      values (s.transfer_agent_id, s.name, s.enabled);
  end upsert_agent;

  procedure assign_udp_port(
    p_transfer_agent_id in varchar2,
    p_data_udp_port in number,
    p_management_endpoint in varchar2 default null
  ) as
  begin
    insert into agent_port_assignment(transfer_agent_id, data_udp_port, management_endpoint)
    values (p_transfer_agent_id, p_data_udp_port, p_management_endpoint);
  exception
    when dup_val_on_index then
      update agent_port_assignment
      set management_endpoint = p_management_endpoint
      where transfer_agent_id = p_transfer_agent_id and data_udp_port = p_data_udp_port;
  end assign_udp_port;

  procedure mark_artifact_seen(
    p_transfer_agent_id in varchar2,
    p_artifact_type in varchar2,
    p_artifact_key in varchar2,
    p_artifact_sha256 in varchar2
  ) as
  begin
    merge into artifact_presence t
    using (
      select p_transfer_agent_id transfer_agent_id,
             p_artifact_type artifact_type,
             p_artifact_key artifact_key,
             p_artifact_sha256 artifact_sha256
      from dual
    ) s
    on (t.transfer_agent_id = s.transfer_agent_id and t.artifact_type = s.artifact_type and t.artifact_key = s.artifact_key)
    when matched then
      update set t.artifact_sha256 = s.artifact_sha256,
                 t.seen_count = t.seen_count + 1,
                 t.last_seen_at = systimestamp,
                 t.deleted_at = null
    when not matched then
      insert (transfer_agent_id, artifact_type, artifact_key, artifact_sha256)
      values (s.transfer_agent_id, s.artifact_type, s.artifact_key, s.artifact_sha256);
  end mark_artifact_seen;
end osdd_management_api;
/
