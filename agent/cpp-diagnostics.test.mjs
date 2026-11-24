import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { promisify } from 'node:util';
import register from './cpp-diagnostics.mjs';

const run = promisify(execFile);
const [collector, diagnose] = process.argv.slice(2).map(path => resolve(path));
const root = await mkdtemp(join(tmpdir(), 'ltp cpp bridge '));
const directory = join(root, 'mock data');
const tools = new Map();
register({ registerTool: tool => tools.set(tool.name, tool) });
const tool = tools.get('read_simulation_snapshot');
const station_id = 'stn.140';
const read = async (options = {}, signal) => {
  const result = await tool.execute('test', { station_id, ...options }, signal);
  assert.deepEqual(JSON.parse(result.content[0].text), result.details);
  return result.details;
};

process.env.LTP_CPP_DIAGNOSE = diagnose;
process.env.LTP_CPP_DATA_DIR = directory;
// A legacy address is deliberately configured. The C++ path must not depend on it.
process.env.LTP_DIAGNOSTIC_URL = 'http://127.0.0.1:0';
try {
  assert.deepEqual([...tools.keys()], ['read_simulation_snapshot']);
  await assert.rejects(read(), /证据不足/);
  await run(collector, [directory, '16', '20']);
  const latest = await read({ limit: 1 });
  assert.equal(latest.schema, 'ltp.cpp.sim-snapshot.v1');
  assert.equal(latest.simulation_only, true);
  assert.equal(latest.scope.station_id, station_id);
  assert.equal(latest.scope.stream_id, 'sim.three-cart.v1');
  assert.equal(latest.coverage, 'unknown');
  assert.equal(latest.frames.length, 1);
  assert.equal(latest.frame_has_more, true);
  assert.equal(latest.logs.length, 1);
  assert.equal(latest.log_has_more, true);
  assert.equal(latest.frames[0].seq, '16');
  assert.equal(latest.frames[0].alarm_condition, 'cleared');
  assert.deepEqual(latest.frames[0].carts, Array(3).fill('station_complete'));

  const tail = await read({ limit: 40 });
  assert.equal(tail.frames.length, 16);
  assert.equal(tail.frame_has_more, false);
  assert.ok(tail.logs.length > 1);
  assert.equal(tail.log_has_more, false);
  assert.equal(tail.log_availability, 'available');
  assert.ok(tail.logs.some(entry => /三车阶段/.test(entry.text)));
  assert.equal(tail.frames.filter(frame => frame.alarm_condition === 'active').length, 1);
  const from = tail.frames[12].received_utc_ms;
  const to = tail.frames[2].received_utc_ms;
  const selected = item => item.received_utc_ms >= from && item.received_utc_ms < to;
  const windowRead = async (...args) => JSON.parse((await run(diagnose,
    [directory, 'window-json', ...args.map(String)])).stdout);
  const window = await windowRead(from, to, 40);
  assert.equal(window.query, 'retained_receive_window');
  assert.deepEqual(window.window, { basis: 'received_utc_ms', from_inclusive: from, to_exclusive: to });
  assert.equal(window.coverage, 'unknown');
  assert.equal(window.current_frame_usable, false);
  assert.deepEqual(window.frames, tail.frames.filter(selected));
  assert.deepEqual(window.logs, tail.logs.filter(selected));
  const clippedWindow = await windowRead(from, to, 1);
  assert.deepEqual(clippedWindow.frames, window.frames.slice(0, 1));
  assert.equal(clippedWindow.frame_has_more, true);
  assert.equal(clippedWindow.log_has_more, true);
  const emptyWindow = await windowRead(1, 2);
  assert.deepEqual(emptyWindow.frames, []);
  assert.deepEqual(emptyWindow.logs, []);
  assert.equal(emptyWindow.frame_freshness, 'no_evidence');
  assert.equal(emptyWindow.coverage, 'unknown');
  for (const [start,end] of [[to,from],[from,from],[-1,to],[0,86400001],['1;step',to]]) {
    await assert.rejects(windowRead(start,end));
  }
  const down = tail.frames.find(frame => frame.link === 'down');
  assert.equal(down.quality, 'bad');
  assert.equal(down.source_utc_ms, null);
  assert.deepEqual(down.carts, [null, null, null]);
  await assert.rejects(read({ station_id: 'stn.150' }), /其他工位/);
  for (const extra of [{ window: {} }, { tag_ids: [] }, { file: 'other' }, { command: 'step' }]) {
    await assert.rejects(read(extra), /筛选条件不可用/);
  }
  for (const limit of [0, 41, 1.5, null, '1', '1;step']) {
    await assert.rejects(read({ limit }), /整数/);
  }
  await assert.rejects(read({}, AbortSignal.abort()), /证据不足/);
  await assert.rejects(run(diagnose, [directory, 'snapshot-json', '41']));
  process.env.LTP_CPP_DIAGNOSE = 'diagnose';
  await assert.rejects(read(), /绝对路径/);
  process.env.LTP_CPP_DIAGNOSE = diagnose;

  // Move only the fixture timestamps backward; never change the system clock.
  const db = new DatabaseSync(join(directory, 'simulation.sqlite'));
  const escaped = '模拟 "引号" \\ 路径\n换行\u0000结束';
  try {
    db.exec('UPDATE runtime_state SET heartbeat_ms=heartbeat_ms-5000; UPDATE frames SET received_ms=received_ms-5000');
    db.prepare("INSERT INTO sim_logs(received_ms,level,category,object,text) VALUES(?,'info','collector','fixture',?)")
      .run(Date.now(), escaped);
    const stale = await read({ limit: 1 });
    assert.equal(stale.frame_freshness, 'stale');
    assert.equal(stale.runtime.liveness, 'stale');
    assert.equal(stale.current_frame_usable, false);
    assert.equal(stale.logs[0].text, escaped);
    assert.equal(stale.logs[0].frame_seq, null);
    assert.equal(stale.frames[0].seq, '16');
    assert.equal(db.prepare('SELECT COUNT(*) AS n FROM frames').get().n, 16);
    db.exec('DROP TABLE sim_logs; PRAGMA user_version=2');
  } finally {
    db.close();
  }
  const older = await read({ limit: 1 });
  assert.equal(older.log_availability, 'unsupported_schema');
  assert.equal(older.log_has_more, null);
  assert.deepEqual(older.logs, []);
  assert.equal(older.coverage, 'unknown');
  await run(collector, [directory, '1', '20']);
  const migrated = await read();
  assert.equal(migrated.limit, 5);
  assert.equal(migrated.log_availability, 'available');
  assert.equal(migrated.frames[0].seq, '17');
  assert.equal(migrated.runtime.boot, migrated.frames[0].boot);
  assert.equal(migrated.frame_freshness, 'recent');
  assert.equal(migrated.runtime.liveness, 'recent');
  assert.equal(migrated.current_frame_usable, true);
  assert.equal(migrated.frames[0].carts[0], 'scanning');
  const large = new DatabaseSync(join(directory, 'simulation.sqlite'));
  try {
    large.exec(`WITH RECURSIVE sample(n) AS (
      SELECT 1 UNION ALL SELECT n+1 FROM sample WHERE n < 400000
    ) INSERT INTO frames(received_ms,boot,monotonic_ns,phase,quality,reason,link,alarm,event)
      SELECT 10,'fixture',n,0,'bad','fixture','down','unknown','fixture' FROM sample`);
  } finally {
    large.close();
  }
  await assert.rejects(windowRead(20,25), /interrupted/); // Budget exhaustion is not an empty result.
  console.log(JSON.stringify({
    schema: migrated.schema, station: migrated.scope.station_id, frame: migrated.frames[0].seq,
    stage: migrated.frames[0].carts[0], heartbeat: migrated.runtime.liveness,
    freshness: migrated.frame_freshness, coverage: migrated.coverage,
  }));
} finally {
  await rm(root, { recursive: true, force: true });
}
