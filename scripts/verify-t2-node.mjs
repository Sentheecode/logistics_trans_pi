// 在 Node 自带 SQLite 中执行同一 DDL；仅操作临时目录。
import { DatabaseSync } from 'node:sqlite';
import { readFileSync, mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
const dir = mkdtempSync(join(tmpdir(), 'ltp-t2-node-'));
let db;
try {
  db = new DatabaseSync(join(dir, 'probe.sqlite'));
  db.exec(readFileSync(new URL('../db/001-infrastructure.sql', import.meta.url), 'utf8'));
  const report = {
    node: process.version,
    sqlite: db.prepare('select sqlite_version() as version').get().version,
    journal: db.prepare('PRAGMA journal_mode').get().journal_mode,
    synchronous: db.prepare('PRAGMA synchronous').get().synchronous,
    foreignKeys: db.prepare('PRAGMA foreign_keys').get().foreign_keys,
    integrity: db.prepare('PRAGMA integrity_check').get().integrity_check,
  };
  if (report.journal !== 'wal' || report.synchronous !== 2 || report.foreignKeys !== 1 || report.integrity !== 'ok') {
    throw new Error('DDL verification failed');
  }
  console.log(JSON.stringify(report, null, 2));
} finally {
  if (db) db.close();
  rmSync(dir, { recursive: true, force: true });
}
