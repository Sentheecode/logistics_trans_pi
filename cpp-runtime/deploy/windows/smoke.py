"""Validate the extracted Windows preview without a development DLL search path."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import tempfile
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_x64(binary):
    with binary.open("rb") as stream:
        require(stream.read(2) == b"MZ", f"{binary.name}: not a PE executable")
        stream.seek(0x3C)
        stream.seek(struct.unpack("<I", stream.read(4))[0])
        require(stream.read(6) == b"PE\0\0\x64\x86", f"{binary.name}: not Windows x64")


def main(package):
    require(os.name == "nt", "Smoke requires a real Windows environment")
    package = package.resolve()
    require(" " in str(package), "Extract the ZIP to a path containing spaces")
    require({path.name for path in package.glob("*.exe")} == {"Logistics.exe"},
            "The preview package must contain exactly one executable")
    require((package / "fonts" / "NotoSansCJKsc-Regular.otf").is_file(), "Missing side-by-side CJK font")
    require((package / "fonts" / "NotoSansCJKsc-Bold.otf").is_file(), "Missing side-by-side bold CJK font")
    files = {path.relative_to(package).as_posix(): path for path in package.rglob("*")
             if path.is_file() and path.name != "FILES.txt"}
    manifest = (package / "FILES.txt").read_text(encoding="utf-8").splitlines()
    require(set(files) == {line.removeprefix("./") for line in manifest}, "Package manifest mismatch")
    require("plugins/platforms/qwindows.dll" in files, "Missing Windows Qt platform plugin")
    for name, path in files.items():
        if path.suffix.lower() in {".exe", ".dll"}:
            check_x64(path)
        else:
            require(name in {"README.txt", "BUILD-INFO.txt", "START.cmd", "qt.conf"}
                    or name.startswith("licenses/") or name.startswith("fonts/"),
                    f"Unexpected package file {name}")

    environment = {key: value for key, value in os.environ.items()
                   if not key.upper().startswith(("QT_", "QML_", "MSYS", "MINGW"))}
    system = environment.get("SYSTEMROOT") or environment["SystemRoot"]
    environment["PATH"] = f"{system}\\System32;{system}"
    environment["QT_QPA_PLATFORM"] = "windows"
    environment["QT_LOGGING_RULES"] = "qt.qpa.gl=false"

    def run(*arguments, **kwargs):
        process = subprocess.run([str(package / "Logistics.exe"), *map(str, arguments)], cwd=working,
                                 env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 timeout=20, **kwargs)
        require(process.returncode == 0, f"Logistics.exe {arguments}: {process.returncode}: {process.stdout!r} {process.stderr!r}")
        return process.stdout

    def request(method, params, request_id=None):
        value = {"jsonrpc": "2.0", "method": method, "params": params}
        if request_id is not None:
            value["id"] = request_id
        return json.dumps(value)

    with tempfile.TemporaryDirectory(prefix="Logistics Windows data ") as scratch:
        working = Path(scratch)
        data = working / "Simulation data"
        screenshot = working / "Windows workbench.png"
        collector = subprocess.Popen([str(package / "Logistics.exe"), "collector", str(data)], cwd=working,
                                     env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(80):
                require(collector.poll() is None, "Collector stopped before writing data")
                if (data / "simulation.sqlite").is_file() and (data / "control.mailbox").is_file():
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Collector did not initialize")
            display = run(data, 9000, screenshot, "normal")
            require(screenshot.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"),
                    "Workbench did not capture a native Qt window")
            require(b"rendered_observations=" in display and b"link=up" in display
                    and b"quality=bad" in display, f"Missing live Qt data: {display!r}")
            require(b"cjk_glyph=yes" in display, f"Packaged workbench font has no Chinese glyph: {display!r}")
            require(collector.poll() is None, "Closing Qt stopped the collector")
            snapshot = json.loads(run("diagnose", data, "snapshot-json", 5))
            require(snapshot["simulation_only"] and snapshot["scope"]["station_id"] == "stn.140"
                    and snapshot["frames"], "Diagnostic evidence is missing")

            messages = [
                request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                       "clientInfo": {"name": "windows-smoke", "version": "1"}}, 1),
                request("notifications/initialized", {}),
                request("tools/list", {}, 2),
                request("tools/call", {"name": "status", "arguments": {"station": "stn.140"}}, 3),
                request("tools/call", {"name": "status", "arguments": {"station": "stn.999"}}, 4),
            ]
            replies = [json.loads(line) for line in run(
                "mcp", data, input=("\n".join(messages) + "\n").encode()
            ).splitlines()]
            require([reply["id"] for reply in replies] == [1, 2, 3, 4], "Invalid MCP responses")
            require(replies[0]["result"]["protocolVersion"] == "2025-11-25", "MCP version mismatch")
            require({tool["name"] for tool in replies[1]["result"]["tools"]}
                    == {"status", "alarms", "logs", "history"}, "Unexpected MCP tools")
            require(replies[2]["result"]["structuredContent"]["station"] == "stn.140"
                    and not replies[2]["result"]["isError"], "Authorized MCP evidence is missing")
            require(replies[3]["result"]["isError"]
                    and replies[3]["result"]["structuredContent"]["error"] == "scope_denied",
                    "Cross-station MCP request was not denied")
            require(collector.poll() is None, "Closing MCP stopped the collector")

            backup = working / "Archived simulation"
            restored = working / "Restored simulation"
            run("archive", "backup", data, backup)
            run("archive", "restore", backup, restored)
            recovered = json.loads(run("diagnose", restored, "snapshot-json"))
            require(recovered["frames"] and not recovered["current_frame_usable"],
                    "Restored archive is missing history or exposed stale heartbeat as live")
        finally:
            if collector.poll() is None:
                collector.terminate()
            collector.wait(timeout=5)

    print("PASS: Windows x64 ZIP, native Qt platform plugin, independent collector, CLI, MCP scope and archive")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
