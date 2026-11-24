"""Exercise the C++ archive CLI with a live writer and a fresh restore."""
from contextlib import closing
from pathlib import Path
import json
import sqlite3
import subprocess
import sys
import tempfile
import time

collector, diagnose, archive = sys.argv[1:]


def run(*args, check=True):
    return subprocess.run(args, check=check, capture_output=True, text=True, timeout=15)


def snapshot(directory):
    return json.loads(run(diagnose, str(directory), "snapshot-json", "5").stdout)


with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    source, saved, restored = (root / name for name in ("source", "saved", "restored"))
    child = subprocess.Popen([collector, str(source)], stdout=subprocess.PIPE, text=True)
    try:
        assert child.stdout.readline().startswith("SIMULATION_ONLY")
        for _ in range(30):
            if snapshot(source)["frames"]:
                break
            time.sleep(0.05)
        else:
            raise AssertionError("collector did not commit a frame")
        first = snapshot(source)
        run(archive, "backup", str(source), str(saved))
        # The backup is self-contained and doesn't require SQLite WAL files.
        assert [p.name for p in saved.iterdir()] == ["simulation.sqlite"]
        run(archive, "restore", str(saved), str(restored))
        evidence = snapshot(restored)
        old_seq = int(evidence["frames"][0]["seq"])
        assert old_seq >= int(first["frames"][0]["seq"])
        assert evidence["runtime"]["heartbeat_utc_ms"] is None
        assert not evidence["current_frame_usable"]
        archived = snapshot(saved)
        assert not archived["current_frame_usable"]
        assert evidence["logs"] == archived["logs"] and evidence["logs"]
        assert evidence["log_retention"] == archived["log_retention"]
        assert evidence["log_retention"]["row_cap"] == 300

        before = (restored / "simulation.sqlite").read_bytes()
        assert run(archive, "restore", str(saved), str(restored), check=False).returncode
        assert before == (restored / "simulation.sqlite").read_bytes()
        run(collector, str(restored), "1", "20")
        assert int(snapshot(restored)["frames"][0]["seq"]) == old_seq + 1
        assert child.poll() is None
        for _ in range(30):
            if int(snapshot(source)["frames"][0]["seq"]) > old_seq:
                break
            time.sleep(0.05)
        else:
            raise AssertionError("original collector stopped advancing")
    finally:
        child.terminate()
        child.wait(timeout=5)

    invalid = root / "foreign"
    invalid.mkdir()
    with closing(sqlite3.connect(invalid / "simulation.sqlite")) as db:
        db.execute("CREATE TABLE foreign_data(id INTEGER)")
    failed = root / "failed"
    assert run(archive, "backup", str(invalid), str(failed), check=False).returncode
    assert not failed.exists()
    (saved / "unexpected.jsonl").write_text("synthetic legacy segment")
    assert run(archive, "restore", str(saved), str(failed), check=False).returncode
    assert not failed.exists()

    oversized = root / "oversized"
    oversized.mkdir()
    with closing(sqlite3.connect(oversized / "simulation.sqlite")) as db:
        db.executescript("""
            PRAGMA application_id=1279471699;
            PRAGMA user_version=4;
            CREATE TABLE padding(data BLOB);
            INSERT INTO padding VALUES(zeroblob(64 * 1024 * 1024));
        """)
    rejected = run(archive, "backup", str(oversized), str(failed), check=False)
    assert rejected.returncode and "64 MiB" in rejected.stderr
    assert not failed.exists()
