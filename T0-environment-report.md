# T0 环境验证报告

状态：部分通过，阻塞于 LLM 实测所需配置。验证环境为本地 Linux x86_64 云工作区，不代表现场 Windows。

## 更正：pi 官方身份与安装实测

此前在未核实官方包名时使用 `@mariozechner/pi-coding-agent` 查询，并据此停止，是验证流程错误。以下官方证据已核实，原包查询失败不再作为阻塞依据：

- 官方仓库：https://github.com/earendil-works/pi
- 正式发布：https://github.com/earendil-works/pi/releases/tag/v1.5.2 （非 prerelease，发布时间 2026-11-10T12:25:43Z）。
- 版本化包定义：https://github.com/earendil-works/pi/blob/v1.5.2/packages/coding-agent/package.json
- 正确包名：`@earendil-works/pi-coding-agent`，版本 `1.5.2`，Node 要求 `>=22.19.0`。
- GitHub 元数据经 GitHub 连接器读取；这不证明现场企业内网可以访问 GitHub。
- 指定 npm 镜像查询正确包名成功，repository 指向上述官方仓库。
- 本地独立安装位置：`t0/pi-install`；未全局安装。
- 实际安装使用指定镜像、`--ignore-scripts --no-audit --no-fund`；123 个包，31 秒，退出码 0。
- `./t0/pi-install/node_modules/.bin/pi --version` 原始输出：`1.5.2`，退出码 0。

### 依赖固定与离线交付验证

- 直接依赖固定为 `1.5.2`，`package-lock.json` v3 固定 123 个依赖条目，全部 resolved URL 来自指定镜像。
- 采用完整 node_modules 归档方案，不以普通 npm 安装或锁文件单独充当离线可复现证据。
- 归档：`t0/artifacts/pi-node_modules-linux-x64-node24.tar.gz`（本地保留，不入 Git）。
- SHA256：`d7d5d238526f91f199806fe26de549977fab1ab2a97adb83d1867966ded64b74`。
- 校验成功后解压到全新目录，不执行 npm；15,541 个文件/符号链接条目的内容、文件权限及链接目标与原目录一致。
- 还原后的 `pi --version` 同样输出 `1.5.2`。
- 限制：验证的是同平台依赖镜像还原，不是源码可重复构建；没有关闭操作系统网络，不能声称完成物理断网验证。归档不包含 Node；Node 发行物及校验尚需归档。Windows 需要单独制作和验证，不能直接复用 Linux 依赖树。

### 当前真实阻塞

未发现可识别的 LLM API 凭据，尚未确定服务商、Base URL 和模型。仅发现 `OPENAI_CLUSTER` 环境变量名称，不把它视为可用推理凭据，未输出其值。未发起计费推理请求，也没有测量工具调用延迟。

本地模型探测：内存约 9.7 GiB，无 swap，文件系统约 7.9 GiB；未发现 `nvidia-smi`、`ollama` 命令或名称匹配的本地模型进程。这不能证明机器没有 GPU，也不能证明本地模型绝对不可行。没有下载模型或自行切换服务。

## 已完成

- 工作目录：`/workspace/ltp`。
- Git 空提交回滚起点：`f70aced`。创建目录和初始化仓库属于建立回滚机制的必要引导操作；之后才进行项目文件写入。
- Node 实测：`v24.22.1`，满足 `>=22.19.0`。
- npm 实测：`11.9.0`。
- Git 实测：`2.52.0`。
- npm 镜像：`https://registry.npmmirror.com`。
- `npm ping` 实测：`PONG 1153ms`。

## 历史错误查询证据（已撤销其阻塞结论）

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

- 完整断网依赖交付验证及 Node 发行物归档：尚未完成；已完成同平台依赖归档还原。
- LLM API 连通性：未验证。
- 完整 tool-calling 往返延迟：未测量，无数值。
- 本地模型可行性：未验证。
- Windows / Electron 部署可行性：未验证。

## 恢复验证所需信息

需要明确 LLM 服务商、模型名、实际 Base URL，并在运行环境安全配置凭据（不通过聊天或 Git 传递密钥）。T0 未通过，不进入 T1，不创建业务或前端代码。
