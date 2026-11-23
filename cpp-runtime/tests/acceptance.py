"""Black-box acceptance, synthetic data only; no network or production services."""
from contextlib import closing
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import time

collector, diagnose = sys.argv[1:]

def run(*args, ok=True, **kwargs):
    p = subprocess.run(args, capture_output=True, text=True, timeout=15, **kwargs)
    assert (p.returncode == 0) == ok, (args, p.returncode, p.stdout, p.stderr)
    return p

def rows(directory):
    with closing(sqlite3.connect((directory / "simulation.sqlite").resolve().as_uri() + "?mode=ro", uri=True)) as db:
        return db.execute("select seq,phase,cart1,cart2,cart3,quality,reason,link,alarm,source_ms,boot from frames order by seq").fetchall()

def wait_for(predicate):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except (sqlite3.Error, FileNotFoundError):
            pass
        time.sleep(.03)
    raise AssertionError("condition did not become true")

with tempfile.TemporaryDirectory(prefix="cpp-simulation-") as tmp:
    root = pathlib.Path(tmp)
    d = root / "dedicated"
    run(collector, str(d), "16", "20")
    r = rows(d)
    assert len(r) == 16
    assert r[0][2:5] == ("scanning", "waiting", "waiting")
    assert r[3][2:5] == ("station_complete", "scanning", "waiting")
    assert r[9][2:5] == ("station_complete", "station_complete", "scanning")
    assert r[5][8] == "active" and r[7][8] == "cleared"
    assert r[6][2:5] == (None, None, None) and r[6][5:7] == ("bad", "source_bad")
    assert r[10][5:10] == ("bad", "disconnected", "down", "unknown", None)
    assert r[12][5:8] == ("good", "simulated", "up")
    assert r[15][2:5] == ("station_complete",) * 3
    output = run(diagnose, str(d), "history", "200").stdout
    assert "UNKNOWN" in output and "coverage=unknown" in output and "more=false" in output
    assert "more=true" in run(diagnose, str(d), "history", "2").stdout
    for args in [("history", "201"), ("history", "0"), ("history", "1 OR 1"), ("status", "2"), ("sql",), ("write",), ("help", "x")]:
        run(diagnose, str(d), *args, ok=False)
    run(diagnose, str(root / "missing"), "status", ok=False)
    assert not (root / "missing").exists()
    run(collector, str(d), "1", "20")
    r2 = rows(d)
    assert r2[:16] == r and r2[-1][0] == 17 and r2[-1][10] != r[-1][10]
    p = subprocess.Popen([collector, str(d)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        wait_for(lambda: len(rows(d)) >= 18)
        run(collector, str(d), "1", "20", ok=False)
        run(diagnose, str(d), input="status\nhistory 2\nalarms\nexit\n")
        n = len(rows(d))
        wait_for(lambda: len(rows(d)) > n)
        assert p.poll() is None
        # Kill only this test's collector. Committed history survives and lease is released.
        p.kill()
        p.wait(timeout=5)
        before = rows(d)
        run(collector, str(d), "1", "20")
        assert rows(d)[:-1] == before
    finally:
        if p.poll() is None:
            p.terminate()
            p.wait(timeout=5)
    time.sleep(3.1)
    assert "freshness=stale" in run(diagnose, str(d), "status").stdout
    other = root / "unrelated"
    other.mkdir()
    (other / "runtime.sqlite").write_text("synthetic sentinel")
    run(collector, str(other), "1", "20", ok=False)
    assert (other / "runtime.sqlite").read_text() == "synthetic sentinel"
    wrong = root / "wrong-schema"
    wrong.mkdir()
    with closing(sqlite3.connect(wrong / "simulation.sqlite")) as db:
        db.execute("create table unrelated(x)")
    run(collector, str(wrong), "1", "20", ok=False)
    run(diagnose, str(wrong), "status", ok=False)
print("PASS: order, anomaly, null quality, reconnect, durable restart/crash, single writer, bounded read-only CLI, independent lifecycle, stale evidence, foreign-directory/schema refusal")
