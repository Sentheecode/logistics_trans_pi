export const utcUs=us=>{
  const ms=Math.floor(us/1000),remainder=us-ms*1000;
  return new Date(ms).toISOString().slice(0,-1)+String(remainder).padStart(3,'0')+'Z';
};
export function coverageFor(from,to,spans,gaps) {
  const cuts=new Set([from,to]);
  for(const x of [...spans,...gaps]){cuts.add(Math.max(from,Number(x.from_us)));cuts.add(Math.min(to,Number(x.to_us)));}
  const sorted=[...cuts].filter(x=>x>=from&&x<=to).sort((a,b)=>a-b),covered=[],missing=[];
  const add=(list,a,b,reason)=>{const last=list.at(-1);if(last&&last.b===a&&last.reason===reason)last.b=b;else list.push({a,b,reason});};
  for(let i=0;i<sorted.length-1;i++) {
    const a=sorted[i],b=sorted[i+1];if(a===b)continue;
    const scope=spans.filter(s=>Number(s.from_us)<=a&&Number(s.to_us)>=b);
    const gap=gaps.find(g=>Number(g.from_us)<=a&&Number(g.to_us)>=b);
    const reason=gap?.reason??(scope.some(s=>s.status==='unknown')?'unknown':scope.some(s=>s.status==='rotated')?'rotated':scope.some(s=>s.status==='recorded')?null:'not_recorded');
    if(reason)add(missing,a,b,reason);else add(covered,a,b,null);
  }
  return {requested:{from:utcUs(from),to:utcUs(to)},
    time_window_actually_covered:covered.map(x=>({from:utcUs(x.a),to:utcUs(x.b)})),
    gaps:missing.map(x=>({window:{from:utcUs(x.a),to:utcUs(x.b)},reason:x.reason})),
    status:missing.length===0?'complete':covered.length?'partial':missing.some(x=>['unknown','clock_discontinuity'].includes(x.reason))?'unknown':'none'};
}
