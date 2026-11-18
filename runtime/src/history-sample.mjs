import {utcUs} from './log-coverage.mjs';
const utc = value => value == null ? null : utcUs(Number(value));
export function stamp(row){return {sequence:String(row.sequence),boot_id:row.boot_id,monotonic_ns:String(row.monotonic_ns),observed_at:utc(row.observed_at_us)};}
export function sample(row){return {tag_id:row.tag_id,device_id:row.device_id,scan_group_id:row.group_id,value:JSON.parse(row.value_json),
  quality:{level:row.quality,reason:row.quality_reason,native_code:row.native_code??undefined},source_timestamp:utc(row.source_at_us),
  server_timestamp:utc(row.server_at_us),event_timestamp:utc(row.event_at_us),timestamp_basis:row.timestamp_basis,clock_epoch:Number(row.clock_epoch),stamp:stamp(row)};}
