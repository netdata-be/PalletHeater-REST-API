#!/usr/bin/env python3
"""Build certs/x509_crt_bundle.bin: the root CAs the firmware trusts for HTTPS
(update check and download from GitHub, see src/fwUpdate.h).

Only the roots in ROOTS, not the whole Mozilla store (~140 roots, 54 KB of flash;
these 10 are about 4 KB). Checked 20/11/2026:
    api.github.com, github.com                       -> USERTrust ECC Certification Authority
    release-assets / objects.githubusercontent.com   -> ISRG Root X1
The others are margin for a CA change at GitHub: the other roots of the same CAs, and
DigiCert, which GitHub used before. If the update check fails with a certificate error
after such a change, add the new root to ROOTS and run this again (--all puts the whole
store in, as before).

Format = the ESP-IDF certificate bundle (same as tools/gen_crt_bundle.py of the
Arduino core): only subject name + public key per root, sorted on subject so the
ESP32 can binary search it. Used with WiFiClientSecure::setCACertBundle().
The chosen roots are also written readable to certs/ca_roots.pem (in git, so a change
shows in the diff).

Regenerate (once a year or when GitHub changes CA):
    curl -o /tmp/cacert.pem https://curl.se/ca/cacert.pem      # Mozilla roots
    python3 -m pip install cryptography
    python3 scripts/gen_ca_bundle.py /tmp/cacert.pem certs/x509_crt_bundle.bin [--all]
"""
import os
import re
import struct
import sys

from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.x509.oid import NameOID

ROOTS = [
    "USERTrust ECC Certification Authority",
    "USERTrust RSA Certification Authority",
    "Sectigo Public Server Authentication Root E46",
    "Sectigo Public Server Authentication Root R46",
    "ISRG Root X1",
    "ISRG Root X2",
    "DigiCert Global Root G2",
    "DigiCert Global Root G3",
    "DigiCert TLS ECC P384 Root G5",
    "DigiCert TLS RSA4096 Root G5",
]


def cn(c):
    a = c.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
    return a[0].value if a else ""


def main(src, dst, everything):
    pem = open(src, encoding="utf-8").read()
    blocks = re.findall(r"-----BEGIN CERTIFICATE-----[\s\S]+?-----END CERTIFICATE-----", pem)
    certs = [(b, x509.load_pem_x509_certificate(b.encode())) for b in blocks]
    if not everything:
        certs = [(b, c) for b, c in certs if cn(c) in ROOTS]
        missing = set(ROOTS) - {cn(c) for _, c in certs}
        if missing:
            sys.exit("not in " + src + ": " + ", ".join(sorted(missing)))
        with open(os.path.join(os.path.dirname(dst), "ca_roots.pem"), "w") as f:
            f.write("# Root CAs in x509_crt_bundle.bin, made by scripts/gen_ca_bundle.py\n\n")
            for b, c in sorted(certs, key=lambda x: ROOTS.index(cn(x[1]))):
                f.write(f"# {cn(c)} (valid until {c.not_valid_after_utc:%d/%m/%Y})\n{b}\n\n")
    certs.sort(key=lambda x: x[1].subject.public_bytes())
    out = struct.pack(">H", len(certs))
    for _, c in certs:
        name = c.subject.public_bytes()
        key = c.public_key().public_bytes(serialization.Encoding.DER,
                                          serialization.PublicFormat.SubjectPublicKeyInfo)
        out += struct.pack(">HH", len(name), len(key)) + name + key
    with open(dst, "wb") as f:
        f.write(out)
    print(f"{len(certs)} root certificates, {len(out)} bytes -> {dst}")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--all"]
    if len(args) != 2:
        sys.exit(__doc__)
    main(args[0], args[1], "--all" in sys.argv)
