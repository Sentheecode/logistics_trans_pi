"""Give the isolated fixture an ephemeral IPv4 loopback port."""
import socket
import subprocess
import sys

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]

subprocess.run([sys.argv[1], str(port)], timeout=10, check=True)
