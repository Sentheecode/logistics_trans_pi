import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm, readFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { OPCUAServer, OPCUACertificateManager, Variant, DataType } from 'node-opcua';
import { startAcquisition } from '../service.mjs';

test('本地 OPC UA 数据进入历史、报警和 JSONL，断链保持活动报警', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'ltp-history-'));
  const server = new OPCUAServer({ port: 0, host: '127.0.0.1', hostname: '127.0.0.1',
    serverCertificateManager: new OPCUACertificateManager({rootFolder: join(dir,'pki')}) });
  let service, db, running = false, counter = 0, timer;
  async function until(sql) {
    for(let i=0;i<200;i++) {
      if(db.prepare(sql).get().n > 0) return;
      await new Promise(resolve => setTimeout(resolve, 30));
    }
    assert.fail(sql);
  }
  try {
    await server.initialize();
    const space = server.engine.addressSpace;
    const ns = space.registerNamespace('urn:ltp:persistence-test');
    ns.addVariable({organizedBy:space.rootFolder.objects, nodeId:'s=counter', browseName:'Counter',
      minimumSamplingInterval:50, dataType:DataType.UInt32,
      value:{get:()=>new Variant({dataType:DataType.UInt32,value:counter})}});
    await server.start(); running = true;
    timer = setInterval(()=>counter++,50);
    const directory = join(dir,'history');
    service = await startAcquisition({
      endpoint:server.endpoints[0].endpointDescriptions()[0].endpointUrl, security_mode:'None',
      allow_insecure_loopback:true,pki_directory:join(dir,'client'),heartbeat_tag_id:'counter',
      manifest:{device_id:'test',groups:[{id:'fast',interval_ms:100,tags:[
        {id:'counter',type:'number',deadband:{mode:'none',value:0}}]}]},
      bindings:{counter:{namespace_uri:'urn:ltp:persistence-test',identifier:'counter'}}
    },directory,[{id:'high',tag_id:'counter',kind:'high',trigger:2,clear:1}]);
    db = new DatabaseSync(join(directory,'runtime.sqlite'), {readOnly:true});
    await until("SELECT count(*) n FROM alarm_occurrences WHERE condition='active'");
    await server.shutdown(0); running = false;
    await until("SELECT count(*) n FROM history_raw WHERE quality='bad' AND json_extract(value_json,'$.type')='unavailable'");
    await service.stop();
    assert.equal(db.prepare("SELECT protocol FROM devices").get().protocol,'opcua');
    assert.equal(db.prepare("SELECT condition FROM alarm_occurrences").get().condition,'active');
    const logs=(await readFile(join(directory,'events.jsonl'),'utf8')).trim().split('\n').map(JSON.parse);
    assert.ok(logs.some(r=>r.event_code==='alarm.activate' && r.context.tag==='counter'));
    assert.ok(logs.some(r=>r.event_code==='alarm.quality_unknown'));
  } finally {
    clearInterval(timer);
    try { await service?.stop(); } finally {
      db?.close(); if(running) await server.shutdown(0);
      await rm(dir,{recursive:true,force:true});
    }
  }
});
