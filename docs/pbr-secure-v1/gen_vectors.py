#!/usr/bin/env python3
"""Generates the pbr-secure-v1 conformance vectors.

Run: python3 docs/pbr-secure-v1/gen_vectors.py
Writes docs/pbr-secure-v1/vectors.json. Every input is fixed so two
implementations can be compared byte for byte.
"""
import hashlib
import json
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, x25519
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

OUT = Path(__file__).with_name("vectors.json")

SUITE = "x25519-hkdf-sha256-chacha20poly1305-ecdsa-p256"
PROTO_VER = 1


def hkdf(ikm: bytes, salt: bytes, info: bytes, length: int) -> bytes:
    return HKDF(algorithm=hashes.SHA256(), length=length, salt=salt, info=info).derive(ikm)


def tag(t: str) -> bytes:
    """Length-prefixed transcript element: u16le(len) || bytes."""
    raw = t.encode()
    return len(raw).to_bytes(2, "little") + raw


def elem(name: str, body: bytes) -> bytes:
    return tag(name) + len(body).to_bytes(2, "little") + body


def le16(v: int) -> bytes:
    return v.to_bytes(2, "little")


def le32(v: int) -> bytes:
    return v.to_bytes(4, "little")


def le64(v: int) -> bytes:
    return v.to_bytes(8, "little")


def _reusable(path: str, signed: bytes, pub: bytes) -> bytes | None:
    """ECDSA with a random k is not reproducible, so a regenerated signature
    would never match the committed vector. Reuse and re-verify the stored one;
    only mint a new one when the file has none. Vectors must be stable."""
    if not OUT.exists():
        return None
    try:
        prev = json.loads(OUT.read_text(encoding="utf-8"))[path]
    except (KeyError, ValueError):
        return None
    if bytes.fromhex(prev["signed"]) != signed:
        return None
    raw = bytes.fromhex(prev["signature_raw"])
    try:
        ec.EllipticCurvePublicKey.from_encoded_point(
            ec.SECP256R1(), pub).verify(_raw_to_der(raw), signed, ec.ECDSA(hashes.SHA256()))
    except Exception:
        return None
    return raw


def _seal(key: bytes, nonce: bytes, aad: bytes, plaintext: bytes):
    sealed = ChaCha20Poly1305(key).encrypt(nonce, plaintext, aad)
    return sealed[:-16], sealed[-16:]


def _aad(key_id: int, kind: int, direction: int, auth_seq: int, length: int) -> bytes:
    return bytes([1, key_id, kind, direction]) + le32(auth_seq & 0xFFFFFFFF) + le16(length)


def _envelope(key: bytes, key_id: int, nonce: bytes, kind: int, direction: int,
              auth_seq: int, plaintext: bytes) -> bytes:
    ct, tag = _seal(key, nonce, _aad(key_id, kind, direction, auth_seq, len(plaintext)), plaintext)
    return bytes([1, key_id]) + nonce + ct + tag


def main() -> None:
    # ---- fixed handshake inputs -------------------------------------------
    pi_eph = x25519.X25519PrivateKey.from_private_bytes(bytes(range(1, 33)))
    br_eph = x25519.X25519PrivateKey.from_private_bytes(bytes(range(33, 65)))
    pi_eph_pub = pi_eph.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    br_eph_pub = br_eph.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    shared = pi_eph.exchange(br_eph.public_key())

    pi_identity = ec.derive_private_key(0x1234567890ABCDEF, ec.SECP256R1())
    br_identity = ec.derive_private_key(0x0FEDCBA987654321, ec.SECP256R1())

    def uncompressed(key):
        return key.public_key().public_bytes(
            serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)

    pi_pub = uncompressed(pi_identity)
    br_pub = uncompressed(br_identity)

    def key_id(pub: bytes) -> int:
        return hashlib.sha256(pub).digest()[0]

    boot_id = hashlib.sha256(b"pbr-boot").digest()[:16]
    boot_id4 = boot_id[:4]

    # ---- transcript -------------------------------------------------------
    msg1 = bytes([PROTO_VER, 1]) + pi_eph_pub + b"\x11" * 16
    msg2 = (bytes([PROTO_VER, 1]) + br_eph_pub + b"\x22" * 16
            + boot_id4 + bytes([key_id(br_pub)]))
    msg3 = bytes([key_id(pi_pub)]) + pi_pub

    t2 = elem("pbr_hello_req", msg1) + elem("pbr_hello_resp", msg2)
    t3 = t2 + elem("pbr_hello_finish", msg3)
    transcript_hash = hashlib.sha256(t3).digest()

    # ---- key schedule -----------------------------------------------------
    ks = hkdf(shared, transcript_hash, b"pbr-secure-v1", 96)
    k_hs, k_i2b, k_b2i = ks[:32], ks[32:64], ks[64:96]
    session_id = transcript_hash[:16]
    nonce_prefix = transcript_hash[16:20]

    # ---- hello_ok, sealed with k_handshake, auth_seq 1 --------------------
    # uptime_ms anchors the TTL offset (spec 3.5); a fixed sample keeps the vector stable.
    hello_ok_pt = b'{"schema":"pbr-handshake-v1","msg":"hello_ok","uptime_ms":12345}'
    hello_ok_nonce = nonce_prefix + le64(1)
    hello_ok_aad = _aad(key_id(br_pub), 1, 1, 1, len(hello_ok_pt))
    hello_ok_ct, hello_ok_tag = _seal(k_hs, hello_ok_nonce, hello_ok_aad, hello_ok_pt)

    # ---- first business frame, bridge->Pi, auth_seq 2 ---------------------
    # The sample is a bridge->Pi event, so dir=1, the sender is the bridge, and
    # the direction key is k_bridge_to_pi. Getting this backwards would let an
    # implementation pass the vector while sealing in the wrong direction.
    auth_seq = 2
    plaintext = b'{"event":"attach","module_id":80,"module_instance":"planter-a"}'
    nonce = nonce_prefix + le64(auth_seq)
    aad = _aad(key_id(br_pub), 1, 1, auth_seq, len(plaintext))
    ct, mac = _seal(k_b2i, nonce, aad, plaintext)

    # ---- signatures -------------------------------------------------------
    raw_pi_sig = _reusable("identity_signature", t3, pi_pub)
    if raw_pi_sig is None:
        raw_pi_sig = _der_to_raw(pi_identity.sign(t3, ec.ECDSA(hashes.SHA256())))
    raw_br_sig = _reusable("bridge_signature", t2, br_pub)
    if raw_br_sig is None:
        raw_br_sig = _der_to_raw(br_identity.sign(t2, ec.ECDSA(hashes.SHA256())))

    envelope = bytes([1, key_id(br_pub)]) + nonce + ct + mac

    vectors = {
        "suite": SUITE,
        "proto_ver": PROTO_VER,
        "env_ver": 1,
        "note": "Fixed inputs; regenerate with docs/pbr-secure-v1/gen_vectors.py",
        "handshake": {
            "pi_eph_priv": _hex(bytes(range(1, 33))),
            "bridge_eph_priv": _hex(bytes(range(33, 65))),
            "pi_eph_pub": _hex(pi_eph_pub),
            "bridge_eph_pub": _hex(br_eph_pub),
            "x25519_shared": _hex(shared),
            "pi_identity_pub_uncompressed": _hex(pi_pub),
            "bridge_identity_pub_uncompressed": _hex(br_pub),
            "pi_key_id": key_id(pi_pub),
            "bridge_key_id": key_id(br_pub),
            "boot_id": _hex(boot_id),
            "boot_id4": _hex(boot_id4),
            "msg1_body": _hex(msg1),
            "msg2_body": _hex(msg2),
            "msg3_body": _hex(msg3),
            "t2": _hex(t2),
            "t3": _hex(t3),
            "transcript_hash": _hex(transcript_hash),
            "session_id": _hex(session_id),
            "nonce_prefix": _hex(nonce_prefix),
        },
        "keys": {
            "info": "pbr-secure-v1",
            "salt_is_transcript_hash": True,
            "hkdf_96": _hex(ks),
            "k_handshake": _hex(k_hs),
            "k_pi_to_bridge": _hex(k_i2b),
            "k_bridge_to_pi": _hex(k_b2i),
        },
        "hello_ok": {
            "auth_seq": 1,
            "plaintext": hello_ok_pt.decode(),
            "nonce": _hex(hello_ok_nonce),
            "aad": _hex(hello_ok_aad),
            "ciphertext": _hex(hello_ok_ct),
            "tag": _hex(hello_ok_tag),
            "envelope_hex": _hex(
                bytes([1, key_id(br_pub)]) + hello_ok_nonce + hello_ok_ct + hello_ok_tag),
        },
        "envelope": {
            "auth_seq": auth_seq,
            "plaintext": plaintext.decode(),
            "nonce": _hex(nonce),
            "aad": _hex(aad),
            "ciphertext": _hex(ct),
            "tag": _hex(mac),
            "envelope_hex": _hex(envelope),
        },
        "identity_signature": {
            "alg": "ECDSA-P256-SHA256, raw r||s 64 bytes",
            "signer": "pi",
            "signed": _hex(t3),
            "signature_raw": _hex(raw_pi_sig),
            "must_verify_with": _hex(pi_pub),
        },
        "bridge_signature": {
            "alg": "ECDSA-P256-SHA256, raw r||s 64 bytes",
            "signer": "bridge",
            "signed": _hex(t2),
            "signature_raw": _hex(raw_br_sig),
            "must_verify_with": _hex(br_pub),
        },
        "negative": [
            {"case": "flip_ciphertext_bit",
             "envelope_hex": _hex(bytes([1, key_id(br_pub)]) + nonce + _flip(ct, 0) + mac),
             "expect": "tag_mismatch", "response": "none"},
            {"case": "flip_tag_bit",
             "envelope_hex": _hex(envelope[:-1] + bytes([envelope[-1] ^ 0x80])),
             "expect": "tag_mismatch", "response": "none"},
            {"case": "replay_same_envelope",
             "envelope_hex": _hex(envelope),
             "expect": "REPLAY", "response": "error_frame",
             "note": "deliver the same envelope twice; second must be REPLAY"},
            {"case": "auth_seq_zero",
             "envelope_hex": _hex(_envelope(k_b2i, key_id(br_pub), nonce_prefix + le64(0),
                                            1, 1, 0, plaintext)),
             "expect": "REPLAY", "response": "error_frame"},
            {"case": "wrong_key_id",
             "key_id": (key_id(br_pub) + 1) & 0xFF,
             "envelope_hex": _hex(bytes([1, (key_id(br_pub) + 1) & 0xFF]) + nonce + ct + mac),
             "expect": "IDENTITY_REJECTED", "response": "none",
             "note": "unknown key id is dropped silently; the code only reaches the audit log"},
            {"case": "sealed_with_wrong_direction_key",
             "envelope_hex": _hex(_envelope(k_i2b, key_id(br_pub), nonce, 1, 1, auth_seq, plaintext)),
             "expect": "tag_mismatch", "response": "none"},
            {"case": "kind_tampered",
             "envelope_hex": _hex(envelope),
             "frame_kind": 3,
             "expect": "tag_mismatch", "response": "none",
             "note": "AAD covers kind; changing the frame header kind alone must fail"},
            {"case": "proto_ver_downgrade",
             "proto_ver": 0,
             "expect": "UNSUPPORTED_VERSION", "response": "error_frame"},
            {"case": "bridge_signature_tampered",
             "signed": _hex(t2),
             "signature_raw": _hex(_flip(raw_br_sig, 0)),
             "must_verify_with": _hex(br_pub),
             "expect": "IDENTITY_REJECTED", "response": "error_frame"},
            {"case": "ttl_not_3000",
             "ttl_ms": 2999,
             "expect": "EVENT_EXPIRED", "response": "error_frame"},
            {"case": "rfid_uid_wrong_length",
             "uid": "A1B2C3", "uid_len": 4,
             "expect": "INVALID_MESSAGE", "response": "error_frame"},
            {"case": "rfid_uid_not_hex",
             "uid": "ZZ123456", "uid_len": 4,
             "expect": "INVALID_MESSAGE", "response": "error_frame"},
        ],
    }
    OUT.write_text(json.dumps(vectors, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {OUT}")
    print("transcript_hash", vectors["handshake"]["transcript_hash"])
    print("k_pi_to_bridge", vectors["keys"]["k_pi_to_bridge"])


def _hex(b: bytes) -> str:
    return b.hex()


def _flip(b: bytes, index: int) -> bytes:
    out = bytearray(b)
    out[index] ^= 0x01
    return bytes(out)


def _der_to_raw(der: bytes) -> bytes:
    """ECDSA signatures travel as fixed r||s, never DER: DER length varies and
    every parser disagrees on its edge cases."""
    assert der[0] == 0x30
    r_len = der[3]
    r = der[4:4 + r_len]
    s_len = der[4 + r_len + 1]
    s = der[4 + r_len + 2:4 + r_len + 2 + s_len]
    return r.lstrip(b"\x00").rjust(32, b"\x00") + s.lstrip(b"\x00").rjust(32, b"\x00")


def _raw_to_der(raw: bytes) -> bytes:
    """Inverse of _der_to_raw, used to re-verify a stored signature."""
    assert len(raw) == 64
    body = b"".join(_der_int(raw[i:i + 32]) for i in (0, 32))
    return b"\x30" + bytes([len(body)]) + body


def _der_int(x: bytes) -> bytes:
    b = x.lstrip(b"\x00") or b"\x00"
    if b[0] & 0x80:
        b = b"\x00" + b
    return b"\x02" + bytes([len(b)]) + b


if __name__ == "__main__":
    main()
