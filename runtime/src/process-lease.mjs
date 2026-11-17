import { DatabaseSync } from 'node:sqlite';
import { randomUUID } from 'node:crypto';
import { existsSync, readFileSync, openSync, closeSync, fsyncSync, renameSync, unlinkSync } from 'node:fs';
import { join } from 'node:path';
import { syncDirectory, writeAll } from './file-durability.mjs';

export class ProcessLease {
  #db; #directory; #nonce=randomUUID(); #owned=false;
  constructor(directory) {
    this.#directory=directory;
    const marker=join(directory,'runtime.lock');
    if(existsSync(marker)) {
      let metadata;try{metadata=JSON.parse(readFileSync(marker,'utf8'));}catch{}
      if(metadata?.protocol!=='sqlite-exclusive-v1') throw new Error('发现旧格式/损坏的锁记录，需要停机核对，不能猜测删除');
    }
    let temp;
    try {
      this.#db=new DatabaseSync(join(directory,'lease.sqlite'));
      this.#db.exec('PRAGMA busy_timeout=0; CREATE TABLE IF NOT EXISTS lease_version(version INTEGER NOT NULL); BEGIN EXCLUSIVE;');
      temp=join(directory,`.lease-${this.#nonce}.tmp`);
      const fd=openSync(temp,'wx',0o600);
      try {writeAll(fd,Buffer.from(JSON.stringify({protocol:'sqlite-exclusive-v1',pid:process.pid,nonce:this.#nonce})+'\n'));fsyncSync(fd);}
      finally{closeSync(fd);}
      renameSync(temp,marker);temp=null;syncDirectory(directory);this.#owned=true;
    } catch(error) {if(temp&&existsSync(temp))unlinkSync(temp);if(this.#db)this.#db.close();throw error;}
  }
  close() {
    if(!this.#db)return;
    try {
      if(this.#owned){unlinkSync(join(this.#directory,'runtime.lock'));syncDirectory(this.#directory);this.#owned=false;}
    } finally {try{this.#db.close();}finally{this.#db=null;}}
  }
}
