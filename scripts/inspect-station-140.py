"""只读检查来源基线；冲突保留不改写，不生成 PLC 运行配置。"""
import csv
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / 'data/baselines/station-140.csv'
with path.open(encoding='utf-8', newline='') as f:
    rows = list(csv.DictReader(f))
by_section_address = defaultdict(list)
by_address = defaultdict(list)
for row in rows:
    by_section_address[(row['section'], row['address'])].append(row)
    by_address[row['address']].append(row)

def reference(row):
    return {key:row[key] for key in ('source_row_id','name','address','direction')}

report = {
    'source': str(path.relative_to(root)),
    'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
    'source_status': 'user_confirmed_baseline',
    'runtime_status': 'not_mapped_not_enabled',
    'rows': len(rows),
    'sections': dict(Counter(r['section'] for r in rows)),
    'classification': {
        'reserved_slash': sum(r['name'] == '////////' for r in rows),
        'unnamed': sum(r['name'] == '' for r in rows),
        'named': sum(r['name'] not in ('', '////////') for r in rows),
    },
    'duplicate_addresses_within_section': [
        {'section':section, 'address':address, 'rows':[reference(r) for r in group]}
        for (section,address),group in by_section_address.items() if len(group)>1
    ],
    'addresses_present_in_both_sections': [address for address,group in by_address.items()
        if len({r['section'] for r in group})>1],
    'write_section_source_direction_is_read': [reference(r) for r in rows
        if r['section']=='上位机给PLC' and r['direction']=='上位机读取（PLC→PC）'],
    'non_bool_dbx_notation_needs_opcua_mapping': [reference(r) for r in rows
        if r['data_type'].upper()!='BOOL' and '.DBX' in r['address']],
    'limitations': ['无 OPC UA 连接/类型/AccessLevel 验证',
                    '无静默地址或方向修正', '未据此生成有效业务写权限',
                    '分类 named 只表示有名称，不表示已完成语义/安全审核'],
}
print(json.dumps(report,ensure_ascii=False,indent=2))
