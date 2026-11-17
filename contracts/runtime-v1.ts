/** T1 契约草案。只定义边界；不代表端点已实现。HTTP 前缀 /api/v1。 */
export type UTC = string; // RFC3339 UTC，运行时校验
export type Sequence = string; // 十进制无符号整数字符串，禁止转 JS number
export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };
export interface Window { from: UTC; to: UTC } // [from,to)
export interface Stamp {
  sequence: Sequence; // runtime 持久化分配；跨重启不倒退，不承诺连续
  observed_at: UTC; // runtime 接收时墙钟；可倒退
  boot_id: string;
  monotonic_ns: Sequence; // 仅相同 boot_id 内可比较
}
export interface Quality {
  level: 'good' | 'uncertain' | 'bad';
  reason: string; // stale / disconnected / overflow / source_bad 等机器可读原因
  native_code?: string;
}
export type TagValue =
  | { type: 'boolean'; value: boolean }
  | { type: 'number'; value: number } // 有限数值
  | { type: 'int64' | 'uint64' | 'string'; value: string }
  | { type: 'unavailable'; value: null };
export interface TagSample {
  tag_id: string;
  device_id: string;
  scan_group_id: string;
  value: TagValue;
  quality: Quality;
  source_timestamp: UTC | null;
  server_timestamp: UTC | null;
  stamp: Stamp;
}
export interface ReadMeta {
  request_id: string;
  runtime_id: string; // runtime 持久身份，重装/重建数据后必须改变
  as_of_sequence: Sequence;
  generated_at: UTC;
}
export interface Page<T> {
  items: T[];
  next_cursor: string | null;
  meta: ReadMeta;
}
export interface Coverage {
  requested: Window;
  time_window_actually_covered: Window[]; // 可不连续；空数组是无证据
  status: 'complete' | 'partial' | 'none' | 'unknown';
  gaps: Array<{
    window: Window;
    reason: 'rotated' | 'not_recorded' | 'clock_discontinuity' | 'unknown';
  }>;
  retention_policy_revision: string;
}
export interface TagQuery { tag_ids: string[] }
export interface TagQueryResult {
  items: TagSample[];
  missing_tag_ids: string[];
  meta: ReadMeta;
}
export interface HistoryQuery {
  tag_ids: string[];
  window: Window;
  resolution: 'raw' | '1s' | '1m' | '1h';
  limit?: number;
  cursor?: string;
}
export interface HistoryBucket {
  tag_id: string;
  window: Window;
  count: number;
  good_count: number;
  bad_count: number;
  uncertain_count: number;
  min: number | null;
  max: number | null;
  mean: number | null;
  first: TagSample | null;
  last: TagSample | null;
}
export interface HistoryResult extends Page<TagSample | HistoryBucket> {
  coverage: Coverage;
  resolution: HistoryQuery['resolution'];
}
export interface Alarm {
  occurrence_id: string;
  revision: string;
  rule_id: string;
  tag_id: string;
  condition: 'active' | 'cleared';
  acknowledgement: 'unacknowledged' | 'acknowledged';
  suppression: 'none' | 'shelved' | 'suppressed_by_design';
  suppression_until: UTC | null;
  severity: number;
  message_zh: string;
  stamp: Stamp;
}
export interface AlarmQuery {
  condition?: Alarm['condition'];
  acknowledgement?: Alarm['acknowledgement'];
  limit?: number;
  cursor?: string;
}
export interface LogContext {
  device: string | null;
  tag: string | null;
  scan_cycle: string | null;
  session_id: string | null;
}
export interface LogRecord {
  stamp: Stamp;
  level: 'debug' | 'info' | 'warn' | 'error';
  event_code: string;
  message_zh: string;
  context: LogContext;
  fields: { [key: string]: Json }; // 不含凭据
}
export interface LogQuery {
  window: Window;
  device?: string;
  tag?: string;
  event_code?: string;
  text?: string; // 字面文本，禁止任意正则/SQL
  limit?: number;
  cursor?: string;
}
export interface LogResult extends Page<LogRecord>, Coverage {
  matched_count: number; // 固定快照与完整筛选的总匹配数，不是本页数
  count_is_exact: true; // 无法在限额内精确计算时返回错误，不伪装为 0
}
export interface CorrelateQuery {
  window: Window;
  tag_ids: string[];
  device_ids: string[];
}
export interface CorrelateResult {
  meta: ReadMeta;
  evidence: Array<{
    kind: 'tag' | 'alarm' | 'log';
    id: string;
    stamp: Stamp;
    summary_zh: string;
  }>;
  coverage_by_source: { history: Coverage; alarms: Coverage; logs: Coverage };
  truncated: boolean;
  evidence_sufficiency: 'sufficient_for_window' | 'insufficient';
  // 返回证据而非推定根因；sufficient_for_window 也不是因果证明。
}
export interface RuntimeHealth {
  meta: ReadMeta;
  mode: 'running' | 'stopped' | 'test_station';
  watchdog: 'healthy' | 'degraded';
  persistence: 'healthy' | 'degraded' | 'unavailable';
  drivers: Array<{
    device_id: string;
    link: 'down' | 'connecting' | 'up';
    session: 'absent' | 'creating' | 'active';
    subscription: 'absent' | 'rebuilding' | 'healthy' | 'stale';
    last_sample_at: UTC | null;
  }>;
}

// agent 诊断态仅注册以下工具，参数由服务端重新校验。
export interface DiagnosticTools {
  query_tags(input: TagQuery): Promise<TagQueryResult>;
  query_history(input: HistoryQuery): Promise<HistoryResult>;
  list_alarms(input: AlarmQuery): Promise<Page<Alarm>>;
  search_logs(input: LogQuery): Promise<LogResult>;
  correlate_time_window(input: CorrelateQuery): Promise<CorrelateResult>;
}

export interface EventEnvelope<K extends string, P> {
  protocol: 'ltp.runtime.v1';
  runtime_id: string;
  event_sequence: Sequence; // 单独的持久化事件序列，全局排序
  subscription_id: string;
  kind: K;
  payload: P;
}
export type RuntimeEvent =
  | EventEnvelope<'tag.changed', TagSample[]>
  | EventEnvelope<'alarm.changed', Alarm[]>
  | EventEnvelope<'health.changed', RuntimeHealth>;
export type WsClientMessage = {
  kind: 'subscribe';
  request_id: string;
  topics: Array<'tags' | 'alarms' | 'health'>;
  tag_ids: string[];
  resume?: { runtime_id: string; after_event_sequence: Sequence };
} | { kind: 'unsubscribe'; subscription_id: string };
export type WsServerMessage = RuntimeEvent | {
  kind: 'snapshot';
  subscription_id: string;
  runtime_id: string;
  watermark: Sequence; // 原子快照对应事件序列
  tags: TagSample[];
  alarms: Alarm[];
  health: RuntimeHealth;
} | {
  kind: 'subscribed';
  request_id: string;
  subscription_id: string;
  runtime_id: string;
  resumed: boolean;
  watermark: Sequence;
} | {
  kind: 'resync_required';
  reason: 'cursor_expired' | 'runtime_changed' | 'slow_consumer';
} | {
  kind: 'heartbeat';
  runtime_id: string;
  emitted_at: UTC;
  latest_event_sequence: Sequence;
} | { kind: 'error'; error: ApiError };

// 禁止通用文件路径/脚本/PLC 地址写入；资源由 runtime allowlist 解析。
export type ChangeRequest = {
  action: 'config.replace' | 'screen.replace' | 'threshold.replace';
  resource_id: string;
  expected_revision: string;
  proposed_document: Json;
  reason: string;
} | {
  action: 'alarm.acknowledge' | 'alarm.shelve';
  occurrence_id: string;
  expected_revision: string;
  reason: string;
  until?: UTC; // shelve 必填且有上限
};
export interface ChangeProposal {
  proposal_id: string;
  created_by: string; // 从认证主体取得，拒绝调用方自报身份
  request: ChangeRequest;
  diff: string; // 服务端生成规范差异；拒绝无法展示的变更
  diff_sha256: string;
  base_revision: string;
  expires_at: UTC;
  state: 'pending' | 'applying' | 'applied' | 'rejected' | 'expired' | 'failed';
}
export interface HumanConfirmation {
  expected_diff_sha256: string;
  expected_base_revision: string;
  decision: 'approve' | 'reject';
  // 人类身份来自独立认证会话；没有可交给 agent 使用的确认 token。
}
export interface ChangeReceipt {
  proposal_id: string;
  state: ChangeProposal['state'];
  rollback_commit: string | null;
  result_commit: string | null;
  audit_id: string;
  applied_revision: string | null;
  failure_code: string | null;
}
export interface ApiError {
  request_id: string;
  code: string;
  message_zh: string;
  retryable: boolean;
  details?: { [key: string]: Json };
}

/** 端点清单；GET 查询参数为对应类型，复杂窗口用 from/to 展开。 */
export interface HttpContract {
  'GET /api/v1/health': { response: RuntimeHealth };
  'GET /api/v1/tags': { query: TagQuery; response: TagQueryResult };
  'GET /api/v1/history': { query: HistoryQuery; response: HistoryResult };
  'GET /api/v1/alarms': { query: AlarmQuery; response: Page<Alarm> };
  'GET /api/v1/logs': { query: LogQuery; response: LogResult };
  'GET /api/v1/correlations': { query: CorrelateQuery; response: CorrelateResult };
  'POST /api/v1/change-proposals': { body: ChangeRequest; response: ChangeProposal };
  'GET /api/v1/change-proposals/:id': { response: ChangeProposal };
  'POST /api/v1/change-proposals/:id/confirmation': {
    body: HumanConfirmation; response: ChangeReceipt;
  };
  'GET /api/v1/change-proposals/:id/receipt': { response: ChangeReceipt };
}
