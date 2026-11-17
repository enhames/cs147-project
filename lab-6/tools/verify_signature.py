#!/usr/bin/env python3
"""Verify an ATECC608A (ECDSA P-256 / ES256) signature from Lab 6.

The device publishes:
  iot/<TEAM_ID>/telemetry      the exact message bytes that were signed
  iot/<TEAM_ID>/telemetry/sig  {"seq":..,"alg":"ES256","sig":"<128 hex chars = r||s>"}
and prints its public key at boot (128 hex chars = X||Y), also retained on iot/<TEAM_ID>/pubkey.

The firmware computes SHA-256(message) and the chip signs that digest with the private key
in slot 0. Verifying with ECDSA(SHA-256) over the message checks exactly that.

Usage:
  python verify_signature.py --pubkey <hex X||Y | PEM file> --msg '<telemetry JSON>' --sig <hex or base64>
  python verify_signature.py --pubkey key.pem --msg-file msg.txt --sig <hex>
  python verify_signature.py --log serial.txt    # check every PUB/SIG pair in a saved Serial log
  python verify_signature.py --selftest          # checks this script without a board

Exit code 0 = valid, 1 = invalid, 2 = bad input.
Needs: pip install cryptography   (falls back to: pip install ecdsa)
"""
import argparse
import base64
import binascii
import hashlib
import os
import sys


def parse_bytes(text, expected_len, what):
    """Accept hex (with or without spaces/colons/0x) or base64."""
    t = text.strip().strip('"')
    h = t.replace(" ", "").replace(":", "")
    if h.lower().startswith("0x"):
        h = h[2:]
    try:
        raw = binascii.unhexlify(h)
        if len(raw) in expected_len:
            return raw
    except (binascii.Error, ValueError):
        pass
    try:
        raw = base64.b64decode(t, validate=True)
        if len(raw) in expected_len:
            return raw
    except (binascii.Error, ValueError):
        pass
    sys.exit(f"ERROR: {what} must be hex or base64 of {' or '.join(map(str, expected_len))} bytes")


def load_pubkey_point(arg):
    """Return the 65-byte uncompressed point 04||X||Y."""
    if os.path.isfile(arg):
        data = open(arg, "rb").read()
        if b"BEGIN PUBLIC KEY" in data:
            from cryptography.hazmat.primitives import serialization
            from cryptography.hazmat.primitives.asymmetric import ec
            key = serialization.load_pem_public_key(data)
            if not isinstance(key, ec.EllipticCurvePublicKey) or key.curve.name != "secp256r1":
                sys.exit("ERROR: PEM key is not a P-256 public key")
            return key.public_bytes(serialization.Encoding.X962,
                                    serialization.PublicFormat.UncompressedPoint)
        arg = data.decode().strip()
    raw = parse_bytes(arg, (64, 65), "public key")
    return raw if len(raw) == 65 else b"\x04" + raw


def verify_cryptography(point, msg, sig):
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
    key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), point)
    r = int.from_bytes(sig[:32], "big")
    s = int.from_bytes(sig[32:], "big")
    try:
        key.verify(encode_dss_signature(r, s), msg, ec.ECDSA(hashes.SHA256()))
        return True
    except InvalidSignature:
        return False


def verify_ecdsa_lib(point, msg, sig):
    import ecdsa
    vk = ecdsa.VerifyingKey.from_string(point[1:], curve=ecdsa.NIST256p)
    try:
        return vk.verify(sig, msg, hashfunc=hashlib.sha256)
    except ecdsa.BadSignatureError:
        return False


def verify(point, msg, sig):
    try:
        return verify_cryptography(point, msg, sig)
    except ImportError:
        return verify_ecdsa_lib(point, msg, sig)


def selftest():
    """Sign like the ATECC608A does (sign a SHA-256 digest, raw r||s) and verify."""
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    priv = ec.generate_private_key(ec.SECP256R1())
    point = priv.public_key().public_bytes(serialization.Encoding.X962,
                                           serialization.PublicFormat.UncompressedPoint)
    msg = b'{"team":"T00","seq":1,"t_ms":5000,"temp_c":22.41,"press_hpa":1012.33}'
    digest = hashlib.sha256(msg).digest()
    der = priv.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(der)
    sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    ok = verify(point, msg, sig)
    bad = verify(point, msg.replace(b"22.41", b"99.99"), sig)
    print("pubkey:", point[1:].hex())
    print("msg:   ", msg.decode())
    print("sig:   ", sig.hex())
    print("valid signature accepted:", ok, "| tampered message rejected:", not bad)
    return 0 if ok and not bad else 1


def verify_log(path, pubkey_arg):
    """Verify each 'PUB <topic>  {json}' line followed by a 'SIG <hex>' line in a Serial log."""
    import re
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    point = load_pubkey_point(pubkey_arg) if pubkey_arg else None
    msg, good, bad = None, 0, 0
    for i, line in enumerate(lines):
        if point is None and "Device public key" in line and i + 1 < len(lines):
            point = load_pubkey_point(lines[i + 1].strip())
            print("public key from log:", lines[i + 1].strip())
        m = re.search(r"PUB \S+/telemetry\s+(\{.*\})\s*$", line)
        if m:
            msg = m.group(1).encode()
            continue
        m = re.search(r"SIG ([0-9a-fA-F]{128})", line)
        if m and msg is not None:
            if point is None:
                sys.exit("ERROR: no public key in the log; pass --pubkey")
            ok = verify(point, msg, bytes.fromhex(m.group(1)))
            good, bad = good + ok, bad + (not ok)
            print("VALID  " if ok else "INVALID", msg.decode())
            msg = None
    print(f"{good} valid, {bad} invalid")
    return 0 if good > 0 and bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(description="Verify a Lab 6 ATECC608A ES256 signature.")
    ap.add_argument("--pubkey", help="128 hex chars (X||Y), 130 with 04 prefix, base64, or a file (hex or PEM)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--msg", help="the exact telemetry payload that was signed (quote it)")
    g.add_argument("--msg-file", help="file holding the exact payload (no trailing newline is added)")
    ap.add_argument("--sig", help="64-byte signature r||s as hex (128 chars) or base64")
    ap.add_argument("--log", help="a saved Serial Monitor log: verify every PUB/SIG pair in it")
    ap.add_argument("--selftest", action="store_true", help="test this script with a software key")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    if a.log:
        return verify_log(a.log, a.pubkey)
    if not (a.pubkey and a.sig and (a.msg is not None or a.msg_file)):
        ap.print_help()
        return 2

    point = load_pubkey_point(a.pubkey)
    msg = a.msg.encode() if a.msg is not None else open(a.msg_file, "rb").read().rstrip(b"\r\n")
    sig = parse_bytes(a.sig, (64,), "signature")
    print("SHA-256(msg):", hashlib.sha256(msg).hexdigest())
    if verify(point, msg, sig):
        print("VALID: this message was signed by the key in that ATECC608A.")
        return 0
    print("INVALID: wrong key, wrong signature, or the message is not byte-for-byte identical.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
