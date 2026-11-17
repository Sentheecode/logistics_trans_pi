// 仅为 T3 合成场景；不代表 140 工位、现场点位编码或真实报警阈值。
export const manifest = () => ({device_id:'mock-only',groups:[
  {id:'fast',interval_ms:100,tags:[
    {id:'temperature',type:'number',initial:5,deadband:{mode:'absolute',value:1}},
    {id:'heartbeat',type:'boolean',initial:false,deadband:{mode:'none',value:0}},
  ]},
  {id:'slow',interval_ms:1000,tags:[{id:'status',type:'string',initial:'就绪',deadband:{mode:'none',value:0}}]},
]});
export const rules = () => [{id:'mock-high',tag_id:'temperature',kind:'high',trigger:10,clear:8}];
