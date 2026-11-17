-- T4 第一部分：仅从 version=1 迁移；由 runtime 在排他启动阶段执行一次。
BEGIN IMMEDIATE;
CREATE TABLE clock_tracker_state (
  singleton INTEGER PRIMARY KEY CHECK (singleton=1),
  state_json TEXT NOT NULL CHECK (json_valid(state_json))
) STRICT;
CREATE TABLE clock_events (
  sequence INTEGER PRIMARY KEY CHECK (sequence>0),
  clock_epoch INTEGER NOT NULL CHECK (clock_epoch>=0),
  boot_id TEXT NOT NULL REFERENCES runtime_boots(boot_id),
  monotonic_ns INTEGER NOT NULL CHECK (monotonic_ns>=0),
  observed_at_us INTEGER NOT NULL,
  tag_id TEXT NOT NULL REFERENCES tags(tag_id),
  reasons_json TEXT NOT NULL CHECK (json_valid(reasons_json)),
  details_json TEXT NOT NULL CHECK (json_valid(details_json))
) STRICT;
INSERT INTO clock_tracker_state VALUES (1,json_object(
  'version',1,
  'epoch',COALESCE((SELECT MAX(clock_epoch) FROM (
    SELECT clock_epoch FROM history_raw UNION ALL SELECT clock_epoch FROM history_aggregates)),0),
  'last_observation',json((SELECT json_object('boot_id',boot_id,
    'observed_us',CAST(observed_at_us AS TEXT),'monotonic_ns',CAST(monotonic_ns AS TEXT))
    FROM history_raw ORDER BY sequence DESC LIMIT 1)),
  'streams',json('[]')
));
INSERT INTO schema_migrations VALUES (2,'T4 clock discontinuity state and events');
COMMIT;
