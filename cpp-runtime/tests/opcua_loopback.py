"""Give the isolated fixture an ephemeral IPv4 loopback port and optional test PKI."""
from pathlib import Path
import socket
import subprocess
import sys
import tempfile

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]

if len(sys.argv) == 2:
    subprocess.run([sys.argv[1], str(port)], timeout=10, check=True)
else:
    with tempfile.TemporaryDirectory() as directory:
        args = [sys.argv[1], str(port)]
        for name in ("server", "client", "rogue"):
            cert = Path(directory) / (name + ".der")
            key = Path(directory) / (name + ".key")
            app = "server" if name == "server" else "client"
            subprocess.run([
                sys.argv[2], "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                "-subj", "/CN=fixture-" + name, "-addext", "basicConstraints=critical,CA:FALSE",
                "-addext", "keyUsage=critical,digitalSignature,nonRepudiation,keyEncipherment,dataEncipherment",
                "-addext", "extendedKeyUsage=serverAuth,clientAuth",
                "-addext", "subjectAltName=URI:urn:synthetic:" + app + ",IP:127.0.0.1",
                "-outform", "DER", "-out", str(cert), "-keyout", str(key),
            ], capture_output=True, check=True, timeout=8)
            args.extend([str(cert), str(key)])
        subprocess.run(args, timeout=18, check=True)
