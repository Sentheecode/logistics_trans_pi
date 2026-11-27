"""Independent newline-delimited MCP client against a real collector and stdio process."""
from contextlib import closing
from pathlib import Path
import json
import sqlite3
import subprocess
import sys
import tempfile

collector, server, diagnose = sys.argv[1:]
station = {"station": "stn.140"}
init = {"protocolVersion": "2025-11-25", "capabilities": {},
        "clientInfo": {"name": "independent-fixture", "version": "1"}}


def rpc(method, params=None, rpc_id=None):
    value = {"jsonrpc": "2.0", "method": method}
    if params is not None:
        value["params"] = params
    if rpc_id is not None:
        value["id"] = rpc_id
    return json.dumps(value)


def tool(name, args, rpc_id, **extra):
    return rpc("tools/call", {"name": name, "arguments": args, **extra}, rpc_id)


def run(data, messages):
    proc = subprocess.run([server, str(data)], input="\n".join(messages) + "\n",
                          capture_output=True, text=True, check=True, timeout=10)
    replies = [json.loads(line) for line in proc.stdout.splitlines()]
    assert all(r["jsonrpc"] == "2.0" for r in replies)
    return {r["id"]: r for r in replies}


def result(replies, rpc_id, error=False):
    tool_result = replies[rpc_id]["result"]
    assert tool_result["isError"] == error
    value = tool_result["structuredContent"]
    assert json.loads(tool_result["content"][0]["text"]) == value
    return value


launch = [rpc("initialize", init, "init"), rpc("notifications/initialized")]
with tempfile.TemporaryDirectory() as temp:
    data = Path(temp) / "data"
    subprocess.run([collector, str(data), "16", "20"], capture_output=True, check=True, timeout=10)
    db = data / "simulation.sqlite"
    sentinel = "SYNTHETIC_CONFIDENTIAL_TEXT \" \n"
    with closing(sqlite3.connect(db)) as conn:
        latest = conn.execute("SELECT MAX(received_ms) FROM frames").fetchone()[0]
        conn.execute("INSERT INTO sim_logs(received_ms,level,category,object,text) VALUES(?,?,?,?,?)",
                     (latest, sentinel, sentinel, sentinel, sentinel))
        conn.commit()
    minute = latest // 60000 * 60000
    replies = run(data, [rpc("tools/list", {}, "early"), "{invalid", *launch,
                         rpc("tools/list", {}, "tools"), rpc("ping", {}, "ping"),
                         tool("status", station, 1), tool("alarms", station, 2),
                         tool("logs", {**station, "limit": 1}, 3),
                         tool("history", {**station, "limit": 16}, 4),
                         tool("history", {**station, "from_utc_ms": latest, "to_utc_ms": latest + 1}, 5),
                         tool("history", {**station, "from_utc_ms": minute - 60000,
                                          "to_utc_ms": minute + 60000, "bucket_ms": 60000}, 6),
                         tool("logs", {"station": "stn.999"}, 7),
                         tool("logs", {**station, "path": "/irrelevant", "sql": "irrelevant"}, 8),
                         tool("history", {**station, "limit": 1.5}, 9),
                         tool("history", {**station, "from_utc_ms": latest}, 10),
                         tool("write", station, 11),
                         tool("status", station, 12, extra=True),
                         '{"jsonrpc":"2.0","method":"ping","id":"first","id":"second"}'])
    assert replies["early"]["error"]["code"] == -32002
    assert replies[None]["error"]["code"] == -32700 and "second" not in replies
    assert replies["init"]["result"]["protocolVersion"] == init["protocolVersion"]
    assert replies["ping"]["result"] == {}
    tools = replies["tools"]["result"]["tools"]
    assert [t["name"] for t in tools] == ["status", "alarms", "logs", "history"]
    assert all(t["annotations"]["readOnlyHint"] for t in tools)
    assert result(replies, 1)["latest_observed"]["sequence"] == "16"
    assert result(replies, 2)["latest_observed"]["condition"] == "cleared"
    logs = result(replies, 3)
    assert logs["availability"] == "available" and logs["has_more_retained"]
    assert len(logs["logs"]) == 1 and logs["logs"][0]["category"] is None
    assert sentinel.split()[0] not in json.dumps(replies)
    local_snapshot = subprocess.run([diagnose, str(data), "snapshot-json", "1"], capture_output=True,
                                    check=True, timeout=10)
    assert json.loads(local_snapshot.stdout)["logs"][0]["text"] == sentinel
    history = result(replies, 4)["frames"]
    assert len(history) == 16
    assert all(row["carts"] == [None, None, None] for row in history if row["quality"] == "bad")
    assert result(replies, 5)["frames"][0]["received_utc_ms"] == latest
    rollup = result(replies, 6)
    assert sum(b["samples"] for b in rollup["buckets"]) == 16 and rollup["coverage"] == "unknown"
    tag_window = {**station, "from_utc_ms": minute - 60000, "to_utc_ms": minute + 60000,
                  "bucket_ms": 60000}
    tag_reply = run(data, [*launch,
                           tool("history", {**tag_window, "tag": "sim.completed_carts"}, 1),
                           tool("history", {**tag_window, "tag": "unknown"}, 2),
                           tool("history", {**station, "tag": "sim.blocked_carts"}, 3)])
    numeric = result(tag_reply, 1)
    cli_tag = subprocess.run([diagnose, str(data), "tag-rollup-json", "sim.completed_carts",
                              str(minute - 60000), str(minute + 60000), "60000"],
                             capture_output=True, check=True, timeout=10)
    assert numeric["buckets"] == json.loads(cli_tag.stdout)["buckets"]
    assert sum(b["usable_values"] for b in numeric["buckets"]) == 13
    for n in (2, 3):
        assert result(tag_reply, n, True) == {"error": "invalid_tool_arguments"}
    assert result(replies, 7, True) == {"error": "scope_denied"}
    for n in (8, 9, 10):
        assert result(replies, n, True) == {"error": "invalid_tool_arguments"}
    for n in (11, 12):
        assert replies[n]["error"]["code"] == -32602

    with closing(sqlite3.connect(db)) as conn:
        conn.executescript("""
            UPDATE runtime_state SET heartbeat_ms=heartbeat_ms-5000;
            UPDATE frames SET received_ms=received_ms-5000;
            DROP TABLE sim_logs; DROP TABLE sim_log_retention; PRAGMA user_version=2;
        """)
    replies = run(data, [*launch, tool("status", station, 1), tool("logs", station, 2)])
    status = result(replies, 1)
    assert status["frame_freshness"] == "stale" and status["collector"]["liveness"] == "stale"
    assert not status["current_frame_usable"]
    logs = result(replies, 2)
    assert logs["availability"] == "unsupported_schema" and logs["has_more_retained"] is None

    with closing(sqlite3.connect(db)) as conn:
        conn.executescript("""
            WITH RECURSIVE sample(n) AS (
                SELECT 1 UNION ALL SELECT n+1 FROM sample WHERE n < 400000
            )
            INSERT INTO frames(received_ms,boot,monotonic_ns,phase,quality,reason,link,alarm,event)
            SELECT 10,'fixture',n,0,'bad','fixture','down','unknown','fixture' FROM sample;
        """)
    exhausted = run(data, [*launch, tool("history", {**station, "from_utc_ms": 20, "to_utc_ms": 25}, 1),
                           tool("history", {**station, "from_utc_ms": 0, "to_utc_ms": 1000, "bucket_ms": 1000}, 2)])
    for n in (1, 2):
        assert result(exhausted, n, True) == {"error": "evidence_unavailable"}

    # Closing an MCP client leaves collector and the independent CLI usable.
    live = Path(temp) / "live"
    child = subprocess.Popen([collector, str(live)], stdout=subprocess.PIPE, text=True)
    try:
        assert child.stdout.readline().startswith("SIMULATION_ONLY")
        evidence = run(live, [*launch, tool("status", station, 1)])
        assert evidence[1]["result"]["structuredContent"]["collector"]["mode"] == "automatic"
        assert child.poll() is None
        cli = subprocess.run([diagnose, str(live), "snapshot-json"], capture_output=True, check=True, timeout=10)
        assert json.loads(cli.stdout)["scope"]["station_id"] == station["station"]
    finally:
        child.terminate()
        child.wait(timeout=5)
    alarm_data = Path(temp) / "alarm"
    subprocess.run([collector, str(alarm_data), "6", "20"], capture_output=True, check=True, timeout=10)
    active = result(run(alarm_data, [*launch, tool("alarms", station, 1)]), 1)
    assert active["latest_observed"]["condition"] == "active"
    assert result(run(Path(temp) / "missing", [*launch, tool("status", station, 1)]), 1, True) == {
        "error": "evidence_unavailable"}
    overload = subprocess.run([server, str(data)], input="x" * (16384 + 1), capture_output=True,
                              text=True, timeout=5)
    assert overload.returncode and not overload.stdout

print("PASS: independent MCP lifecycle, four scoped tools, redaction, quality, bounds and collector independence")
