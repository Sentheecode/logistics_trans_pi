import test from'node:test';import assert from'node:assert/strict';import{exerciseCrash}from'./crash-helper.mjs';
for(const mode of ['pending','after-fsync','partial','duplicate','deleting','watchdog']) {
  test(`SIGKILL恢复：${mode}`,{skip:process.platform!=='linux',timeout:15000},async()=>{
    const result=await exerciseCrash(mode);assert.equal(result.pending_logs,0);assert.equal(result.integrity_check,'ok');
  });
}
