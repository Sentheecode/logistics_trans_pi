import { EventEmitter } from 'node:events';

const finite = n => typeof n === 'number' && Number.isFinite(n);
function checkValue(tag, value) {
  if (tag.type === 'number' && finite(value)) return;
  if (tag.type === 'boolean' && typeof value === 'boolean') return;
  if (tag.type === 'string' && typeof value === 'string') return;
  if (['int64','uint64'].includes(tag.type) && typeof value === 'string' && /^(0|-?[1-9][0-9]*)$/.test(value)) {
    const n=BigInt(value);
    if (tag.type === 'int64' && n >= -(2n**63n) && n < 2n**63n) return;
    if (tag.type === 'uint64' && n >= 0n && n < 2n**64n) return;
  }
  throw new TypeError(`点位值类型/范围错误：${tag.id}`);
}

export class MockDriver extends EventEmitter {
  #manifest; #tags=new Map(); #groups=[]; #last=new Map();
  #state={link:'down',session:'absent',subscription:'absent'};
  #now=0; #origin; #generation=0; #sessionGeneration=0; #cycle=0;
  constructor(manifest,{wall_origin_ms=Date.now()}={}) {
    super(); this.#manifest=structuredClone(manifest); this.#origin=wall_origin_ms;
    if (!manifest.device_id || !Array.isArray(manifest.groups) || !manifest.groups.length || !finite(wall_origin_ms)) throw new TypeError('无效订阅清单');
    const groupIds=new Set();
    for (const group of this.#manifest.groups) {
      if (!group.id || groupIds.has(group.id) || !Number.isSafeInteger(group.interval_ms) || group.interval_ms<10 || !Array.isArray(group.tags)) throw new TypeError('无效采集组');
      groupIds.add(group.id); this.#groups.push({definition:group,next:0});
      for (const tag of group.tags) {
        if (!tag.id || this.#tags.has(tag.id)) throw new TypeError('重复或空 tag_id');
        checkValue(tag,tag.initial);
        const d=tag.deadband;
        if (!d || !['none','absolute','percent'].includes(d.mode) || !finite(d.value) || d.value<0 || (d.mode==='none' && d.value!==0) || (tag.type!=='number' && d.mode!=='none')) throw new TypeError('无效点位死区');
        if (d.mode==='percent' && (!finite(tag.engineering_low) || !finite(tag.engineering_high) || tag.engineering_high<=tag.engineering_low || d.value>100)) throw new TypeError('百分比死区缺少有效工程量程');
        this.#tags.set(tag.id,{tag,group:group.id,value:tag.initial,quality:{level:'good',reason:'source_good'},source_at_ms:null,server_at_ms:null});
      }
    }
  }
  #transition(patch) { Object.assign(this.#state,patch); this.emit('state',structuredClone(this.#state)); }
  snapshot() { return {connection:structuredClone(this.#state),manifest:structuredClone(this.#manifest),generation:this.#generation}; }
  restoreSession() {
    if (this.#state.session==='active') return;
    this.#transition({link:'connecting',session:'absent',subscription:'absent'});
    this.#transition({link:'up',session:'creating'});
    this.#sessionGeneration++;
    this.#transition({session:'active',subscription:'stale'});
  }
  connect() { this.restoreSession(); if(this.#state.subscription!=='healthy') this.rebuildSubscriptions(); }
  rebuildSubscriptions() {
    if(this.#state.link!=='up' || this.#state.session!=='active') throw new Error('必须先建立链路与会话');
    this.#transition({subscription:'rebuilding'});
    this.#generation++; this.#last.clear();
    for(const group of this.#groups) group.next=this.#now+group.definition.interval_ms;
    this.#transition({subscription:'healthy'});
  }
  #invalidate(reason) {
    for(const item of this.#tags.values()) this.#publish(item,{level:'bad',reason},true);
    this.#last.clear();
  }
  disconnect() {
    if(this.#state.link==='down') return;
    this.#transition({link:'down',session:'absent',subscription:'absent'});
    this.#invalidate('disconnected');
  }
  loseSubscriptions() {
    if(this.#state.session!=='active') throw new Error('没有活动会话');
    this.#transition({subscription:'stale'}); this.#invalidate('stale');
  }
  setValue(id,value,{quality={level:'good',reason:'source_good'},source_at_ms=null,server_at_ms=null}={}) {
    const item=this.#tags.get(id); if(!item) throw new Error(`未知点位：${id}`);
    checkValue(item.tag,value);
    if(!['good','uncertain','bad'].includes(quality.level) || !quality.reason || [source_at_ms,server_at_ms].some(n=>n!==null&&!finite(n))) throw new TypeError('无效质量/时间戳');
    Object.assign(item,{value,quality:structuredClone(quality),source_at_ms,server_at_ms});
  }
  #publish(item,quality=item.quality,synthetic=false) {
    const previous=this.#last.get(item.tag.id);
    if(previous && JSON.stringify(previous.quality)===JSON.stringify(quality)) {
      let same=previous.value===item.value;
      const d=item.tag.deadband;
      if(item.tag.type==='number' && d.mode!=='none') {
        const threshold=d.mode==='absolute'?d.value:(item.tag.engineering_high-item.tag.engineering_low)*d.value/100;
        same=same || Math.abs(item.value-previous.value)<threshold;
      }
      if(same) return;
    }
    const source=synthetic?null:item.source_at_ms;
    const server=synthetic?null:item.server_at_ms;
    const observed=this.#origin+this.#now;
    const sample={tag_id:item.tag.id,device_id:this.#manifest.device_id,group_id:item.group,
      value:{type:item.tag.type,value:item.value},quality:structuredClone(quality),
      source_at_ms:source,server_at_ms:server,observed_at_ms:observed,
      event_at_ms:source??server??observed,timestamp_basis:source!==null?'source':server!==null?'server':'observed',
      monotonic_ns:String(BigInt(this.#now)*1000000n),scan_cycle:String(++this.#cycle),session_id:`mock-session-${this.#sessionGeneration}`};
    this.emit('sample',sample); // 消费方失败必须向调用者抛出，不能静默丢失。
    this.#last.set(item.tag.id,{value:item.value,quality:structuredClone(quality)});
  }
  advance(ms) {
    if(!Number.isSafeInteger(ms)||ms<0||!Number.isSafeInteger(this.#now+ms)) throw new TypeError('无效 mock 时间步长');
    const target=this.#now+ms;
    if(this.#state.subscription!=='healthy') {this.#now=target;return;}
    while(true) {
      const next=Math.min(...this.#groups.map(g=>g.next)); if(next>target) break;
      this.#now=next;
      for(const group of this.#groups.filter(g=>g.next===next)) {
        for(const tag of group.definition.tags) this.#publish(this.#tags.get(tag.id));
        group.next+=group.definition.interval_ms;
      }
    }
    this.#now=target;
  }
}
