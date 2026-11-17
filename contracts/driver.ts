/** 通用采集边界：不导出 node-opcua 或任何协议库对象。 */
export interface ConnectionState {
  link: 'down' | 'connecting' | 'up';
  session: 'absent' | 'creating' | 'active';
  subscription: 'absent' | 'rebuilding' | 'healthy' | 'stale';
}
export type Deadband = {mode:'none';value:0} | {mode:'absolute'|'percent';value:number};
export interface MonitoredTag {
  id:string;
  type:'number'|'boolean'|'string'|'int64'|'uint64';
  initial:number|boolean|string; // mock 专用初始值；真实协议映射在独立配置中
  deadband:Deadband;
  engineering_low?:number;
  engineering_high?:number;
}
export interface SubscriptionManifest {
  device_id:string;
  groups:Array<{id:string;interval_ms:number;tags:MonitoredTag[]}>;
}
export interface DriverSample {
  tag_id:string;
  device_id:string;
  group_id:string;
  value:{type:MonitoredTag['type'];value:number|boolean|string};
  quality:{level:'good'|'uncertain'|'bad';reason:string;native_code?:string};
  source_at_ms:number|null;
  server_at_ms:number|null;
  observed_at_ms:number;
  event_at_ms:number;
  timestamp_basis:'source'|'server'|'observed';
  monotonic_ns:string;
  scan_cycle:string;
  session_id:string;
}
export interface AcquisitionDriver {
  connect():void|Promise<void>;
  disconnect():void|Promise<void>;
  rebuildSubscriptions():void|Promise<void>;
  snapshot():{connection:ConnectionState;manifest:SubscriptionManifest;generation:number};
  on(event:'sample',listener:(sample:DriverSample)=>void):this;
  on(event:'state',listener:(state:ConnectionState)=>void):this;
}
// MockDriver 的 setValue/advance/loseSubscriptions/restoreSession 是模拟接口，
// 不属于 agent 或公共 HTTP API；真实 driver 的异步取消/关闭在接入时扩展。
