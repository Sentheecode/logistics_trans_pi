BEGIN IMMEDIATE;
ALTER TABLE log_index ADD COLUMN record_sha256 TEXT;
ALTER TABLE log_segments ADD COLUMN opened_uptime_ns INTEGER NOT NULL DEFAULT 0;
ALTER TABLE log_segments ADD COLUMN sealed_uptime_ns INTEGER;
CREATE UNIQUE INDEX one_active_log_segment ON log_segments(state) WHERE state='active';
CREATE TABLE log_maintenance_state (
  singleton INTEGER PRIMARY KEY CHECK(singleton=1),
  uptime_ns INTEGER NOT NULL CHECK(uptime_ns>=0),
  boot_id TEXT,
  last_monotonic_ns INTEGER
) STRICT;
INSERT INTO log_maintenance_state VALUES(1,0,NULL,NULL);
CREATE TABLE log_recording_spans (
  span_id TEXT PRIMARY KEY,
  segment_id TEXT NOT NULL REFERENCES log_segments(segment_id),
  boot_id TEXT NOT NULL REFERENCES runtime_boots(boot_id),
  clock_epoch INTEGER NOT NULL CHECK(clock_epoch>=0),
  from_us INTEGER NOT NULL,
  to_us INTEGER NOT NULL CHECK(to_us>from_us),
  last_monotonic_ns INTEGER NOT NULL,
  status TEXT NOT NULL CHECK(status IN ('recorded','rotated','unknown')),
  policy_revision TEXT NOT NULL REFERENCES retention_policies(revision)
) STRICT;
CREATE INDEX log_spans_window ON log_recording_spans(from_us,to_us);
CREATE TABLE log_time_gaps (
  gap_id TEXT PRIMARY KEY,
  from_us INTEGER NOT NULL,
  to_us INTEGER NOT NULL CHECK(to_us>from_us),
  reason TEXT NOT NULL CHECK(reason IN ('clock_discontinuity','not_recorded','unknown'))
) STRICT;
CREATE TABLE log_recovery_actions (
  action_id TEXT PRIMARY KEY,
  action TEXT NOT NULL,
  segment_id TEXT REFERENCES log_segments(segment_id),
  recorded_at_us INTEGER NOT NULL,
  backup_path TEXT,
  details_json TEXT NOT NULL CHECK(json_valid(details_json))
) STRICT;
-- runtime 自身的时钟观测没有 tag，允许显式 NULL，不能冒充某个设备点位。
CREATE TABLE clock_events_v3 (
  sequence INTEGER PRIMARY KEY CHECK(sequence>0),
  clock_epoch INTEGER NOT NULL CHECK(clock_epoch>=0),
  boot_id TEXT NOT NULL REFERENCES runtime_boots(boot_id),
  monotonic_ns INTEGER NOT NULL CHECK(monotonic_ns>=0),
  observed_at_us INTEGER NOT NULL,
  tag_id TEXT REFERENCES tags(tag_id),
  reasons_json TEXT NOT NULL CHECK(json_valid(reasons_json)),
  details_json TEXT NOT NULL CHECK(json_valid(details_json))
) STRICT;
INSERT INTO clock_events_v3 SELECT * FROM clock_events;
DROP TABLE clock_events;
ALTER TABLE clock_events_v3 RENAME TO clock_events;
INSERT INTO schema_migrations VALUES(3,'T4 journal recovery rotation and recording coverage');
COMMIT;
