# T4 第一部分：时钟不连续记录

日期：2026-11-17。环境：Linux、Node v24.22.1、Node SQLite 3.53.4。

状态：样本级时钟处理与 mock 验证完成；**T4 总体验收尚未完成**。没有修改系统时钟，没有执行物理拔电，没有部署 Windows 或连接现场。

## 实现

- `runtime/src/clock-tracker.mjs`：纯预计算与提交分开，异常输入不改变状态。
- `db/002-clock-tracking.sql`：schema v1→v2，新增 clock_tracker_state、clock_events；保留原历史及最大 epoch。迁移在排他启动阶段执行，当前只对小型 mock 库验证，未做大库迁移性能验收。
- `runtime/src/mock-loop.mjs`：历史样本、不连续事件、clock state 与 JSONL intent 同事务。失败事务整体回滚，内存只在事务提交后更新。
- `MockDriver.jumpWallTime()`：仅注入模拟墙钟；虚拟单调时间和分组调度不倒退。

墙钟、事件时间和序号分别保留，不为了排序修改原始时间：

| 条件 | 记录原因 |
|---|---|
| 同一 boot 墙钟倒退 | wall_backward |
| 墙钟增量与单调增量差超过阈值 | wall_jump |
| boot_id 改变 | boot_boundary，不比较不同 boot 的单调时间 |
| 同一事件时间域回跳 | event_backward |
| 事件时间增量与接收时间增量差超过阈值 | event_time_discontinuity |
| source/server/observed 时间域切换 | event_basis_changed |

当前 mock 判定阈值为 1000ms，任意负向墙钟增量单独记录。它是工程初值，不是现场验收参数。时间不连续是观测事实，不单独证明 NTP、RTC 或 PLC 时钟哪个是根因；迟到数据也可能造成事件时间不连续。

clock_epoch 为保守的全局观测分段，单次观测多个原因只增加一个段。样本保留当时 source/server/observed，不覆盖为修正时间。不同设备源时钟仍不假定一致，不能跨设备只按墙钟断言因果。

## 实测结果

完整 runtime 测试 **21 项通过、0 项失败**，包括旧有 12 项及以下新增场景：正常稀疏采样、墙钟前后跳、源事件回跳/前跳、时间域切换、boot 切换、单调时间倒退拒绝、无副作用 preview、过期 preview 拒绝、失败事务重试和旧库迁移。

时钟演示证据：`t4/evidence/clock-demo.json`。

| 项目 | 结果 |
|---|---|
| 注入 | 墙钟后跳 1 小时，再前跳 2 小时 |
| 样本数 | 4 |
| 样本序号 | 1、3、6、9，严格递增；中间号码用于日志/时钟事件 |
| 样本 epoch | 0、0、1、2 |
| JSONL | 6 条，含 2 条 clock.discontinuity；全部通过日志 Schema 验证 |
| 未导出 outbox | 0 |
| SQLite integrity_check | ok |

旧版 mock 数据库迁移测试保留旧样本，并从既有 epoch=7 继续到新 boot 的 epoch=8。故障注入在历史写入时中止事务，验证 clock_events 与时钟状态也回滚，重试仍记录同一次不连续，不会因内存先更新而漏记。

原 T3 十次模拟断线演示仍通过：60 条历史、60 条日志、outbox=0。T2 的 SQLite 约束检查与日志 Schema 正反例检查通过；140 来源点表 SHA256 未变。未增加 npm 依赖。

## 复算

```bash
cd /workspace/ltp
npm test --prefix runtime
npm run clock-demo --prefix runtime
npm run demo --prefix runtime
```

测试和演示仅在临时目录写入；不改现场、不改系统时间。

## 尚未完成的 T4 要求

- 独立后台时钟观测、分组实际调度与看门狗；当前只在收到样本时检测，静默期间不会立即产生时钟事件。
- JSONL 尾行/SQLite 跨资源恢复、异常退出后写入锁恢复、真实 SIGKILL/物理掉电恢复验证。
- 日志轮转、保留覆盖检查点、降采样和容量清理。
- 全量时钟状态 JSON 当前随样本持久化，尚未针对大点数优化；不能据此宣称达到现场 100ms 组吞吐。
- 手动改测试工位系统时间、Windows 文件持久化、磁盘缓存和电源故障实测。

这些项目未通过前不能声称 T4 达成“拔电/改系统时间后数据不丢、不错序”。本次仅证明输入时间跳变时的记录顺序、事务一致性和证据可见性。
