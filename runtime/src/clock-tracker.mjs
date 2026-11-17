/** 时钟观测器：preview 不改状态；调用者在持久化成功后 commit。 */
export function toMicroseconds(ms) {
  if(ms===null) return null;
  if(typeof ms!=='number' || !Number.isFinite(ms)) throw new TypeError('无效时间戳');
  const us=Math.round(ms*1000);
  if(!Number.isSafeInteger(us)) throw new TypeError('时间戳超出安全范围');
  return us;
}
const signed=s=>typeof s==='string' && /^(0|-?[1-9][0-9]*)$/.test(s);
const unsigned=s=>typeof s==='string' && /^(0|[1-9][0-9]*)$/.test(s);
const basis=b=>['source','server','observed'].includes(b);
const abs=n=>n<0n?-n:n;
function observationValid(o) {
  return o && typeof o.boot_id==='string' && o.boot_id.length>0 && signed(o.observed_us) && unsigned(o.monotonic_ns);
}

export class ClockTracker {
  #state; #threshold;
  constructor({state={version:1,epoch:0,last_observation:null,streams:[]},jump_threshold_ms=1000}={}) {
    if(typeof jump_threshold_ms!=='number' || !Number.isFinite(jump_threshold_ms) || jump_threshold_ms<0) throw new TypeError('无效时钟阈值');
    this.#threshold=BigInt(toMicroseconds(jump_threshold_ms));
    if(state?.version!==1 || !Number.isSafeInteger(state.epoch) || state.epoch<0 || !Array.isArray(state.streams)
      || (state.last_observation!==null&&!observationValid(state.last_observation))) throw new TypeError('无效时钟状态/版本');
    const ids=new Set();
    for(const s of state.streams) {
      if(!s.tag_id || ids.has(s.tag_id) || !basis(s.basis) || !signed(s.event_us) || !signed(s.observed_us)
        || !Number.isSafeInteger(s.epoch) || s.epoch<0 || s.epoch>state.epoch) throw new TypeError('无效点位时钟状态');
      ids.add(s.tag_id);
    }
    this.#state=structuredClone(state);
  }
  state() { return structuredClone(this.#state); }
  preview(sample) {
    if(!sample.tag_id || !basis(sample.timestamp_basis)) throw new TypeError('无效样本时间域');
    const current={boot_id:sample.boot_id,observed_us:String(toMicroseconds(sample.observed_at_ms)),monotonic_ns:sample.monotonic_ns};
    if(!observationValid(current) || BigInt(current.monotonic_ns)>9223372036854775807n) throw new TypeError('无效启动身份/单调时间');
    const event=toMicroseconds(sample.event_at_ms);if(event===null) throw new TypeError('事件时间不能为空');
    const previous=this.#state.last_observation;
    const next=this.state(); const reasons=[];
    if(previous) {
      if(previous.boot_id!==current.boot_id) {reasons.push('boot_boundary');next.streams=[];}
      else {
        const monoDelta=BigInt(current.monotonic_ns)-BigInt(previous.monotonic_ns);
        if(monoDelta<0n) throw new Error('同一 boot 的单调时间不可倒退');
        const wallDelta=BigInt(current.observed_us)-BigInt(previous.observed_us);
        if(wallDelta<0n) reasons.push('wall_backward');
        else if(abs(wallDelta-monoDelta/1000n)>this.#threshold) reasons.push('wall_jump');
      }
    }
    const stream=next.streams.find(s=>s.tag_id===sample.tag_id);
    if(!reasons.length && stream && stream.epoch===next.epoch) {
      if(stream.basis!==sample.timestamp_basis) reasons.push('event_basis_changed');
      else if(sample.timestamp_basis!=='observed') {
        const delta=BigInt(event)-BigInt(stream.event_us);
        const observedDelta=BigInt(current.observed_us)-BigInt(stream.observed_us);
        if(delta<0n) reasons.push('event_backward');
        else if(abs(delta-observedDelta)>this.#threshold) reasons.push('event_time_discontinuity');
      }
    }
    if(reasons.length) {
      if(next.epoch===Number.MAX_SAFE_INTEGER) throw new Error('clock_epoch 已耗尽');
      next.epoch++;
    }
    next.last_observation=current;
    next.streams=next.streams.filter(s=>s.tag_id!==sample.tag_id);
    next.streams.push({tag_id:sample.tag_id,basis:sample.timestamp_basis,event_us:String(event),observed_us:current.observed_us,epoch:next.epoch});
    return {base:JSON.stringify(this.#state),next,epoch:next.epoch,reasons,
      details:{previous_observation:previous,current_observation:current,previous_stream:stream??null,event_us:String(event)}};
  }
  commit(proposal) {
    if(proposal.base!==JSON.stringify(this.#state)) throw new Error('过期的时钟预计算');
    this.#state=structuredClone(proposal.next);
  }
}
