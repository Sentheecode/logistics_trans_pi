import{exerciseCrash}from'../runtime/test/crash-helper.mjs';
if(process.platform!=='linux')throw new Error('本验证器当前仅验收Linux；不能据此报告Windows已通过');
const results=[];for(const mode of ['pending','after-fsync','partial','duplicate','deleting','watchdog'])results.push(await exerciseCrash(mode,{keep:true}));
console.log(JSON.stringify({node:process.version,platform:process.platform,results,
  limits:['真实杀死测试子进程，不是物理断电','测试只使用新临时目录','未验证Windows磁盘缓存或测试工位改时间']},null,2));
