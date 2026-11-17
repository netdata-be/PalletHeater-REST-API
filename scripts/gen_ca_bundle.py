#!/usr/bin/env python3
"""Build certs/x509_crt_bundle.bin: the root CAs the firmware trusts for HTTPS
(update check and download from GitHub, see src/fwUpdate.h).

Format = the ESP-IDF certificate bundle (same as tools/gen_crt_bundle.py of the
Arduino core): only subject name + public key per root, sorted on subject so the
ESP32 can binary search it. Used with WiFiClientSecure::setCACertBundle().

Regenerate (once a year or when GitHub changes CA):
    curl -o /tmp/cacert.pem https://curl.se/ca/cacert.pem      # Mozilla roots
    python3 -m pip install cryptography
    python3 scripts/gen_ca_bundle.py /tmp/cacert.pem certs/x509_crt_bundle.bin
"""
import re
import struct
import sys

from cryptography import x509
from cryptography.hazmat.primitives import serialization


def main(src, dst):
    pem = open(src, encoding="utf-8").read()
    blocks = re.findall(r"-----BEGIN CERTIFICATE-----[\s\S]+?-----END CERTIFICATE-----", pem)
    certs = [x509.load_pem_x509_certificate(b.encode()) for b in blocks]
    certs.sort(key=lambda c: c.subject.public_bytes())
    out = struct.pack(">H", len(certs))
    for c in certs:
        name = c.subject.public_bytes()
        key = c.public_key().public_bytes(serialization.Encoding.DER,
                                          serialization.PublicFormat.SubjectPublicKeyInfo)
        out += struct.pack(">HH", len(name), len(key)) + name + key
    with open(dst, "wb") as f:
        f.write(out)
    print(f"{len(certs)} root certificates, {len(out)} bytes -> {dst}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
