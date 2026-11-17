# runtime：独立 mock 服务与 T4 存储加固

零 LLM、零 pi 依赖、零 npm 运行依赖。当前提供可实际定时运行的 mock runtime 和独立监督进程；没有真实 PLC、HTTP/WS、agent 工具或 HMI。测试不加载 140 点表，不执行现场写入。

当前实测 Node v24.22.1 / Linux / SQLite 3.53.4。最低版本声明为 Node22.19，但该最低版本、Windows 和 Electron 打包尚未验收，不把 Linux 成功等同现场可部署。

## 可运行命令

从项目根目录执行，不需要 npm install：

```bash
npm test --prefix runtime
npm run demo --prefix runtime
npm run clock-demo --prefix runtime
npm run storage-demo --prefix runtime
```

有限时长实际定时运行，生成新临时目录并打印路径：

```bash
npm run service --prefix runtime -- --duration-ms 2200
```

长期 mock 运行并自动监督重启（Ctrl+C 停止），请使用专用新目录：

```bash
npm run supervisor --prefix runtime -- --directory /workspace/ltp/runtime/var/mock
```

目录包含 runtime.sqlite、lease.sqlite、runtime.lock、events*.jsonl 及可能的恢复副本。目录路径必须在重启间保持一致；不传目录则每次生成新临时目录。不要把现有生产目录交给 mock，也不要将多台上位机的数据目录指向同一网络共享 SQLite 文件。runtime/var 已加入 Git 忽略。

service 单独运行默认只报告看门狗超时；传 --watchdog-kill 才启用 worker 强制终止自己所在进程。supervisor 启动的子 runtime 默认启用该选项，由父进程负责重启。它只能管理自己启动的 mock 子进程，不接受任意 PID/命令。60 秒最多重启 3 次，默认间隔 1 秒；超过预算明确失败，不无限拉起。父监督进程自身仍需由操作系统服务管理器承载；Windows 服务注册尚未实现。

## 数据与恢复

- schema v1→v2→v3 迁移是独立 SQL。保留原始历史、序号与配置版本；未知版本拒绝运行。
- ProcessLease 在独立 lease.sqlite 中持有排他事务；内核在进程死亡时释放锁。runtime.lock 仅是原子更新的身份记录，不用 PID 猜测删除锁。旧格式/损坏标记需停机核对，不能自动接管未知旧进程。
- 历史、报警、时钟状态和日志 intent 同 SQLite WAL/FULL 事务。日志追加并 fsync，再提交索引和确认长度；JSONL 每条记录有 SHA256，封存段有校验。
- 重启核对已确认前缀；outbox 重放；完整未确认行登记一次。半行/已知重复尾部先保存恢复副本及 intent，修复后记录完成。未知完整记录、冲突或已确认内容损坏时拒绝覆盖。
- 中断的轮转删除由 deleting→deleted 状态恢复。正文被删后保留段元数据和已轮转覆盖记录，不静默伪装成无故障。
- 活动文件被删除/替换时拒绝继续确认写入；查询正文缺失/校验不符返回证据错误，健康状态显式降级。

## 日志策略与查询证据

当前软件默认值（仍需现场容量确认）：单段 128 MiB 或累计运行 24 小时轮转；普通日志至少保留 90 天；单条最大 1 MiB。JSONL 正文预算 128 GiB，磁盘剩余预留 256 MiB；超过预算/低于预留只降级告警，不提前删除年轻日志。预算不含 SQLite、恢复副本或操作系统空间，不能当作整盘容量估算。

保留时长由持久单调运行时间累计，关机期间不计入，宁可多保留；墙钟前跳不能提前清理。策略 JSON 和版本、覆盖区间、轮转墓碑、恢复动作独立落盘。恢复副本/墓碑当前不自动删除，避免清理证据；长期归档与容量规模需后续部署验证。

MockLoop.queryLogs 是本地有界查询，不是已上线的 HTTP/agent 工具：返回 matched_count、实际覆盖区间数组、缺口及策略版本。总匹配与本页条数分开；字面文本筛选不执行正则或 SQL。候选记录超过 10000 条会明确报错，不把截断结果当精确总数。当前没有跨页游标、网络身份验证或查询CPU限时，T6 接口仍需实现。

记录能力由周期 checkpoint 跟踪，不从匹配日志首尾推算。检查点间隔超过 5 秒、跨启动间隙、时钟不确定与轮转窗口不会冒充完整覆盖。边界采用 UTC 半开区间，微秒输出；没有匹配但覆盖完整，与日志已轮转是不同结果。旧库没有覆盖元数据的窗口也不补造完整性。

## 实际调度与看门狗

GroupScheduler 用单调 deadline；各组独立、不重入。迟到显式记录错过周期并跳过旧周期，避免无限追赶；异步组超时或失败通知调用方。停止有超时和取消信号。

MockService 运行采集组和每秒独立维护组；无 tag 变化时仍观测墙钟、检查日志记录能力、清理到期段和记录健康摘要。mock heartbeat 超时或订阅状态异常可按清单重建。OPC UA 真实健康检测仍属于 T5，不能从 mock 推导已通过。

看门狗使用独立 worker 和共享单调心跳，不依赖主线程定时器；已实际验证主线程阻塞时终止测试子进程。service 的正常日志采用质量变化/报警/周期摘要，而不是每次心跳电平一条普通日志。底层 MockLoop 保留 sample_logs=true 的小规模演示兼容模式，不能拿它当全厂高频日志方案。

## 复算崩溃和自动重启

以下只杀死脚本自己创建的临时测试子进程，不接触已有服务：

```bash
node scripts/verify-t4-crash.mjs
node scripts/verify-t4-supervisor.mjs
```

已验证进程 SIGKILL，不代表物理拔电；已注入墙钟变化，不代表改了测试工位系统时间。目录 fsync 支持情况和磁盘空间显式在 health 中报告；不支持时降级，不能声称 Windows 完整持久化已通过。

## 当前边界

- 报警内核仍限于 high/low、滞回与零延时；确认、抑制和完整报警状态机尚未运行实现。
- 历史降采样/自动清理目前仍是 T2 schema 与策略设计；未开启破坏性历史清理，不声称三年容量已在目标机验收。
- 时钟 tracker 当前按全量 JSON 持久化，启动会校验现存日志；大点数、长期存量、10k 样本/秒、写放大和SSD寿命均未验收。
- T0 的 LLM 实测仍按用户要求暂缓。T4 真实电源/系统改时钟及 Windows 服务/持久化未验收。
- 三车逐台扫码和各车在当前工位的全部完成语义已确认；现场触发绑定、复位时序、跨机共享和外部接口仍需落实，不因通用存储代码完成而猜测业务。

完整结果见 docs/T4-runtime-acceptance.md；后续真实 OPC UA 接入需要测试端点和节点清单。
