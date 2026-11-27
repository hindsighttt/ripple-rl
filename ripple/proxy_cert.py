"""Maintain the local config listener's certificate in the mitmproxy state directory."""

import os
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path
from tempfile import NamedTemporaryFile

from cryptography import x509
from cryptography.exceptions import InvalidSignature, UnsupportedAlgorithm
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


HOSTS = ("config.psynet.gg", "config.localhost")


def ensure_config_certificate(directory: Path) -> None:
    ca_data = (directory / "mitmproxy-ca.pem").read_bytes()
    ca = x509.load_pem_x509_certificate(ca_data)
    now = datetime.now(timezone.utc)
    expires = min(ca.not_valid_after_utc, now + timedelta(days=365))
    if expires <= now or ca.not_valid_before_utc > now:
        raise ValueError("The mitmproxy root certificate is outside its validity period.")
    target = directory / "cfgcert-combined.pem"
    try:
        data = target.read_bytes()
        existing = x509.load_pem_x509_certificate(data)
        key = serialization.load_pem_private_key(data, password=None)
        names = existing.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
        existing.verify_directly_issued_by(ca)
        if (existing.not_valid_before_utc <= now
                and existing.not_valid_after_utc > now + timedelta(days=1)
                and set(HOSTS) <= set(names.get_values_for_type(x509.DNSName))
                and existing.public_key() == key.public_key()):
            return
    except (FileNotFoundError, ValueError, TypeError, InvalidSignature, UnsupportedAlgorithm, x509.ExtensionNotFound):
        pass

    ca_key = serialization.load_pem_private_key(ca_data, password=None)
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    certificate = (
        x509.CertificateBuilder()
        .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, HOSTS[0])]))
        .issuer_name(ca.subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(minutes=5))
        .not_valid_after(expires)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(x509.SubjectAlternativeName([x509.DNSName(host) for host in HOSTS]), critical=False)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        .sign(ca_key, hashes.SHA256())
    )
    certificate.verify_directly_issued_by(ca)
    temp = None
    try:
        with NamedTemporaryFile(dir=directory, prefix="cfgcert-", delete=False) as stream:
            temp = Path(stream.name)
            stream.write(certificate.public_bytes(serialization.Encoding.PEM))
            stream.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                           serialization.NoEncryption()))
        os.replace(temp, target)
    finally:
        if temp is not None:
            temp.unlink(missing_ok=True)


if __name__ == "__main__":
    ensure_config_certificate(Path(sys.argv[1]))
