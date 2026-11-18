import { OpcuaDriver } from './src/adapter.mjs';
import { AcquisitionStore } from '../../src/acquisition-store.mjs';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';

export async function startAcquisition(config, directory, rules = []) {
  const driver = new OpcuaDriver(config);
  const store = new AcquisitionStore({
    directory, manifest: config.manifest, rules, sample_logs: false,
    opcua_source: { endpoint: config.endpoint, bindings: config.bindings }
  });
  let stopping;
  let finish;
  let fail;
  const done = new Promise((resolve, reject) => { finish = resolve; fail = reject; });
  done.catch(() => {}); // 启动阶段也可能收到同步采集错误。
  function stop(error) {
    if (!stopping) stopping = Promise.resolve().then(async () => {
      try { await driver.disconnect(); } finally { store.close(); }
      if (error) throw error;
    });
    stopping.then(finish, fail);
    return stopping;
  }
  function persist(action) {
    if (stopping) return;
    try { action(); } catch (error) { stop(error); }
  }
  driver.on('sample', sample => persist(() => store.accept(sample)));
  driver.on('diagnostic', event => persist(() =>
    store.writeEvent(event.code, event.message_zh, event, 'warn')));
  try { await driver.connect(); } catch (error) { await stop(error); }
  if (stopping) await stopping;
  return { stop: () => stop(), done, snapshot: () => driver.snapshot() };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const [file, directory] = process.argv.slice(2);
  if (!file || !directory) throw new Error('用法: node service.mjs 配置.json 数据目录');
  const { driver, rules } = JSON.parse(await readFile(file, 'utf8'));
  const service = await startAcquisition(driver, directory, rules);
  for (const signal of ['SIGINT', 'SIGTERM']) process.once(signal, () => service.stop());
  await service.done;
}
