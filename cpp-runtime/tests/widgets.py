import pathlib
import subprocess
import sys
import tempfile
import time
from contextlib import closing
import sqlite3
collector, widgets, hmi, client = sys.argv[1:]

def run(*args):
    r=subprocess.run(args,capture_output=True,text=True,timeout=45)
    assert r.returncode==0,(r.stdout,r.stderr)
    print(r.stdout)

def state(d):
    with closing(sqlite3.connect((d/'simulation.sqlite').resolve().as_uri()+'?mode=ro',uri=True)) as db:
        return db.execute('select mode from runtime_state').fetchone()[0],db.execute('select max(seq) from frames').fetchone()[0]

with tempfile.TemporaryDirectory(prefix='cpp-widgets-') as tmp:
    run(widgets,str(pathlib.Path(tmp)/'session-evidence'),'evidence')
    d=pathlib.Path(tmp)/'data'
    p=subprocess.Popen([collector,str(d)],stdout=subprocess.DEVNULL)
    try:
        run(widgets,str(d),'online')
        assert p.poll() is None
        before=state(d)[1];time.sleep(.7);assert state(d)[1]>before
        run(client,str(d),'manual')
        before=state(d)
        run(hmi,str(d),'900')
        assert p.poll() is None and state(d)==before
        run(hmi,str(d),'900')
        assert state(d)==before,'HMI reopening changed collector state'
        p.terminate();p.wait(timeout=5)
        time.sleep(1.7)
        run(widgets,str(d),'offline')
        run(widgets,str(pathlib.Path(tmp)/'missing'),'offline')
    finally:
        if p.poll() is None:p.terminate();p.wait(timeout=5)
print('PASS: HMI exit/reopen in both modes preserves independent collector; stale/missing endpoint disables controls')
