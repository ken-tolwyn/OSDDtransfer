from osddtransfer.protocol import PacketHeader, parse_packet, packet_bytes


def test_packet_roundtrip():
    payload = b"hello"
    header = PacketHeader(
        version=1,
        transfer_agent_id="a1",
        batch_id="b1",
        stripe_id=0,
        shard_index=0,
        k=4,
        m=1,
        kind="data",
        payload_sha256="2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824",
    )
    blob = packet_bytes(header, payload)
    parsed_header, parsed_payload = parse_packet(blob)
    assert parsed_payload == payload
    assert parsed_header.batch_id == "b1"
