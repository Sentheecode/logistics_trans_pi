"""验证工具使用环境已有 jsonschema；未添加 runtime 依赖。"""
import copy
import json
import re
from datetime import datetime
from importlib.metadata import version
from pathlib import Path
from jsonschema import Draft202012Validator, FormatChecker

root = Path(__file__).resolve().parents[1]
schema = json.loads((root / 'contracts/log-record.schema.json').read_text())
Draft202012Validator.check_schema(schema)
checker = FormatChecker()

# 此环境的 jsonschema 未安装可选 RFC3339 解析依赖，默认会跳过 date-time。
# 显式注册本项目 UTC 微秒子集，避免把跳过格式检查误报为通过。
@checker.checks('date-time', raises=ValueError)
def utc_timestamp(value):
    if not isinstance(value, str):
        return True  # 类型错误交给 schema 的 type 检查
    if not re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]{1,6})?Z', value):
        return False
    datetime.fromisoformat(value.replace('Z', '+00:00'))
    return True

validator = Draft202012Validator(schema, format_checker=checker)
sample = {'stamp': {'sequence': '1', 'observed_at': '2026-11-17T00:00:00Z',
                   'boot_id': 'b1', 'monotonic_ns': '1'},
          'level': 'info', 'event_code': 'driver.link.up', 'message_zh': '链路恢复',
          'context': {'device': 'mock-1', 'tag': None, 'scan_cycle': None, 'session_id': 's1'},
          'fields': {}}
validator.validate(sample)
invalid = []
item = copy.deepcopy(sample); del item['context']['scan_cycle']; invalid.append(item)
for bad_sequence in (1, '-1'):
    item = copy.deepcopy(sample); item['stamp']['sequence'] = bad_sequence; invalid.append(item)
for bad_date in ('not-a-dateZ', '2026-02-30T00:00:00Z'):
    item = copy.deepcopy(sample); item['stamp']['observed_at'] = bad_date; invalid.append(item)
for item in invalid:
    assert list(validator.iter_errors(item)), 'invalid log accepted'
print(json.dumps({'validator': 'jsonschema', 'version': version('jsonschema'),
                  'date_time_checker': 'explicit stdlib UTC microsecond subset',
                  'valid_record_accepted': True, 'invalid_records_rejected': len(invalid),
                  'installation': 'preinstalled environment tool; no project dependency added'}, indent=2))
