import { execFile } from 'node:child_process';
import { isAbsolute } from 'node:path';
import { promisify } from 'node:util';

const run = promisify(execFile);
const station = 'stn.140';
const stream = 'sim.three-cart.v1';

export default function register(pi) {
  pi.registerTool({
    name: 'read_simulation_snapshot',
    label: '读取 C++ 模拟诊断快照',
    description: '读取唯一 SPS 模拟工位的保留帧与本机模拟日志尾部。返回帧质量、心跳与新鲜度。覆盖未知；不支持旧版五工具的时间窗、标签、报警记录、日志检索或关联契约。',
    parameters: {
      type: 'object',
      properties: {
        station_id: { type: 'string', enum: [station] },
        limit: { type: 'integer', minimum: 1, maximum: 40 },
      },
      required: ['station_id'],
      additionalProperties: false,
    },
    async execute(_id, input, signal) {
      if (!input || input.station_id !== station ||
          Object.keys(input).some(key => key !== 'station_id' && key !== 'limit')) {
        throw new Error('仅支持 stn.140 模拟工位和 limit；其他工位或筛选条件不可用。');
      }
      const limit = Object.hasOwn(input, 'limit') ? input.limit : 5;
      if (!Number.isInteger(limit) || limit < 1 || limit > 40) {
        throw new Error('limit 必须为 1..40 的整数。');
      }
      const { LTP_CPP_DIAGNOSE: binary, LTP_CPP_DATA_DIR: directory } = process.env;
      if (!binary || !directory || !isAbsolute(binary) || !isAbsolute(directory)) {
        throw new Error('请以绝对路径配置 LTP_CPP_DIAGNOSE 与 LTP_CPP_DATA_DIR。');
      }
      let details;
      try {
        const { stdout } = await run(binary, [directory, 'snapshot-json', String(limit)], {
          shell: false, windowsHide: true, timeout: 5000, maxBuffer: 256 * 1024, signal,
        });
        details = JSON.parse(stdout);
      } catch {
        throw new Error('C++ 模拟诊断失败，证据不足。检查本机程序和模拟数据目录；不会回退到旧服务。');
      }
      if (details?.schema !== 'ltp.cpp.sim-snapshot.v1' || details.simulation_only !== true ||
          details.scope?.station_id !== station || details.scope?.stream_id !== stream ||
          details.query !== 'retained_tail' || details.coverage !== 'unknown' || details.limit !== limit ||
          !Array.isArray(details.frames) || details.frames.length > limit ||
          !Array.isArray(details.logs) || details.logs.length > limit) {
        throw new Error('C++ 返回的接口版本、范围或数据不符，证据不足。');
      }
      return { content: [{ type: 'text', text: JSON.stringify(details) }], details };
    },
  });
}
