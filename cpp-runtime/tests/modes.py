"""Synthetic local mailbox tests; no network, arbitrary SQL input or device access."""
from contextlib import closing
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import time
import os

collector, client, diagnose = sys.argv[1:]
def run(*args, ok=True):
    p = subprocess.run(args, capture_output=True, text=True, timeout=8)
    assert (p.returncode == 0) == ok, (args, p.stdout, p.stderr)
    return p

def snapshot(d):
    with closing(sqlite3.connect((d / 'simulation.sqlite').resolve().as_uri()+'?mode=ro', uri=True)) as db:
        return db.execute('select mode,revision,heartbeat_ms,boot from runtime_state').fetchone(), db.execute('select count(*),max(seq) from frames').fetchone()

def wait(predicate):
    end=time.monotonic()+6
    while time.monotonic()<end:
        try:
            if predicate():return
        except (sqlite3.Error,FileNotFoundError):pass
        time.sleep(.03)
    raise AssertionError('timed out')

def put(d, text):
    # Lock the same byte on Windows / same file on POSIX, exactly as the channel.
    with open(d/'control.mailbox','r+b') as f:
        if os.name=='nt':
            import msvcrt
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(f,fcntl.LOCK_EX)
        f.seek(0);f.write(text.encode());f.truncate();f.flush()
        if os.name=='nt':f.seek(0);msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)

with tempfile.TemporaryDirectory(prefix='cpp-modes-') as tmp:
    d=pathlib.Path(tmp)/'data'
    p=subprocess.Popen([collector,str(d)],stdout=subprocess.DEVNULL)
    try:
        wait(lambda:snapshot(d)[1][0]>=2)
        run(client,str(d),'step',ok=False)
        run(client,str(d),'manual')
        state,n=snapshot(d)
        time.sleep(3.2)
        state2,n2=snapshot(d)
        assert n2==n and state2[0]=='manual' and state2[2]>state[2]
        output=run(diagnose,str(d),'status').stdout
        assert 'freshness=stale' in output and 'process=recent' in output and 'mode=manual' in output
        run(client,str(d),'step')
        assert snapshot(d)[1][0]==n[0]+1
        # Duplicate revision must never advance twice, even with a different request ID.
        state,n=snapshot(d)
        put(d,f'Q abc {state[3]} {state[1]-1} {int(time.time()*1000)+1200} step')
        wait(lambda:'stale_revision' in (d/'control.mailbox').read_text())
        assert snapshot(d)[1]==n
        put(d,f'Q abd {state[3]} {state[1]} {int(time.time()*1000)-1} step')
        wait(lambda:'expired' in (d/'control.mailbox').read_text())
        assert snapshot(d)[1]==n
        put(d,f'Q abe deadbeef {state[1]} {int(time.time()*1000)+1200} step')
        wait(lambda:'session_changed' in (d/'control.mailbox').read_text())
        assert snapshot(d)[1]==n
        put(d,'X'*1024)
        wait(lambda:len((d/'control.mailbox').read_bytes())<513)
        assert p.poll() is None and snapshot(d)[1]==n
        put(d,'R malformed')
        wait(lambda:(d/'control.mailbox').read_text()=='')
        run(client,str(d),'disconnect')
        with closing(sqlite3.connect(d/'simulation.sqlite')) as db:
            assert db.execute('select cart1,quality,link from frames order by seq desc limit 1').fetchone()==(None,'bad','down')
        run(client,str(d),'step')
        # Contending clients are serialized/rejected; each successful request advances once.
        n=snapshot(d)[1][0]
        clients=[subprocess.Popen([client,str(d),'step'],stdout=subprocess.PIPE,stderr=subprocess.PIPE) for _ in range(8)]
        outcomes=[q.communicate(timeout=8) for q in clients]
        successes=sum(q.returncode==0 for q in clients)
        assert snapshot(d)[1][0]==n+successes
        # Collector restart preserves manual mode and committed history; no implicit step.
        old=snapshot(d)
        p.terminate();p.wait(timeout=5)
        p=subprocess.Popen([collector,str(d)],stdout=subprocess.DEVNULL)
        wait(lambda:snapshot(d)[0][3]!=old[0][3])
        time.sleep(.7)
        assert snapshot(d)[0][0]=='manual' and snapshot(d)[1]==old[1]
        run(client,str(d),'automatic_confirmed')
        wait(lambda:snapshot(d)[1][0]>old[1][0]+1)
        assert snapshot(d)[0][0]=='automatic'
        p.terminate();p.wait(timeout=5)
        run(client,str(d),'manual',ok=False)  # fresh-looking DB, absent live endpoint: bounded timeout
        time.sleep(1.6)
        run(client,str(d),'step',ok=False)
    finally:
        if p.poll() is None:p.terminate();p.wait(timeout=5)
    run(client,str(pathlib.Path(tmp)/'absent'),'step',ok=False)
    # Synthetic v1 fixture: same historical schema, no runtime_state table.
    before=snapshot(d)[1]
    with closing(sqlite3.connect(d/'simulation.sqlite')) as db:
        db.execute('drop table runtime_state');db.execute('drop table sim_log_retention');db.execute('pragma user_version=1');db.commit()
    run(collector,str(d),'1','20')
    assert snapshot(d)[1][0]==before[0]+1
    assert snapshot(d)[0][0]=='automatic'

print('PASS: automatic/manual, idle heartbeat, exactly-one-step, revision/expiry/session fencing, contention, restart, no collector failure')
