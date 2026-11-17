# T0 环境验证报告

状态：阻塞，未通过。验证环境为本地 Linux 云工作区，不代表现场 Windows。

## 已完成

- 工作目录：`/workspace/ltp`。
- Git 空提交回滚起点：`f70aced`。创建目录和初始化仓库属于建立回滚机制的必要引导操作；之后才进行项目文件写入。
- Node 实测：`v24.22.1`，满足 `>=22.19.0`。
- npm 实测：`11.9.0`。
- Git 实测：`2.52.0`。
- npm 镜像：`https://registry.npmmirror.com`。
- `npm ping` 实测：`PONG 1153ms`。

## 阻塞证据

执行命令：

```bash
npm view '@mariozechner/pi-coding-agent@1.5.2' name version repository engines bin dist.integrity --json --registry=https://registry.npmmirror.com --fetch-retries=0 --fetch-timeout=30000
```

退出码：`1`。关键原始输出：

```text
npm error code E404
npm error 404 No match found for version 1.5.2
npm error 404 The requested resource '@mariozechner/pi-coding-agent@1.5.2' could not be found or you do not have permission to access it.
```

证据边界：这是候选包查询；官方包身份尚未核实。该结果不证明 pi 1.5.2 不存在，也不能区分包名变化、镜像同步问题或版本信息问题。没有换源、换版本或替换 harness。

## 尚未验证

- pi 安装及 `pi --version`：未执行，无实测输出。
- 依赖锁定与离线可复现：未验证；未创建依赖树或声称锁文件足够。
- LLM API 连通性：未验证。
- 完整 tool-calling 往返延迟：未测量，无数值。
- 本地模型可行性：未验证。
- Windows / Electron 部署可行性：未验证。

## 恢复验证所需信息

需要确认用户所指 pi 1.5.2 的官方发布地址或准确 npm 包坐标。确认后继续 T0，不进入 T1，不创建业务或前端代码。
