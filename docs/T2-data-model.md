# T2：采集、报警、历史与日志数据模型

日期：2026-11-17。状态：基础设施 DDL 草案，已在临时 SQLite 验证；尚未接 runtime。不是现场数据库，也不包含物流业务表。

## 范围与已有业务表的接入时机

现在适合提供已定的表结构和现有逻辑。优先需要：数据库种类及版本、DDL（或字段/主键/唯一约束）、表间关联、谁读谁写、状态转换、PLC 点位如何映射到表。示例记录可脱敏，不需要账号、密码或生产数据导出。

本次 `devices/tags/history_*` 等名称只属于独立 runtime 基础设施数据库。后续不能直接覆盖或迁移现场同名业务表；跨库关联优先使用稳定业务 ID 和映射，不提前引入任务、托盘、输送线等业务实体。数据库是否同机、是否共库，等用户说明后再定。

## 存储基础与字段口径

DDL：`db/001-infrastructure.sql`。SQLite STRICT 表，WAL，`synchronous=FULL`，外键开启。只适用于新建库；不使用 `IF NOT EXISTS` 掩盖结构漂移。后续版本由迁移版本和内容校验管理。每个连接重设连接级 PRAGMA；`journal_mode` 返回值必须实测为 wal。部署磁盘须是支持 SQLite 锁与落盘语义的本地文件系统，不先假定网络共享盘可用。

后续 T4 已添加 `db/002-clock-tracking.sql` 与运行时 schema v1→v2 迁移，持久化观测状态和时间不连续事件；详见 `docs/T4-clock-acceptance.md`。原始 DDL 保持为 v1，迁移是独立文件。当前迁移按版本选择，完整迁移内容校验及大型现场库升级仍未实现。

| 表/字段组 | 口径与用途 |
|---|---|
| runtime_identity / runtime_boots | 持久 runtime_id 与每次启动的 boot_id 分开；单调时间只能在同一 boot 比较 |
| sequence_counters | record 与 event 各自持久高水位；SQL INTEGER 为有符号 64 位，API 转十进制字符串；禁止经 JS number 丢精度 |
| config_revisions | 配置完整规范清单与 Git 提交；历史引用当时版本，不用现行量纲解释旧值；清单不含密钥 |
| devices | 协议限定 mock/opcua；connection_json 存参数及凭据引用，不存凭据；S7 尚不启用 |
| scan_groups | 请求采样/发布频率、队列与丢弃方向；OPC UA 实际 revised 参数在会话日志/健康状态另记，不能把请求频率当成实测频率 |
| tags | 稳定 tag_id、设备、组、协议地址 JSON、类型、工程单位、量程、死区、新鲜度、安全分类和版本 |
| history_raw | 原始样本、quality、源/服务器/接收时间、选定事件时间、时钟段、单调时间、序号及上下文 |
| history_aggregates | 分辨率、桶、版本与时钟段分区；good 数值计数/sum/min/max、各质量计数、首尾样本、算法版本、输入高水位与完成标记 |
| alarm_rules / alarm_rule_state | 规则阈值/延时；运行态抑制和延时进度，独立于实例 |
| alarm_occurrences / alarm_transitions | 每次触发的实例与不可变事实；确认不改条件；新触发新 occurrence_id |
| log_outbox / log_segments / log_index | 待导出的完整 JSON、文件段元数据、可重建查询索引；JSONL 正文不在索引重复存储 |
| recording_coverage / retention_policies | 数据源、作用域、分辨率、时钟段、已记录/缺失/轮转窗口及策略版本 |
| audit_events | 操作主体、operation_id、阶段、Git 提交、双时间与详情；默认运行时 append-only |

`*_at_us` 为 Unix UTC 微秒 INTEGER，不是本地时区文本。区间一律 `[from,to)`。`monotonic_ns` 是当前进程启动后的单调偏移，不采用墙钟换算，不在进程重启后续接旧计时器。程序必须检查整数溢出并显式失败，不能回绕或重置序号。全局时钟段编号持久记录于运行配置/恢复元数据，T4 再实现分配策略。

API 序号有两套：record 序号排序记录，event 序号排序 WebSocket 事件，不能互相比较。record 序号在单写者事务中分配，禁止事务提交前向 HMI/agent 发布；回滚可收回尚未公开的号码，跨重启已发布/持久化号码不复用。跨表全局唯一分配由 runtime 单写者保证，单表主键不能代替它。

### tag 与质量

- OPC UA address_json 将保存 NamespaceUri 与标识符等协议地址，不能依赖重启后可能改变的 namespace index；具体对象结构在 T5 依据库与现场确定。抽象层仅暴露逻辑 tag_id。
- boolean/string/int64/uint64 不启用百分比或绝对模拟量死区；仅 number 可设置 absolute/percent。百分比基于已定义工程量程，不能用当前值作分母。boolean 按变化，string 按完整值变化。int64/uint64 若未来需要数值死区，另定精确整型算法，不转换为浮点。
- quality.level：good=可信有效；uncertain=有值但可信度不足；bad=不可用于正常控制/统计。reason 保留 source_good/source_uncertain/source_bad、stale、disconnected、overflow、decode_error 等规范原因；native_code 原样保留协议状态码。
- null/无有效值用 `{type:'unavailable',value:null}`；禁止把断线映射为数字 0。最后一个好值可作为旧值展示，但必须带 stale/bad 质量，不能冒充最新好值。
- deadband 仅抑制同质量且值变化不足的通知；质量改变、重连首样本、源时间异常必须上报。缺省不靠死区节省容量，历史保留按最坏持续变化估算。报警不应使用历史降采样结果；OPC UA 服务端死区仍可能影响源头可见性，报警关键点默认不启用该死区。
- `stale_after_ms` 是本地接收新鲜度上限，不是源时间与 PC 时间的差值。源时钟偏差单独记录，不直接改写原始源时间。
- JSON Schema 与运行时校验负责 TagValue 类型和值的一致性、有限浮点、int64/uint64 范围、字符串长度、参数字段 allowlist、安全分类等；SQLite 的 json_valid 只证明 JSON 语法正确，不能证明业务有效。DDL 不自称覆盖这些校验。

### 时间选择

有 SourceTimestamp 时优先选它，否则 ServerTimestamp，再否则 observed_at。保留三个原始时间与 timestamp_basis，错误时间不静默修正。`sequence` 是记录顺序，event_at 是事件展示/趋势分桶时间，observed_at 是本地日志检索/接收时间。T1 现已补入 event_timestamp、timestamp_basis、clock_epoch，避免这些信息只藏在数据库。

驱动源时间或本机墙钟发生不连续时切 clock_epoch，异常窗口标 unknown。不同设备源时钟不假定一致，跨设备关联要显示偏差，不能以同一墙钟标签断言先后因果。history coverage 需对应 tag 与分辨率并映射到选定事件时间；无法可靠映射时为 unknown，不从日志接收覆盖推导源事件覆盖。

## 报警状态机

阈值值、布尔触发值、滞回与持续时间均属受控配置。这里只定义机制，不填入现场参数。severity 的 1–4 从低到高，仅为草案排序，不替代现场等级标准。

| 输入/事件 | 条件/运行态转换 | 实例与确认 |
|---|---|---|
| 正常样本满足触发条件 | normal → pending_on；持续达到 on_delay 后 active | 新建 active + unacknowledged 实例，记录 activate |
| 触发延时内恢复 | pending_on → normal | 不创建实例 |
| active 条件进入恢复区 | active → pending_off；持续达到 off_delay 后 normal | 实例变 cleared，保留原确认状态，记录 clear |
| 恢复延时内再次越限 | pending_off → active | 保留原实例 |
| 人工确认 | 不改变条件 | acknowledged，记录操作者和审计，不当作恢复 |
| cleared 但未确认又触发 | 新一轮延时后 active | 创建新实例；旧 cleared/unacknowledged 仍可查询 |
| 数值/布尔规则遇质量 bad/uncertain 或过期 | evaluation → unknown，重置待定延时 | 不伪造 clear；已 active 实例保持，产生 quality_unknown；独立 quality_bad 规则直接评估质量，不走此分支 |
| 新好样本恢复 | 重新判定，不补算未知期间持续时间 | 按当前条件继续/重启延时；原 active 的恢复仍走 off_delay |
| 重启 | 旧单调计时失效，先 unknown，等待新样本 | 保留实例及确认，不沿用旧计时器时长 |

滞回：high 以 `value >= trigger` 触发、`value <= clear` 恢复，要求 clear < trigger；low 反向，要求 clear > trigger。boolean 用配置触发值；quality_bad 仅在持续 bad/stale 时触发，并用恢复为 good 的规则恢复。on/off_delay=0 允许立即转换。延时基于单调时间，且中间采样间隙超过 stale 上限时不能视为持续成立。

抑制不是条件状态：

- shelved：人工、理由、期限、有权限且通过受控写；到期恢复通知。实际条件、历史、报警实例继续评估和落盘。
- suppressed_by_design：仅来自明确配置的设计抑制，不根据未确认的业务状态自行猜测；仍保留条件/抑制事件。
- 当前规则状态决定 API 的 suppression 字段，状态改变有 transition；历史查询不能把当前抑制状态倒套给旧记录。
- interlock/safety 规则禁止修改与搁置；仅查看/记录不等于修改 PLC 安全逻辑。设计抑制配置未定义时不得启用。
- shelve 到期同时保存墙钟期限、当前 boot 的单调期限；墙钟回跳不能延长搁置；重启且无法验证剩余有效期时取消搁置并记录恢复事实。T4 实现，当前 DDL 只保存持久状态。

一条规则最多一个 active 实例由部分唯一索引保证。报警清除、确认、抑制的多表更新与对应日志 outbox 要同一事务；确认主体权限、状态转换完整性由 runtime 检查，不能只靠 CHECK 约束。

## 历史降采样、聚合与保留

以下保留期是容量场景参数，未获现场确认，不在 DDL 中偷偷设为上线默认值。

| 分辨率 | 场景保留期 | 用途 |
|---|---|---|
| raw | 6 小时 | 快速故障追踪；保留已接收采样与质量变化 |
| 1s | 24 小时 | 近期趋势 |
| 1m | 30 天 | 月内运行趋势 |
| 1h | 1095 天（按 365 天/年估算） | 三年概况 |

每层保留 sum、numeric_good_count、min、max、首尾完整样本和质量计数。`mean=sum/numeric_good_count`，没有 good 数值则 null。boolean/int64/uint64/string 的 min/max/mean 为 null，本阶段不对其强行做浮点统计；变化详情只在 raw 保留，长期需求须用户另定。

聚合为样本加权，不是时间加权：不补点、不前向填充、不把一个值重复到缺口。固定 UTC 边界，以 event_at 分桶；版本或 clock_epoch 不同则分开返回。`first/last` 按序号取，不依赖墙钟排序。跨桶合并要累计 sum/count 和 min/max，禁止平均各桶 mean。桶边界遇回跳/版本切换可有同一窗口多条结果，API 显式附带版本与时钟段。

实时聚合可更新未完成桶，写入 source_high_water；延迟样本只在原始层仍保留且桶可重算时幂等重算。删除下层前必须确认上层已提交并 finalized，质量计数守恒，覆盖元数据同事务更新。超出补算窗口的迟到数据记录 late_sample 并标聚合窗口不完整，不默默篡改三年前统计。具体迟到容忍窗口由 T3/T4 实测后定，未配置不自动清理 raw。

按有限批次 DELETE，避免长事务拖住采集；WAL checkpoint 有界执行，长读快照设有效期。删除释放页不必立即缩小文件；不在采集主循环运行 VACUUM。容量回收/分片策略须在 T4 用目标吞吐和磁盘做验证。WAL 改善并发，不是容量压缩，也不保证 1 万次独立 fsync/秒可承受。

## JSONL 日志、审计与覆盖

JSON Schema：`contracts/log-record.schema.json`。每行单个 UTF-8 JSON 对象加 LF；stamp 包含 record sequence、墙钟、boot_id、monotonic_ns。context 的 device/tag/scan_cycle/session_id 四个键始终存在，不适用时显式 null。message_zh 给人读，event_code/fields 给工具筛选；禁止凭据进入字段。

建议记录驱动状态变化、重建订阅、质量变化、报警转换、写入动作、丢弃/过载和周期汇总。不能给每个 100ms 样本再写一条普通日志；原始样本在 history_raw，诊断日志按事件和限频摘要记录。限频计数与被省略窗口显式写出，error/审计不得静默限频丢弃。

拟议日志策略：普通诊断日志保留 90 天；单段 128 MiB 或 24 小时轮转，先到为准；90 天预计容量之外另设 128 GiB 预警预算，超预算不得静默把 90 天缩成几小时。磁盘不足/记录失败显式置 persistence=degraded/unavailable、报警并登记缺口；是否停止业务动作由后续现场要求决定，当前不承诺满盘还能无损。审计目标三年，单独容量和受控导出，不随普通日志轮转。策略变更本身持久记录，并保留历史策略版本。

落盘协议草案：

1. 样本/报警和 log_outbox 在同一 SQLite FULL 事务提交；提交成功前不宣称持久化成功。
2. 导出器顺序追加 JSONL，fsync 文件；记录字节偏移、长度、durable_bytes 后，在 SQLite 事务写 log_index 并移除相同 outbox 项。
3. 崩溃在追加与登记之间时，启动扫描末段，以序号核对已落盘行，截断不完整尾行后重放未确认 intent；以序号去重，不能重复显示为两次故障。目录项/轮转元数据需 fsync 或目标平台等效持久化，T4 实测。
4. 查询不能忽略尚在 outbox 的已提交日志；同一固定快照联合文件索引与 outbox，以 sequence 去重。正文损坏、文件不存在、索引落后都返回缺口/unknown，不能当作零匹配。
5. 轮转删除先标 deleting，待查询快照/读租约释放后删文件，再标 deleted 并更新覆盖墓碑；恢复未完成步骤时保持保守覆盖。墓碑与策略版本至少保存三年，即使正文已删除也能回答“已轮转”。

覆盖由周期性记录能力检查点确认。没有日志事件但记录服务持续健康，可以是 complete 且 matched_count=0；进程宕机、未启用、磁盘失败、轮转、时钟不确定必须是缺口。log segment 首尾日志时间不能代表覆盖起止。多个源合并以请求范围、作用域和实际区间交集计算，不凭某个源完整就宣布全部完整。

audit_events 的 SQL 触发器拒绝 UPDATE/DELETE，仅是程序误用防护；能改数据库文件/DDL 的管理员仍可绕过，不能宣称防篡改。agent 只走只读 API，文件系统权限隔离在部署实现。三年审计归档需要专门校验、访问控制和恢复流程，不由普通清理线程删表。

## 容量估算：硬证据与假设

实测证据在 `t2/evidence/schema-verification.json`：临时 SQLite 3.53.1，分别插入 20,000 条合成 raw 和 aggregate；含当前索引的新增分配页均摊约 raw **192.92 B/条**、aggregate **1060.45 B/条**。样本为单 tag、短 ID、数值；真实字符串、UUID、更多索引、长快照都会增加，不可直接套用为现场固定大小。

预算取 raw 384 B/条、aggregate 1536 B/条；假定每个点每 100ms 都变化，所有点每层都有桶，不计算死区或稀疏压缩收益。普通日志按 10 条/秒、正文 700 B/条、索引 192 B/条、90 天；审计按 1000 条/天、2048 B/条、三年。历史预留 2 倍空间用于 WAL、页空洞、短期清理/重算，不含备份。以上都是公开假设，不是保证上界。

公式：`raw_rows = 点数 × 每秒样本数 × 保留秒数`；`aggregate_rows = 点数 × 保留秒数 / 桶秒数`；`bytes = rows × 单条预算`。单位 GB/TB 为十进制，GiB 与它不同。

| 点数 | 历史四层合计 GB | 含 2 倍历史空间、普通日志、审计的 GB | 三年全量 raw 的 TB |
|---:|---:|---:|---:|
| 100 | 32.238 | 136.080 | 36.329 |
| 1000 | 322.376 | 716.356 | 363.295 |
| 5000 | 1611.878 | 3295.361 | 1816.474 |

可粘贴复算：

```bash
cd /workspace/ltp
python3 scripts/verify-t2.py
python3 scripts/estimate-capacity.py
```

1000 点 × 10 Hz 是 10,000 样本/秒、一天 864,000,000 条。因此保留周期是设计输入，不能先承诺“SQLite 存三年 raw 没问题”。即使容量够，吞吐、写放大、SSD 耐久、满盘策略和批量提交的最大在途窗口仍须 T3/T4 目标机实测；当前本地探测不是性能验收。

## 验证边界

已验证 DDL 在 Python SQLite 3.53.1 及 Node v24.22.1 自带 SQLite 3.53.4 的临时文件库执行。Python 检查 WAL/FULL/外键、死区类型/量程约束、单规则单 active、确认不清条件、审计 SQL 禁改、零日志与覆盖可分开表达、回跳样本按序号排序、counter/outbox 事务回滚、外键与 integrity_check。Node 检查 DDL、WAL/FULL/外键与完整性；证据分别保存。

日志 Schema 使用环境已有 jsonschema 验证，接受有效样本并拒绝缺失上下文键、错误序号类型/符号、不合法日期。该环境默认 FormatChecker 缺少可选 date-time 检查器，首次负向测试发现其静默跳过格式校验；验证脚本已显式注册 UTC 微秒子集解析器。后续 runtime 也必须启用日期格式断言，不能仅依赖 format 注解。工具复算命令为 `python3 scripts/verify-log-schema.py`（需要环境中已有 jsonschema）及 `node scripts/verify-t2-node.mjs`，未向 runtime 添加 Python 依赖。

没有实际拔电、没有改系统时钟、没有跑 OPC UA 或报警引擎；应用级约束尚未实现，TypeScript 只做类型剥离后的模块语法检查，未完成语义类型检查。

### 已实现的增量聚合

schema v4 用持久序号记录聚合进度。runtime 在样本事务和维护检查点各处理至多 16 条待聚合原始记录，分别更新秒、分钟和小时桶。每个分辨率直接累计源样本。三层结果与进度在同一事务提交，进程重启后从已提交序号继续。旧库随采集或检查点逐批补算。较大旧库的追赶速度和新增写放大仍需在目标工控机测量。

诊断工具 query_history 可选择 raw、1s、1m、1h。聚合查询接受按分辨率对齐的 UTC 起止时间，返回各配置版本和时钟段的独立结果。每桶返回质量计数、首尾样本和 good 数值统计，超出浮点求和范围时 mean 为 null，sum_overflow 为 true。backfill_pending 和 aggregated_through_sequence 表示本次快照的补算进度。覆盖仍按 unknown 报告。

当前桶允许继续接收样本，finalized 为 0。保留期、迟到窗口和现场容量尚待确认，本次没有启用自动删除或释放磁盘空间。
