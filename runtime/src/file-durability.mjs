import { openSync, closeSync, fsyncSync, writeSync } from 'node:fs';
export function writeAll(fd,bytes) {
  for(let offset=0;offset<bytes.length;) {
    const n=writeSync(fd,bytes,offset,bytes.length-offset);
    if(n<=0) throw new Error('文件写入没有进展');offset+=n;
  }
}
export function syncDirectory(directory) {
  let fd;
  try {fd=openSync(directory,'r');fsyncSync(fd);return true;}
  catch(error) {
    if(process.platform==='win32' && ['EPERM','EISDIR','EINVAL','ENOTSUP'].includes(error.code)) return false;
    throw error;
  } finally {if(fd!==undefined)closeSync(fd);}
}
