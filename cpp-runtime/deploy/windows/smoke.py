"""Validate the extracted Windows preview without a development DLL search path."""
from contextlib import closing
import ctypes
from ctypes import wintypes
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
import sqlite3


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_x64(binary):
    with binary.open("rb") as stream:
        require(stream.read(2) == b"MZ", f"{binary.name}: not a PE executable")
        stream.seek(0x3C)
        pe_offset = struct.unpack("<I", stream.read(4))[0]
        stream.seek(pe_offset)
        require(stream.read(6) == b"PE\0\0\x64\x86", f"{binary.name}: not Windows x64")
        if binary.name == "Logistics.exe":
            stream.seek(pe_offset + 24 + 68)
            require(struct.unpack("<H", stream.read(2))[0] == 2, "Launcher must use the GUI subsystem")


def main(package):
    require(os.name == "nt", "Smoke requires a real Windows environment")
    package = package.resolve()
    require(" " in str(package), "Extract the ZIP to a path containing spaces")
    expected_exes = {"Logistics.exe", "collector.exe", "diagnose.exe", "hmi.exe",
                     "logistics-mcp.exe", "simulation-archive.exe"}
    require({path.name for path in package.glob("*.exe")} == expected_exes,
            "Unexpected executable set")
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
                    or name.startswith("licenses/"), f"Unexpected package file {name}")

    environment = {key: value for key, value in os.environ.items()
                   if not key.upper().startswith(("QT_", "QML_", "MSYS", "MINGW"))}
    system = environment.get("SYSTEMROOT") or environment["SystemRoot"]
    environment["PATH"] = f"{system}\\System32;{system}"
    environment["QT_QPA_PLATFORM"] = "windows"
    environment["QT_LOGGING_RULES"] = "qt.qpa.gl=false"

    def run(name, *arguments, **kwargs):
        process = subprocess.run([str(package / name), *map(str, arguments)], cwd=working,
                                 env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 timeout=20, **kwargs)
        require(process.returncode == 0, f"{name}: {process.returncode}: {process.stdout!r} {process.stderr!r}")
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
        collector = subprocess.Popen([str(package / "collector.exe"), str(data)], cwd=working,
                                     env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(80):
                require(collector.poll() is None, "Collector stopped before writing data")
                if (data / "simulation.sqlite").is_file() and (data / "control.mailbox").is_file():
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Collector did not initialize")
            display = run("hmi.exe", data, 9000, screenshot, "normal")
            require(screenshot.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"),
                    "Workbench did not capture a native Qt window")
            require(b"rendered_observations=" in display and b"link=up" in display
                    and b"quality=bad" in display, f"Missing live Qt data: {display!r}")
            require(collector.poll() is None, "Closing Qt stopped the collector")
            snapshot = json.loads(run("diagnose.exe", data, "snapshot-json", 5))
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
                "logistics-mcp.exe", data, input=("\n".join(messages) + "\n").encode()
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
            run("simulation-archive.exe", "backup", data, backup)
            run("simulation-archive.exe", "restore", backup, restored)
            recovered = json.loads(run("diagnose.exe", restored, "snapshot-json"))
            require(recovered["frames"] and not recovered["current_frame_usable"],
                    "Restored archive is missing history or exposed stale heartbeat as live")
        finally:
            if collector.poll() is None:
                collector.terminate()
            collector.wait(timeout=5)

        launched_data = working / "Hidden simulation"
        launcher_env = dict(environment, LTP_PREVIEW_DATA_DIR=str(launched_data))
        subprocess.run([str(package / "Logistics.exe")], env=launcher_env, cwd=working,
                       check=True, timeout=20)
        manifests = sorted((package / "preview-logs").glob("*/processes.txt"))
        require(len(manifests) == 1, "Missing launcher process IDs")
        pids = {name: int(pid) for name, pid in
                (line.split() for line in manifests[0].read_text().splitlines())}
        require(set(pids) == {"collector", "hmi"}, "Unexpected launcher process list")
        require((manifests[0].parent / "hmi.log").exists(), "Missing HMI output file")
        windows = ctypes.windll.user32
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        windows.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        windows.IsWindowVisible.argtypes = [wintypes.HWND]
        windows.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

        def find_hmi_window():
            handles = []

            @callback_type
            def visit(hwnd, _):
                pid = wintypes.DWORD()
                windows.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == pids["hmi"] and windows.IsWindowVisible(hwnd):
                    handles.append(hwnd)
                return True

            windows.EnumWindows(visit, 0)
            return handles[0] if handles else None

        def sample_id():
            try:
                with closing(sqlite3.connect((launched_data / "simulation.sqlite").as_uri()+"?mode=ro", uri=True)) as db:
                    return db.execute("select coalesce(max(seq),0) from frames").fetchone()[0]
            except sqlite3.OperationalError:
                return 0

        def await_value(probe, reason):
            until = time.monotonic() + 15
            while time.monotonic() < until:
                value = probe()
                if value:
                    return value
                time.sleep(0.1)
            raise RuntimeError(reason)

        try:
            hwnd = await_value(find_hmi_window, "Launcher did not open HMI")
            before = await_value(sample_id, "Hidden collector did not commit a frame")
            require(windows.PostMessageW(hwnd, 0x0010, 0, 0), "HMI close could not be delivered")
            await_value(lambda: find_hmi_window() is None, "HMI did not close")
            after_close = sample_id()
            await_value(lambda: sample_id() > max(before, after_close), "Closing the HMI stopped hidden sampling")
            require("SIMULATION_ONLY" in (manifests[0].parent / "collector.log").read_text(),
                    "Missing background collector startup record")
        finally:
            for pid in pids.values():
                subprocess.run(["taskkill", "/PID", str(pid), "/F"], env=environment,
                               capture_output=True, timeout=10)

    print("PASS: Windows x64 ZIP, native Qt platform plugin, independent collector, CLI, MCP scope and archive")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
