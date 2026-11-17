-- T2 基础设施草案；不含物流业务表。仅用于新数据库，迁移由后续 runtime 管理。
-- 每个 runtime 连接都必须重新设置 foreign_keys / synchronous / busy_timeout。
PRAGMA journal_mode = WAL;
PRAGMA synchronous = FULL;
PRAGMA foreign_keys = ON;
PRAGMA busy_timeout = 5000;

BEGIN IMMEDIATE;

CREATE TABLE schema_migrations (
  version INTEGER PRIMARY KEY,
  description TEXT NOT NULL
) STRICT;
INSERT INTO schema_migrations VALUES (1, 'T2 infrastructure draft');

CREATE TABLE runtime_identity (
  singleton INTEGER PRIMARY KEY CHECK (singleton = 1),
  runtime_id TEXT NOT NULL UNIQUE,
  created_at_us INTEGER NOT NULL
) STRICT;
CREATE TABLE sequence_counters (
  name TEXT PRIMARY KEY CHECK (name IN ('record', 'event')),
  high_water INTEGER NOT NULL CHECK (high_water >= 0)
) STRICT;
INSERT INTO sequence_counters VALUES ('record', 0), ('event', 0);

CREATE TABLE runtime_boots (
  boot_id TEXT PRIMARY KEY,
  started_at_us INTEGER NOT NULL,
  ended_at_us INTEGER,
  clean_shutdown INTEGER NOT NULL DEFAULT 0 CHECK (clean_shutdown IN (0, 1))
) STRICT;

CREATE TABLE config_revisions (
  revision TEXT PRIMARY KEY,
  git_commit TEXT NOT NULL,
  manifest_json TEXT NOT NULL CHECK (json_valid(manifest_json)),
  created_at_us INTEGER NOT NULL
) STRICT;

CREATE TABLE devices (
  device_id TEXT PRIMARY KEY,
  name_zh TEXT NOT NULL,
  protocol TEXT NOT NULL CHECK (protocol IN ('mock', 'opcua')),
  connection_json TEXT NOT NULL CHECK (json_valid(connection_json)),
  enabled INTEGER NOT NULL CHECK (enabled IN (0, 1)),
  config_revision TEXT NOT NULL REFERENCES config_revisions(revision)
) STRICT;

CREATE TABLE scan_groups (
  group_id TEXT PRIMARY KEY,
  device_id TEXT NOT NULL REFERENCES devices(device_id),
  requested_interval_ms INTEGER NOT NULL CHECK (requested_interval_ms >= 10),
  publishing_interval_ms INTEGER NOT NULL CHECK (publishing_interval_ms >= 10),
  queue_size INTEGER NOT NULL CHECK (queue_size BETWEEN 1 AND 10000),
  discard_oldest INTEGER NOT NULL CHECK (discard_oldest IN (0, 1)),
  config_revision TEXT NOT NULL REFERENCES config_revisions(revision),
  UNIQUE (group_id, device_id)
) STRICT;

CREATE TABLE tags (
  tag_id TEXT PRIMARY KEY,
  device_id TEXT NOT NULL,
  group_id TEXT NOT NULL,
  name_zh TEXT NOT NULL,
  address_json TEXT NOT NULL CHECK (json_valid(address_json)),
  data_type TEXT NOT NULL CHECK (data_type IN ('boolean', 'number', 'int64', 'uint64', 'string')),
  engineering_unit TEXT,
  engineering_low REAL,
  engineering_high REAL,
  deadband_mode TEXT NOT NULL CHECK (deadband_mode IN ('none', 'absolute', 'percent')),
  deadband_value REAL NOT NULL DEFAULT 0 CHECK (deadband_value >= 0),
  stale_after_ms INTEGER NOT NULL CHECK (stale_after_ms > 0),
  safety_class TEXT NOT NULL CHECK (safety_class IN ('ordinary', 'interlock', 'safety')),
  history_enabled INTEGER NOT NULL CHECK (history_enabled IN (0, 1)),
  enabled INTEGER NOT NULL CHECK (enabled IN (0, 1)),
  config_revision TEXT NOT NULL REFERENCES config_revisions(revision),
  FOREIGN KEY (group_id, device_id) REFERENCES scan_groups(group_id, device_id),
  CHECK (engineering_low IS NULL OR engineering_high IS NULL OR engineering_low < engineering_high),
  CHECK (data_type = 'number' OR deadband_mode = 'none'),
  CHECK (deadband_mode != 'none' OR deadband_value = 0),
  CHECK (deadband_mode != 'percent' OR
    (engineering_low IS NOT NULL AND engineering_high IS NOT NULL
      AND engineering_high > engineering_low AND deadband_value <= 100))
) STRICT;

CREATE TABLE history_raw (
  sequence INTEGER PRIMARY KEY CHECK (sequence > 0),
  tag_id TEXT NOT NULL REFERENCES tags(tag_id),
  config_revision TEXT NOT NULL REFERENCES config_revisions(revision),
  boot_id TEXT NOT NULL REFERENCES runtime_boots(boot_id),
  scan_cycle TEXT NOT NULL,
  session_id TEXT,
  monotonic_ns INTEGER NOT NULL CHECK (monotonic_ns >= 0),
  observed_at_us INTEGER NOT NULL,
  source_at_us INTEGER,
  server_at_us INTEGER,
  event_at_us INTEGER NOT NULL,
  timestamp_basis TEXT NOT NULL CHECK (timestamp_basis IN ('source', 'server', 'observed')),
  clock_epoch INTEGER NOT NULL CHECK (clock_epoch >= 0),
  value_json TEXT NOT NULL CHECK (json_valid(value_json)),
  quality TEXT NOT NULL CHECK (quality IN ('good', 'uncertain', 'bad')),
  quality_reason TEXT NOT NULL,
  native_code TEXT,
  CHECK ((timestamp_basis = 'source' AND source_at_us IS NOT NULL AND event_at_us = source_at_us)
    OR (timestamp_basis = 'server' AND server_at_us IS NOT NULL AND event_at_us = server_at_us)
    OR (timestamp_basis = 'observed' AND event_at_us = observed_at_us))
) STRICT;
CREATE INDEX history_tag_time ON history_raw(tag_id, event_at_us, sequence);
CREATE INDEX history_observed_time ON history_raw(observed_at_us, sequence);

-- 每个 epoch 独立聚合，回跳重叠的桶不强行拼成连续墙钟。
CREATE TABLE history_aggregates (
  tag_id TEXT NOT NULL REFERENCES tags(tag_id),
  config_revision TEXT NOT NULL REFERENCES config_revisions(revision),
  clock_epoch INTEGER NOT NULL CHECK (clock_epoch >= 0),
  resolution_ms INTEGER NOT NULL CHECK (resolution_ms IN (1000, 60000, 3600000)),
  bucket_start_us INTEGER NOT NULL,
  sample_count INTEGER NOT NULL CHECK (sample_count > 0),
  good_count INTEGER NOT NULL CHECK (good_count >= 0),
  uncertain_count INTEGER NOT NULL CHECK (uncertain_count >= 0),
  bad_count INTEGER NOT NULL CHECK (bad_count >= 0),
  numeric_good_count INTEGER NOT NULL CHECK (numeric_good_count BETWEEN 0 AND good_count),
  numeric_sum REAL,
  numeric_min REAL,
  numeric_max REAL,
  first_sequence INTEGER NOT NULL CHECK (first_sequence > 0),
  last_sequence INTEGER NOT NULL CHECK (last_sequence >= first_sequence),
  first_sample_json TEXT NOT NULL CHECK (json_valid(first_sample_json)),
  last_sample_json TEXT NOT NULL CHECK (json_valid(last_sample_json)),
  source_high_water INTEGER NOT NULL CHECK (source_high_water >= last_sequence),
  algorithm_revision TEXT NOT NULL,
  finalized INTEGER NOT NULL CHECK (finalized IN (0, 1)),
  PRIMARY KEY (tag_id, config_revision, clock_epoch, resolution_ms, bucket_start_us),
  CHECK (sample_count = good_count + uncertain_count + bad_count),
  CHECK ((numeric_good_count = 0 AND numeric_sum IS NULL AND numeric_min IS NULL AND numeric_max IS NULL)
    OR (numeric_good_count > 0 AND numeric_sum IS NOT NULL AND numeric_min IS NOT NULL
      AND numeric_max IS NOT NULL AND numeric_min <= numeric_max))
) STRICT;

CREATE TABLE alarm_rules (
  rule_id TEXT PRIMARY KEY,
  tag_id TEXT NOT NULL REFERENCES tags(tag_id),
  config_revision TEXT NOT NULL REFERENCES config_revisions(revision),
  kind TEXT NOT NULL CHECK (kind IN ('high', 'low', 'boolean', 'quality_bad')),
  parameters_json TEXT NOT NULL CHECK (json_valid(parameters_json)),
  severity INTEGER NOT NULL CHECK (severity BETWEEN 1 AND 4),
  on_delay_ms INTEGER NOT NULL CHECK (on_delay_ms >= 0),
  off_delay_ms INTEGER NOT NULL CHECK (off_delay_ms >= 0),
  enabled INTEGER NOT NULL CHECK (enabled IN (0, 1))
) STRICT;

CREATE TABLE alarm_occurrences (
  occurrence_id TEXT PRIMARY KEY,
  rule_id TEXT NOT NULL REFERENCES alarm_rules(rule_id),
  config_revision TEXT NOT NULL REFERENCES config_revisions(revision),
  revision INTEGER NOT NULL CHECK (revision > 0),
  condition TEXT NOT NULL CHECK (condition IN ('active', 'cleared')),
  acknowledgement TEXT NOT NULL CHECK (acknowledgement IN ('unacknowledged', 'acknowledged')),
  opened_sequence INTEGER NOT NULL CHECK (opened_sequence > 0),
  opened_at_us INTEGER NOT NULL,
  cleared_sequence INTEGER,
  cleared_at_us INTEGER,
  acknowledged_sequence INTEGER,
  acknowledged_at_us INTEGER,
  acknowledged_by TEXT,
  CHECK ((condition = 'active' AND cleared_sequence IS NULL AND cleared_at_us IS NULL)
    OR (condition = 'cleared' AND cleared_sequence IS NOT NULL AND cleared_at_us IS NOT NULL)),
  CHECK ((acknowledgement = 'unacknowledged' AND acknowledged_sequence IS NULL
    AND acknowledged_at_us IS NULL AND acknowledged_by IS NULL)
    OR (acknowledgement = 'acknowledged' AND acknowledged_sequence IS NOT NULL
      AND acknowledged_at_us IS NOT NULL AND acknowledged_by IS NOT NULL))
) STRICT;
CREATE UNIQUE INDEX one_active_occurrence ON alarm_occurrences(rule_id) WHERE condition = 'active';
CREATE INDEX alarms_opened_time ON alarm_occurrences(opened_at_us, opened_sequence);

-- 抑制属于规则，与报警实例是否存在无关；实例清除后也不丢失搁置期限。
CREATE TABLE alarm_rule_state (
  rule_id TEXT PRIMARY KEY REFERENCES alarm_rules(rule_id),
  evaluation TEXT NOT NULL CHECK (evaluation IN ('normal', 'pending_on', 'active', 'pending_off', 'unknown')),
  suppression TEXT NOT NULL CHECK (suppression IN ('none', 'shelved', 'suppressed_by_design')),
  suppression_until_us INTEGER,
  suppression_reason TEXT,
  suppression_by TEXT,
  shelve_boot_id TEXT REFERENCES runtime_boots(boot_id),
  shelve_deadline_ns INTEGER CHECK (shelve_deadline_ns >= 0),
  timer_boot_id TEXT REFERENCES runtime_boots(boot_id),
  timer_started_ns INTEGER CHECK (timer_started_ns >= 0),
  last_sample_sequence INTEGER,
  CHECK (suppression != 'shelved' OR
    (suppression_until_us IS NOT NULL AND suppression_reason IS NOT NULL AND suppression_by IS NOT NULL
      AND shelve_boot_id IS NOT NULL AND shelve_deadline_ns IS NOT NULL))
) STRICT;

CREATE TABLE alarm_transitions (
  sequence INTEGER PRIMARY KEY CHECK (sequence > 0),
  rule_id TEXT NOT NULL REFERENCES alarm_rules(rule_id),
  occurrence_id TEXT REFERENCES alarm_occurrences(occurrence_id),
  boot_id TEXT NOT NULL REFERENCES runtime_boots(boot_id),
  monotonic_ns INTEGER NOT NULL CHECK (monotonic_ns >= 0),
  observed_at_us INTEGER NOT NULL,
  transition TEXT NOT NULL CHECK (transition IN
    ('activate', 'clear', 'acknowledge', 'shelve', 'unshelve', 'suppress', 'unsuppress', 'quality_unknown')),
  actor_id TEXT,
  details_json TEXT NOT NULL CHECK (json_valid(details_json))
) STRICT;

CREATE TABLE retention_policies (
  revision TEXT PRIMARY KEY,
  policy_json TEXT NOT NULL CHECK (json_valid(policy_json)),
  effective_sequence INTEGER NOT NULL CHECK (effective_sequence > 0),
  created_at_us INTEGER NOT NULL
) STRICT;

CREATE TABLE log_segments (
  segment_id TEXT PRIMARY KEY,
  relative_path TEXT NOT NULL UNIQUE,
  state TEXT NOT NULL CHECK (state IN ('active', 'sealed', 'deleting', 'deleted', 'corrupt')),
  policy_revision TEXT NOT NULL REFERENCES retention_policies(revision),
  first_sequence INTEGER,
  last_sequence INTEGER,
  durable_bytes INTEGER NOT NULL DEFAULT 0 CHECK (durable_bytes >= 0),
  sha256 TEXT,
  deleted_at_us INTEGER
) STRICT;

-- 样本/报警与日志 intent 同一 SQLite 事务提交；JSONL 导出 fsync 后再登记索引。
CREATE TABLE log_outbox (
  sequence INTEGER PRIMARY KEY CHECK (sequence > 0),
  observed_at_us INTEGER NOT NULL,
  payload_json TEXT NOT NULL CHECK (json_valid(payload_json))
) STRICT;
CREATE TABLE log_index (
  sequence INTEGER PRIMARY KEY CHECK (sequence > 0),
  segment_id TEXT NOT NULL REFERENCES log_segments(segment_id),
  byte_offset INTEGER NOT NULL CHECK (byte_offset >= 0),
  byte_length INTEGER NOT NULL CHECK (byte_length > 0),
  observed_at_us INTEGER NOT NULL,
  boot_id TEXT NOT NULL REFERENCES runtime_boots(boot_id),
  monotonic_ns INTEGER NOT NULL CHECK (monotonic_ns >= 0),
  level TEXT NOT NULL CHECK (level IN ('debug', 'info', 'warn', 'error')),
  event_code TEXT NOT NULL,
  device TEXT,
  tag TEXT,
  scan_cycle TEXT,
  session_id TEXT,
  UNIQUE (segment_id, byte_offset)
) STRICT;
CREATE INDEX logs_window ON log_index(observed_at_us, sequence);
CREATE INDEX logs_device_window ON log_index(device, observed_at_us, sequence);

-- 由运行/落盘覆盖检查点产生，不从匹配记录的 min/max 推算。
CREATE TABLE recording_coverage (
  coverage_id INTEGER PRIMARY KEY,
  source TEXT NOT NULL CHECK (source IN ('history', 'alarms', 'logs')),
  scope_id TEXT NOT NULL, -- tag_id / rule_id / runtime_id
  resolution_ms INTEGER NOT NULL CHECK (resolution_ms IN (0, 1000, 60000, 3600000)),
  clock_epoch INTEGER NOT NULL CHECK (clock_epoch >= 0),
  from_us INTEGER NOT NULL,
  to_us INTEGER NOT NULL CHECK (to_us > from_us),
  status TEXT NOT NULL CHECK (status IN ('recorded', 'rotated', 'not_recorded', 'clock_discontinuity', 'unknown')),
  policy_revision TEXT NOT NULL REFERENCES retention_policies(revision),
  segment_id TEXT REFERENCES log_segments(segment_id),
  durable_through_sequence INTEGER NOT NULL CHECK (durable_through_sequence >= 0)
) STRICT;
CREATE INDEX coverage_window ON recording_coverage(source, scope_id, resolution_ms, from_us, to_us);

-- 同一 JSONL 导出机制；审计的运行时追加与系统管理员权限隔离是两件事。
CREATE TABLE audit_events (
  sequence INTEGER PRIMARY KEY CHECK (sequence > 0),
  operation_id TEXT NOT NULL,
  actor_id TEXT NOT NULL,
  action TEXT NOT NULL,
  phase TEXT NOT NULL CHECK (phase IN ('intent', 'applied', 'failed', 'rejected', 'recovery')),
  observed_at_us INTEGER NOT NULL,
  boot_id TEXT NOT NULL REFERENCES runtime_boots(boot_id),
  monotonic_ns INTEGER NOT NULL CHECK (monotonic_ns >= 0),
  rollback_commit TEXT,
  result_commit TEXT,
  details_json TEXT NOT NULL CHECK (json_valid(details_json))
) STRICT;
CREATE INDEX audit_operation ON audit_events(operation_id, sequence);
CREATE TRIGGER audit_no_update BEFORE UPDATE ON audit_events
BEGIN SELECT RAISE(ABORT, 'audit is append-only'); END;
CREATE TRIGGER audit_no_delete BEFORE DELETE ON audit_events
BEGIN SELECT RAISE(ABORT, 'audit is append-only'); END;

COMMIT;
