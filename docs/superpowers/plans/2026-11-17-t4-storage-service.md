# T4 Storage and Service Implementation Plan

> **For agentic workers:** Implement task-by-task in this session. 用户明确要求继续做完；不另设已授权实现的审批，真实现场验收仍单列。

**Goal:** 补齐可在当前环境完成的日志恢复/轮转/覆盖记录、实际调度和看门狗；验证强制终止后的恢复，不冒充物理拔电。

**Architecture:** 独立 lease.sqlite 持有排他事务，保护数据目录，崩溃后内核释放锁。LogStore 管理有校验的 JSONL、SQLite outbox 重放、分段和保留墓碑；MockLoop 只负责业务事务。独立调度器使用单调时间计算 deadline，超时显式报告、不追赶无限旧周期。进程外 watchdog 能发现主进程事件循环阻塞。

**Tech Stack:** Node built-ins、SQLite WAL/FULL；无新增 npm 运行依赖。

**Spec:** 用户 T4，docs/T2-data-model.md，docs/T4-clock-acceptance.md。

## Global Constraints

- 不连现场、不实现未明确的扫码复位、不调用 LLM。
- 已确认数据不得静默截断，未确认尾部先备份再修复，未知完整记录拒绝猜测。
- 普通日志保留90天/128MiB或24h分段；测试可显式使用缩小的合成策略，策略版本持久化。
- 无匹配、已轮转、未记录及时间不确定必须可区分。
- 全部测试、来源点表不变检查、提交与推送后远程核对。

## Review Focus

- SQLite已提交、JSONL未写；JSONL已fsync、索引未提交；尾行仅写一半。
- 第二进程并发启动不能因残留锁处理而闯入。
- 轮转删除被中断后墓碑仍然可查，时钟前跳不能提前清理90天数据。
- 静默无事件时仍有记录能力检查点，断档不能靠匹配日志首尾冒充覆盖。
- 调度阻塞后显式报告错过周期；进程外watchdog独立检测失去心跳。

### Task 1: Lease 与恢复
- [x] 先写恢复、损坏拒绝、单写者及强制终止测试并确认失败。
- [x] 实现runtime/src/process-lease.mjs、runtime/src/log-store.mjs、db/003-log-lifecycle.sql；接入MockLoop并保留已有行为。
- [x] 验证outbox重放及JSONL尾部幂等恢复，不重复确认日志。

### Task 2: 轮转、保留与查询证据
- [x] 先写按大小/时间轮转、单调保留、不连续窗口、墓碑与精确匹配计数测试。
- [x] 实现checkpoint/prune/queryLogs；策略与覆盖单独落盘；加有界查询。
- [x] 验证零匹配与已轮转、宕机间隙、损坏能明确区分。

### Task 3: 调度和看门狗
- [x] 先写分组deadline、超时跳过、异常传播及外部watchdog测试。
- [x] 实现runtime/src/group-scheduler.mjs、runtime/src/watchdog.mjs、runtime/examples/service.mjs。
- [x] 跑实际定时服务与SIGKILL故障注入，确认高水位不回退和恢复报告。

### Task 4: 汇总验收
- [x] 运行全部runtime测试、T2检查与日志Schema验证；保存证据。
- [x] 明确剩余现场验收、性能/Windows边界；更新README和报告，提交并同步GitHub。
