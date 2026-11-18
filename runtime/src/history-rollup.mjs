import {sample} from './history-sample.mjs';

// 调用方持有写事务。按序消费可处理源时间乱序，进度和三层桶一同提交。
export class HistoryRollup {
  constructor(db) {
    this.pending = db.prepare(`SELECT h.*, t.device_id, t.group_id
      FROM history_raw h JOIN tags t USING(tag_id)
      WHERE sequence>(SELECT last_sequence FROM history_rollup_progress WHERE singleton=1)
      ORDER BY sequence LIMIT 16`);
    this.pending.setReadBigInts(true);
    this.advance = db.prepare('UPDATE history_rollup_progress SET last_sequence=? WHERE singleton=1');
    this.merge = db.prepare(`INSERT INTO history_aggregates(
      tag_id,config_revision,clock_epoch,resolution_ms,bucket_start_us,
      sample_count,good_count,uncertain_count,bad_count,numeric_good_count,
      numeric_sum,numeric_min,numeric_max,first_sequence,last_sequence,
      first_sample_json,last_sample_json,source_high_water,algorithm_revision,finalized)
      VALUES(?,?,?,?,?,1,?,?,?,?,?,?,?,?,?,?,?,?,'sample-weighted-v1',0)
      ON CONFLICT(tag_id,config_revision,clock_epoch,resolution_ms,bucket_start_us) DO UPDATE SET
      sample_count=sample_count+1,good_count=good_count+excluded.good_count,
      uncertain_count=uncertain_count+excluded.uncertain_count,bad_count=bad_count+excluded.bad_count,
      numeric_good_count=numeric_good_count+excluded.numeric_good_count,
      numeric_sum=CASE WHEN excluded.numeric_sum IS NULL THEN numeric_sum ELSE coalesce(numeric_sum,0)+excluded.numeric_sum END,
      numeric_min=CASE WHEN numeric_min IS NULL OR excluded.numeric_min<numeric_min THEN excluded.numeric_min ELSE numeric_min END,
      numeric_max=CASE WHEN numeric_max IS NULL OR excluded.numeric_max>numeric_max THEN excluded.numeric_max ELSE numeric_max END,
      last_sequence=excluded.last_sequence,last_sample_json=excluded.last_sample_json,source_high_water=excluded.source_high_water
      WHERE source_high_water<excluded.source_high_water AND finalized=0 AND algorithm_revision='sample-weighted-v1'`);
  }
  consume() {
    const batch=this.pending.all();
    for(const row of batch){
      const reading=sample(row);
      const snapshot=JSON.stringify(reading);
      const good=row.quality==='good';
      const numeric=good && reading.value.type==='number' && Number.isFinite(reading.value.value);
      const value=numeric?reading.value.value:null;
      for(const width of [1000,60000,3600000]){
        const period=BigInt(width)*1000n;
        const floor=row.event_at_us-((row.event_at_us%period+period)%period);
        const result=this.merge.run(row.tag_id,row.config_revision,row.clock_epoch,width,floor,
          Number(good),Number(row.quality==='uncertain'),Number(row.quality==='bad'),Number(numeric),
          value,value,value,row.sequence,row.sequence,snapshot,snapshot,row.sequence);
        if(result.changes!==1)throw new Error('已有聚合桶与增量进度不一致，请核对数据版本');
      }
    }
    if(batch.length)this.advance.run(batch.at(-1).sequence);
  }
}
