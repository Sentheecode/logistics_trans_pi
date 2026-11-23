"""Schema v3 log bounds: migration, capacity, no per-frame collector spam, single writer."""
from contextlib import closing
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import time

collector, client = sys.argv[1:]

def query(path, sql):
    uri = pathlib.Path(path).resolve().as_uri() + "?mode=ro"
    with closing(sqlite3.connect(uri, uri=True)) as db:
        return db.execute(sql).fetchall()

def counts(path):
    frames = query(path, "select count(*) from frames")[0][0]
    logs = query(path, "select count(*) from sim_logs")[0][0]
    collector_rows = query(path, "select count(*) from sim_logs where category='collector'")[0][0]
    version = query(path, "pragma user_version")[0][0]
    return frames, logs, collector_rows, version

with tempfile.TemporaryDirectory() as tmp:
    path = tmp + "/simulation.sqlite"
    first = subprocess.Popen([collector, tmp], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        end = time.monotonic() + 6
        while time.monotonic() < end:
            try:
                if query(path, "select count(*) from frames")[0][0] > 0:
                    break
            except sqlite3.Error:
                pass
            time.sleep(0.05)
        else:
            raise AssertionError("collector did not write a frame")
        second = subprocess.run([collector, tmp, "1", "20"], capture_output=True, text=True, timeout=8)
        if second.returncode == 0:
            raise AssertionError("second collector took the writer lease")
    finally:
        first.terminate()
        first.wait(timeout=5)

    one = subprocess.run([collector, tmp, "40", "20"], capture_output=True, text=True, timeout=12)
    if one.returncode != 0:
        raise AssertionError(one.stderr)
    frames, logs, collector_rows, version = counts(path)
    if version != 3 or frames < 30 or logs < 1 or logs > 300 or collector_rows >= frames:
        raise AssertionError((frames, logs, collector_rows, version))
    texts = [row[0] for row in query(path, "select text from sim_logs where category='collector' order by id")]
    for previous, current in zip(texts, texts[1:]):
        if previous == current:
            raise AssertionError(("repeated collector state", previous))
    down_frames = query(path, "select count(*) from frames where link='down'")[0][0]
    down_logs = sum(1 for text in texts if "链路断开" in text)
    if down_frames >= 2 and down_logs >= down_frames:
        raise AssertionError(("link-down logged once per frame", down_frames, down_logs))
    for text in texts:
        if "模拟源 sim.three-cart" in text or "凭据" in text or "password" in text.lower():
            raise AssertionError(("log text not brief", text))

    with closing(sqlite3.connect(path)) as db:
        db.execute("drop table sim_logs")
        db.execute("pragma user_version=2")
        db.commit()
    migrated = subprocess.run([collector, tmp, "1", "20"], capture_output=True, text=True, timeout=8)
    if migrated.returncode != 0:
        raise AssertionError(migrated.stderr)
    frames2, logs2, _, version2 = counts(path)
    if version2 != 3 or frames2 != frames + 1 or logs2 < 1:
        raise AssertionError(("v2", frames2, logs2, version2))

    before = frames2
    with closing(sqlite3.connect(path)) as db:
        db.execute("drop table runtime_state")
        db.execute("drop table sim_logs")
        db.execute("pragma user_version=1")
        db.commit()
    legacy = subprocess.run([collector, tmp, "1", "20"], capture_output=True, text=True, timeout=8)
    if legacy.returncode != 0:
        raise AssertionError(legacy.stderr)
    frames3, logs3, _, version3 = counts(path)
    if version3 != 3 or frames3 != before + 1 or logs3 < 1:
        raise AssertionError(("v1", frames3, logs3, version3))

    capped = subprocess.run([collector, tmp, "360", "20"], capture_output=True, text=True, timeout=20)
    if capped.returncode != 0:
        raise AssertionError(capped.stderr)
    frames4, logs4, collector_rows4, version4 = counts(path)
    if version4 != 3 or frames4 < 300 or logs4 > 300 or logs4 < 1 or collector_rows4 >= frames4:
        raise AssertionError(("cap", frames4, logs4, collector_rows4, version4))

print("PASS: log schema migration, capacity, single writer, collector rows are not one per frame")
