"""容量预算假设，可修改输入复算；不是现场容量或吞吐测量。"""
import json

RAW_BYTES = 384
AGGREGATE_BYTES = 1536
TIERS = [('raw_6h', 0.1, 6 * 3600, RAW_BYTES),
         ('1s_24h', 1, 24 * 3600, AGGREGATE_BYTES),
         ('1m_30d', 60, 30 * 86400, AGGREGATE_BYTES),
         ('1h_3y', 3600, 1095 * 86400, AGGREGATE_BYTES)]
scenarios = []
for tags in (100, 1000, 5000):
    tiers = {name: {'rows': int(tags * duration / interval),
                    'decimal_GB': round(tags * duration / interval * size / 1e9, 3)}
             for name, interval, duration, size in TIERS}
    history = sum(tags * duration / interval * size for _, interval, duration, size in TIERS)
    logs = 10 * 90 * 86400 * (700 + 192)  # JSONL + SQLite 索引预算
    audit = 1000 * 1095 * 2048  # 每日 1000 条、三年，示例含相关索引预算
    scenarios.append({'tags': tags, 'tiers': tiers,
        'history_decimal_GB': round(history / 1e9, 3),
        'normal_logs_90d_decimal_GB': round(logs / 1e9, 3),
        'audit_3y_decimal_GB': round(audit / 1e9, 3),
        'total_with_2x_history_headroom_decimal_GB': round((2 * history + logs + audit) / 1e9, 3),
        'raw_only_3y_decimal_TB': round(tags * 10 * 1095 * 86400 * RAW_BYTES / 1e12, 3)})
print(json.dumps({'assumptions': {'raw_row_bytes_including_indexes': RAW_BYTES,
    'aggregate_row_bytes_including_indexes': AGGREGATE_BYTES,
    'sample_rate_hz': 10, 'numeric_tags': True, 'normal_log_rate_per_second': 10,
    'history_headroom_factor': 2, 'year_days': 365,
    'excludes': ['OS', 'Node/Electron', 'backups', 'business tables', 'alarm storms beyond audit budget'],
    'assumes': ['every sample changes', 'all tiers overlap while retained',
                'no sparse buckets and no deadband compression savings']},
    'scenarios': scenarios}, ensure_ascii=False, indent=2))
