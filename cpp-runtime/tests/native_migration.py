"""Synthetic event timestamps and Linux owned-child watchdog."""
import json
import os
from pathlib import Path
import queue
import signal
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
from contextlib import closing

collector, diagnose, control_client = sys.argv[1:]

def run(*args, **kw):
    return subprocess.run(args, capture_output=True, text=True, timeout=10, **kw)

with tempfile.TemporaryDirectory() as base:
    data = str(Path(base)/"rollup")
    run(collector, data, "5", "20", check=True)
    with closing(sqlite3.connect(Path(data)/"simulation.sqlite")) as db:
        # Deliberately make arrival order disagree with event time.
        fixtures = [
            (2_100, None, 3_100, "good", 1),
            (None, 1_200, 3_200, "uncertain", 2),
            (None, None, 1_300, "bad", 3),
            (1_100, 3_400, 3_400, "good", 4),
            (3_000, None, 3_500, "good", 5),  # Excluded by half-open boundary.
        ]
        db.executemany("""UPDATE frames SET source_ms=?,server_ms=?,received_ms=?,quality=?,
                       cart1='station_complete',cart2='station_complete',cart3='station_complete',
                       alarm='active' WHERE seq=?""", fixtures)
        db.commit()
    def buckets(start, stop, width):
        result = run(diagnose, data, "rollup-json", str(start), str(stop), str(width), check=True)
        payload = json.loads(result.stdout)
        assert payload["coverage"] == "unknown"
        return payload["buckets"]
    one, two = buckets(1000, 3000, 1000)
    assert one == dict(start_utc_ms=1000, samples=3, good=1, bad=1, uncertain=1,
                       unknown_quality=0, source_time=1, server_time=1, received_time=1,
                       good_complete_frames=1, good_active_alarm_frames=1)
    assert two["samples"] == 1 and two["source_time"] == 1
    assert buckets(0, 60000, 60000)[0]["samples"] == 5
    assert buckets(0, 3600000, 3600000)[0]["samples"] == 5
    assert buckets(4000, 5000, 1000) == []
    for start, stop, width in [(1, 3000, 1000), (0, 121000, 1000), (0, 3000, 5000)]:
        assert run(diagnose, data, "rollup-json", str(start), str(stop), str(width)).returncode

    # Numeric tags are fixed derived observations, with bad/null never treated as zero.
    with closing(sqlite3.connect(Path(data)/"simulation.sqlite")) as db:
        db.execute("UPDATE frames SET cart1='blocked', cart3='waiting' WHERE seq=1")
        db.execute("UPDATE frames SET quality='bad' WHERE seq=5")
        db.commit()

    def tags(tag, start, stop, width):
        result = run(diagnose, data, "tag-rollup-json", tag, str(start), str(stop), str(width), check=True)
        return json.loads(result.stdout)["buckets"]

    completed = tags("sim.completed_carts", 1000, 4000, 1000)
    assert completed == [
        dict(start_utc_ms=1000, samples=3, usable_values=1, min=3, max=3, avg=3),
        dict(start_utc_ms=2000, samples=1, usable_values=1, min=1, max=1, avg=1),
        dict(start_utc_ms=3000, samples=1, usable_values=0, min=None, max=None, avg=None),
    ]
    blocked, = tags("sim.blocked_carts", 0, 60000, 60000)
    assert blocked == dict(start_utc_ms=0, samples=5, usable_values=2, min=0, max=1, avg=0.5)
    assert run(diagnose, data, "tag-rollup-json", "unconfigured", "0", "60000", "60000").returncode

    if sys.platform == "linux":
        def start(name):
            d = Path(base)/name
            proc = subprocess.Popen([collector, "--supervise", str(d), "40"], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True)
            lines = queue.Queue()
            def read():
                for line in proc.stdout:
                    lines.put(line)
                lines.put("")
            threading.Thread(target=read, daemon=True).start()
            return proc, lines, d
        def owned_child(lines):
            deadline = time.monotonic() + 9
            while time.monotonic() < deadline:
                s = lines.get(timeout=max(.1, deadline-time.monotonic()))
                if s.startswith("collector_started pid="):
                    return int(s.partition("=")[2])
                assert s, "supervisor exited before child startup"
            raise AssertionError("no owned collector")
        def boot(d, other=None):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                result = run(diagnose, str(d), "snapshot-json", "1")
                if not result.returncode:
                    value = json.loads(result.stdout)
                    if (value["current_frame_usable"] and value["runtime"]["boot"] != other):
                        return value["runtime"]["boot"], int(value["frames"][0]["seq"])
                time.sleep(.05)
            raise AssertionError("supervisor did not publish a fresh boot")
        p, lines, d = start("watchdog")
        try:
            child = owned_child(lines)
            previous, sequence = boot(d)
            # Suspend only the collector created by this test. Its parent must stay responsive.
            os.kill(child, signal.SIGSTOP)
            new_child = owned_child(lines)
            assert new_child != child and not Path(f"/proc/{child}").exists()
            new_boot, new_sequence = boot(d, previous)
            assert new_boot != previous and new_sequence > sequence
            for _ in range(2):
                os.kill(new_child, signal.SIGKILL)
                new_child = owned_child(lines)
                new_boot, _ = boot(d, new_boot)
            os.kill(new_child, signal.SIGKILL)
            assert p.wait(timeout=8) != 0
            evidence = p.stderr.read()
            assert "collector_watchdog_timeout" in evidence and "collector_restart_budget_exhausted" in evidence
        finally:
            if p.poll() is None:
                p.kill()
                p.wait(timeout=5)
        p, lines, d = start("stop")
        try:
            child = owned_child(lines)
            start_boot, _ = boot(d)
            run(control_client, str(d), "manual", check=True)
            def manual_state():
                result = run(diagnose, str(d), "snapshot-json", "1", check=True)
                view = json.loads(result.stdout)
                assert view["runtime"]["mode"] == "manual"
                assert view["runtime"]["liveness"] == "recent"
                assert view["runtime"]["boot"] == start_boot
                return view["frames"][0]["seq"]
            held_frame = manual_state()
            time.sleep(2.25)
            assert manual_state() == held_frame
            p.terminate()
            assert p.wait(timeout=5) == 0 and not Path(f"/proc/{child}").exists()
        finally:
            if p.poll() is None:
                p.kill()
                p.wait(timeout=5)
print("PASS: event-time buckets, quality and timestamp provenance, owned-child watchdog and restart budget on Linux")
