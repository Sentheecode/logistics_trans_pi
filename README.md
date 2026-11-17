# logistics_trans_pi

工业物流中间层：上接 LWMS 与两家 AGV，下接 OPC UA PLC。runtime、agent、hmi 独立生命周期；最终计划用 Electron 提供现场桌面界面，runtime 独立服务运行。

当前完成的是接口/数据模型草案、业务点表基线及 T3 mock 内核，尚未连接现场，尚未实现前端。

- T0：pi 1.5.2 已锁定和实测；LLM 连通性/工具调用延迟经用户暂缓，Windows/完整离线部署尚未验收。
- [T1 接口契约](docs/T1-process-and-api.md)
- [T2 数据模型与容量](docs/T2-data-model.md)
- [业务基线](docs/business/requirements-v0.1.md)、[读写同一行的点表结构](docs/business/point-table-v0.1.md)
- [T3 mock 运行与限制](runtime/README.md)

本地验证（不连接 PLC、不调用 LLM）：

```bash
npm test --prefix runtime
npm run demo --prefix runtime
```
