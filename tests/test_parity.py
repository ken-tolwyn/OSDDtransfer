from osddtransfer.parity import reconstruct_single_missing, xor_bytes


def test_xor_bytes():
    a = b"\x01\x02\x03"
    b = b"\x01\x03\x01"
    assert xor_bytes([a, b]) == b"\x00\x01\x02"


def test_reconstruct_single_missing():
    shards = [b"abc", b"def", b"ghi", b"jkl"]
    parity = xor_bytes(shards)
    with_gap = [shards[0], None, shards[2], shards[3]]
    recovered = reconstruct_single_missing(with_gap, parity)
    assert recovered == shards
