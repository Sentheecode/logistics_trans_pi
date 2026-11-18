BEGIN IMMEDIATE;
CREATE TABLE history_rollup_progress (
  singleton INTEGER PRIMARY KEY CHECK(singleton=1),
  last_sequence INTEGER NOT NULL CHECK(last_sequence >= 0)
) STRICT;
INSERT INTO history_rollup_progress VALUES(1,0);
CREATE INDEX history_bucket_lookup ON history_aggregates(tag_id,resolution_ms,bucket_start_us);
INSERT INTO schema_migrations VALUES(4,'incremental event-time history rollup');
COMMIT;
