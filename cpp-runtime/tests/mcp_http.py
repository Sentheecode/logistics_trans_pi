"""Independent MCP HTTP clients over loopback. Optional nginx and ephemeral mTLS."""
from contextlib import contextmanager
from pathlib import Path
import http.client
import json
import os
import secrets
import socket
import ssl
import subprocess
import sys
import tempfile
import time

HEADERS = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
           "MCP-Protocol-Version": "2025-11-25"}
INIT = {"protocolVersion": "2025-11-25", "capabilities": {},
        "clientInfo": {"name": "synthetic-http-test", "version": "1"}}


def post(port, method="tools/list", params=None, *, token=None, tls=None, extra=None, rpc_id=1, body=None, path="/mcp", http_method="POST"):
    headers = dict(HEADERS)
    headers.update(extra or {})
    if token:
        headers["Authorization"] = "Bearer " + token
    message = {"jsonrpc": "2.0", "method": method, "params": params or {}}
    if rpc_id is not None:
        message["id"] = rpc_id
    link = (http.client.HTTPSConnection("localhost", port, timeout=4, context=tls) if tls else
            http.client.HTTPConnection("127.0.0.1", port, timeout=4))
    try:
        link.request(http_method, path, body if body is not None else json.dumps(message), headers)
        answer = link.getresponse()
        raw = answer.read()
        return answer.status, json.loads(raw) if raw and answer.getheader("Content-Type") == "application/json" else None
    finally:
        link.close()


def tool(port, station, **kwargs):
    status, reply = post(port, "tools/call", {"name": "status", "arguments": {"station": station}}, **kwargs)
    assert status == 200, (status, reply)
    return reply["result"]


def launch_client(scenario, port, root=None, cert="none"):
    subprocess.run([sys.executable, __file__, "--client", scenario, str(port), str(root or ""), cert],
                   check=True, timeout=10, env=os.environ.copy())


def client(scenario, port, root, cert):
    token = os.environ["LTP_MCP_GATEWAY_TOKEN"] if scenario.startswith("direct") else None
    tls = None
    if root:
        tls = ssl.create_default_context(cafile=str(Path(root) / "server-ca.crt"))
        if cert != "none":
            tls.load_cert_chain(str(Path(root) / f"{cert}.crt"), str(Path(root) / f"{cert}.key"))
    if scenario.endswith("-denied"):
        denied, _ = post(port, tls=tls)
        assert denied in (400, 401, 403), denied
        if not tls:
            assert post(port, token="0" * 64)[0] == 401
        return
    status, reply = post(port, "initialize", INIT, token=token, tls=tls)
    assert status == 200 and reply["result"]["protocolVersion"] == INIT["protocolVersion"], (status, reply)
    assert post(port, "notifications/initialized", token=token, tls=tls, rpc_id=None) == (202, None)
    assert post(port, token=token, tls=tls)[0] == 200
    assert post(port, token=token, tls=tls, http_method="GET")[0] == 405
    assert post(port, token=token, tls=tls, path="/other")[0] == 404
    assert tool(port, "stn.140", token=token, tls=tls)["structuredContent"]["station"] == "stn.140"
    denied = tool(port, "stn.999", token=token, tls=tls)
    assert denied["isError"] and denied["structuredContent"] == {"error": "scope_denied"}
    assert post(port, token=token, tls=tls, extra={"Host": "other.invalid"})[0] == 403
    assert post(port, token=token, tls=tls, extra={"Origin": "https://other.invalid"})[0] == 403
    assert post(port, token=token, tls=tls, body="a" * 16385)[0] == 413
    assert post(port, token=token, tls=tls, extra={"MCP-Protocol-Version": "0000-00-00"})[0] == 400


def port_number():
    with socket.socket(socket.AF_INET) as reservation:
        reservation.bind(("127.0.0.1", 0))
        return reservation.getsockname()[1]


@contextmanager
def running(args, port):
    with tempfile.TemporaryFile() as log:
        proc = subprocess.Popen(args, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    break
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                        break
                except OSError:
                    time.sleep(0.05)
            else:
                raise AssertionError("loopback process never listened")
            assert proc.poll() is None
            yield
        except Exception:
            log.seek(0)
            print(log.read().decode(errors="replace"), file=sys.stderr)
            raise
        finally:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5)


def create_certificates(openssl, root):
    def command(*args):
        subprocess.run([openssl, *args], cwd=root, check=True, capture_output=True, timeout=10)

    for ca in ("server-ca", "client-ca"):
        command("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                "-subj", f"/CN={ca}", "-addext", "basicConstraints=critical,CA:TRUE",
                "-addext", "keyUsage=critical,keyCertSign,cRLSign",
                "-keyout", f"{ca}.key", "-out", f"{ca}.crt")
    extensions = root / "extensions"
    for name, ca, serial in (("localhost", "server-ca", "09"), ("client-a", "client-ca", "0A"),
                             ("client-b", "client-ca", "0B")):
        extensions.write_text("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n"
                              + ("subjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth\n"
                                 if name == "localhost" else "extendedKeyUsage=clientAuth\n"))
        command("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", f"/CN={name}",
                "-keyout", f"{name}.key", "-out", f"{name}.csr")
        command("x509", "-req", "-in", f"{name}.csr", "-CA", f"{ca}.crt", "-CAkey", f"{ca}.key",
                "-set_serial", f"0x{serial}", "-days", "1", "-extfile", str(extensions), "-out", f"{name}.crt")


def main():
    if sys.argv[1] == "--client":
        client(sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5])
        return

    collector, http, *gateway = sys.argv[1:]
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        data = root / "simulation"
        subprocess.run([collector, str(data), "5", "20"], check=True, capture_output=True, timeout=10)
        token = secrets.token_hex(32)
        os.environ["LTP_MCP_GATEWAY_TOKEN"] = token
        backend = port_number()
        assert subprocess.run([http, str(backend), str(data)], capture_output=True, timeout=3).returncode == 2
        with running([http, "--enable-loopback", str(backend), str(data)], backend):
            launch_client("direct-allowed", backend)
            launch_client("direct-denied", backend)
            if not gateway:
                return
            nginx, openssl, template = gateway
            port = port_number()
            create_certificates(openssl, root)
            text = Path(template).read_text()
            fields = {"GATEWAY_BIND": f"127.0.0.1:{port}", "GATEWAY_NAME": "localhost", "GATEWAY_PORT": port,
                      "SERVER_CERT": root / "localhost.crt", "SERVER_KEY": root / "localhost.key",
                      "CLIENT_CA": root / "client-ca.crt", "AUDIT_LOG": root / "audit.log",
                      "ALLOWED_CLIENT_SERIAL": "0A", "BACKEND_PORT": backend, "BACKEND_TOKEN": token}
            for key, value in fields.items():
                text = text.replace(f"@{key}@", str(value))
            config = root / "nginx.conf"
            config.write_text(text)
            with running([nginx, "-p", f"{root}/", "-c", str(config), "-g", "daemon off;"], port):
                launch_client("gateway-allowed", port, root, "client-a")
                launch_client("gateway-denied", port, root, "client-b")
                launch_client("gateway-denied", port, root, "none")
            audit = (root / "audit.log").read_text()
            assert "serial=0A status=200" in audit and "serial=0B status=403" in audit
            assert token not in audit and "stn." not in audit


if __name__ == "__main__":
    main()
