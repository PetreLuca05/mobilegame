#!/usr/bin/env python3
"""Build assets/cacert.pem from Mozilla's root store (python `certifi`),
keeping only roots that mongoose's built-in TLS can parse and use:
RSA (<= 4096 bit) or EC P-256/P-384 keys, self-signed with SHA-256/384.
Mongoose skips other roots on every handshake anyway; dropping them keeps
handshakes fast. Usage: pip install certifi cryptography; python3 tools/gen_ca_bundle.py
"""
from pathlib import Path

import certifi
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa

ROOT = Path(__file__).resolve().parent.parent
OK_SIG = {
    "1.2.840.113549.1.1.11",  # sha256WithRSAEncryption
    "1.2.840.113549.1.1.12",  # sha384WithRSAEncryption
    "1.2.840.10045.4.3.2",    # ecdsa-with-SHA256
    "1.2.840.10045.4.3.3",    # ecdsa-with-SHA384
}


def usable(cert):
    if cert.signature_algorithm_oid.dotted_string not in OK_SIG:
        return False
    key = cert.public_key()
    if isinstance(key, rsa.RSAPublicKey):
        return key.key_size <= 4096
    if isinstance(key, ec.EllipticCurvePublicKey):
        return key.curve.name in ("secp256r1", "secp384r1")
    return False


def main():
    pem = Path(certifi.where()).read_bytes()
    certs = x509.load_pem_x509_certificates(pem)
    kept = [c for c in certs if usable(c)]
    out = [f"# Mozilla root store via certifi {certifi.__version__}, filtered for mongoose built-in TLS\n".encode()]
    for c in kept:
        out.append(f"# {c.subject.rfc4514_string()}\n".encode())
        out.append(c.public_bytes(serialization.Encoding.PEM))
    (ROOT / "assets/cacert.pem").write_bytes(b"".join(out))
    names = " ".join(sorted(c.subject.rfc4514_string().split(",")[0] for c in kept
                            if any(k in c.subject.rfc4514_string() for k in ("ISRG", "GTS", "SSL.com", "DigiCert", "Amazon"))))
    print(f"kept {len(kept)} of {len(certs)} roots; includes e.g.: {names[:400]}")


if __name__ == "__main__":
    main()
