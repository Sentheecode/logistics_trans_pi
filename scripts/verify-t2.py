"""只在临时目录执行 T2 DDL/约束验证和合成容量探测；不访问现场数据。"""
import json
import pathlib
import sqlite3
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DDL = (ROOT / "db/001-infrastructure.sql").read_text()


def expect_integrity_error(db, sql, params=()):
    try:
        db.execute(sql, params)
    except sqlite3.IntegrityError:
        return
    raise AssertionError(f"expected constraint rejection: {sql}")


with tempfile.TemporaryDirectory(prefix="ltp-t2-") as temporary:
    db = sqlite3.connect(pathlib.Path(temporary) / "probe.sqlite")
    db.executescript(DDL)
    assert db.execute("PRAGMA journal_mode").fetchone()[0] == "wal"
    assert db.execute("PRAGMA synchronous").fetchone()[0] == 2
    assert db.execute("PRAGMA foreign_keys").fetchone()[0] == 1
    db.execute("INSERT INTO runtime_identity VALUES (1, 'probe-runtime', 0)")
    db.execute("INSERT INTO runtime_boots VALUES ('boot-1', 0, NULL, 0)")
    db.execute("INSERT INTO config_revisions VALUES ('rev-1', 'probe-commit', '{}', 0)")
    db.execute("INSERT INTO devices VALUES ('mock-1', '模拟设备', 'mock', '{}', 1, 'rev-1')")
    db.execute("INSERT INTO scan_groups VALUES ('g-100ms', 'mock-1', 100, 100, 10, 1, 'rev-1')")
    tag_sql = """INSERT INTO tags VALUES
      (?, 'mock-1', 'g-100ms', '模拟点位', '{}', ?, NULL, ?, ?, ?, ?, 1000,
       'ordinary', 1, 1, 'rev-1')"""
    db.execute(tag_sql, ('tag-1', 'number', 0, 100, 'absolute', 0.1))
    expect_integrity_error(db, tag_sql, ('bad-bool', 'boolean', None, None, 'absolute', 1))
    expect_integrity_error(db, tag_sql, ('bad-percent', 'number', None, None, 'percent', 1))
    expect_integrity_error(db, "UPDATE tags SET group_id='missing' WHERE tag_id='tag-1'")

    db.execute("INSERT INTO alarm_rules VALUES ('r-1', 'tag-1', 'rev-1', 'high', '{}', 2, 0, 0, 1)")
    occurrence_sql = """INSERT INTO alarm_occurrences VALUES
      (?, 'r-1', 'rev-1', 1, 'active', 'unacknowledged', 1, 100, NULL, NULL, NULL, NULL, NULL)"""
    db.execute(occurrence_sql, ('a-1',))
    expect_integrity_error(db, occurrence_sql, ('a-duplicate',))
    expect_integrity_error(db, "UPDATE alarm_occurrences SET acknowledgement='acknowledged' WHERE occurrence_id='a-1'")
    db.execute("""UPDATE alarm_occurrences SET acknowledgement='acknowledged',
        acknowledged_sequence=2, acknowledged_at_us=101, acknowledged_by='human-1' WHERE occurrence_id='a-1'""")
    assert db.execute("SELECT condition FROM alarm_occurrences").fetchone()[0] == 'active'
    db.execute("UPDATE alarm_occurrences SET condition='cleared', cleared_sequence=3, cleared_at_us=102 WHERE occurrence_id='a-1'")
    db.execute(occurrence_sql, ('a-2',))

    db.execute("INSERT INTO audit_events VALUES (4, 'op-1', 'human-1', 'config.replace', 'intent', 0, 'boot-1', 1, NULL, NULL, '{}')")
    expect_integrity_error(db, "DELETE FROM audit_events")
    expect_integrity_error(db, "UPDATE audit_events SET actor_id='other'")
    db.execute("INSERT INTO retention_policies VALUES ('p-1', '{}', 1, 0)")
    db.execute("""INSERT INTO recording_coverage VALUES
      (1, 'logs', 'probe-runtime', 0, 0, 0, 1000, 'recorded', 'p-1', NULL, 4)""")
    assert db.execute("SELECT count(*) FROM log_index").fetchone()[0] == 0
    assert db.execute("SELECT status FROM recording_coverage").fetchone()[0] == 'recorded'
    # 有完整覆盖且零日志，与没有覆盖不是同一事实；这里只验证两者可分别表达。
    db.commit()

    sample_sql = """INSERT INTO history_raw VALUES
      (?, 'tag-1', 'rev-1', 'boot-1', 'cycle-1', 'session-1', ?, ?, ?, NULL, ?,
       'source', 0, ?, 'good', 'source_good', NULL)"""
    db.execute(sample_sql, (10, 1, 200, 200, 200, '{"type":"number","value":1.0}'))
    db.execute(sample_sql, (11, 2, 100, 100, 100, '{"type":"number","value":2.0}'))
    assert db.execute("SELECT observed_at_us FROM history_raw ORDER BY sequence").fetchall() == [(200,), (100,)]
    # 回跳墙钟不会覆盖/倒置持久序号；不冒充改系统时钟或拔电测试。
    db.commit()
    before = db.execute("SELECT high_water FROM sequence_counters WHERE name='record'").fetchone()[0]
    db.execute("BEGIN IMMEDIATE")
    db.execute("UPDATE sequence_counters SET high_water=100 WHERE name='record'")
    db.execute("INSERT INTO log_outbox VALUES (100, 300, '{}')")
    db.rollback()
    assert db.execute("SELECT high_water FROM sequence_counters WHERE name='record'").fetchone()[0] == before
    assert db.execute("SELECT count(*) FROM log_outbox").fetchone()[0] == 0

    def allocated_bytes():
        # 含索引的已分配 DB 页，不把 WAL 大小误算成稳定态数据体积。
        return db.execute('PRAGMA page_count').fetchone()[0] * db.execute('PRAGMA page_size').fetchone()[0]

    count = 20000
    start = allocated_bytes()
    db.executemany(sample_sql, (
        (i + 1000, i * 100_000_000, 1_700_000_000_000_000 + i * 100_000,
         1_700_000_000_000_000 + i * 100_000, 1_700_000_000_000_000 + i * 100_000,
         json.dumps({'type': 'number', 'value': i * 0.125}, separators=(',', ':')))
        for i in range(count)
    ))
    db.commit()
    raw_bytes = (allocated_bytes() - start) / count
    start = allocated_bytes()
    first_json = json.dumps({'tag_id': 'tag-1', 'device_id': 'mock-1', 'scan_group_id': 'g-100ms',
        'value': {'type': 'number', 'value': 1.0}, 'quality': {'level': 'good', 'reason': 'source_good'},
        'source_timestamp': '2026-11-17T00:00:00Z', 'server_timestamp': None,
        'event_timestamp': '2026-11-17T00:00:00Z', 'timestamp_basis': 'source', 'clock_epoch': 0,
        'stamp': {'sequence': '1000', 'observed_at': '2026-11-17T00:00:00Z',
                  'boot_id': 'boot-1', 'monotonic_ns': '100000000'}}, separators=(',', ':'))
    db.executemany("""INSERT INTO history_aggregates VALUES
      ('tag-1', 'rev-1', 0, 1000, ?, 10, 10, 0, 0, 10, 10.0, 1.0, 1.0,
       1000, 1009, ?, ?, 21000, 'sample-weighted-v1', 1)""",
      ((i * 1_000_000, first_json, first_json) for i in range(count)))
    db.commit()
    aggregate_bytes = (allocated_bytes() - start) / count
    assert db.execute('PRAGMA foreign_key_check').fetchall() == []
    assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
    db.execute('PRAGMA wal_checkpoint(TRUNCATE)')
    report = {
        'sqlite_version': sqlite3.sqlite_version,
        'checks': ['WAL/FULL/foreign_keys', 'deadband type and engineering range',
                   'one active alarm per rule', 'ack does not clear condition',
                   'append-only audit SQL guards', 'empty logs versus recording coverage',
                   'backward wall time ordered by sequence', 'transaction rollback of counter and outbox',
                   'foreign_key_check', 'integrity_check'],
        'synthetic_rows_per_table': count,
        'synthetic_raw_bytes_per_row_including_indexes': round(raw_bytes, 2),
        'synthetic_aggregate_bytes_per_row_including_indexes': round(aggregate_bytes, 2),
        'limitations': ['synthetic short IDs, one tag, numeric values',
                       'not a throughput or power-loss test', 'not Windows validation',
                       'does not validate application-only rules or JSON Schema']
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
