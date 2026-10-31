"""Kill only an owned test server; verify acknowledged SQLite writes after restart."""
import json
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.request
from supplement_helpers import CustomerRun

root=Path(__file__).resolve().parents[3]
report=root/'docs/qt-customer-regression-2026-10-23'
folder=Path(tempfile.mkdtemp(prefix='crash-recovery-',dir='/private/tmp/boer-qt-customer'))
server='/private/tmp/boer-qt-customer/server/boer-qt-server.app/Contents/MacOS/boer-qt-server'
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BOERLAN_SKIP_TRIAL='1',BOER_TEST_SETTINGS_DIR=str(folder/'settings'),BOER_CUSTOMER_ROW_LOG=str(report/'row-results.jsonl'))
os.environ['BOER_CUSTOMER_ROW_LOG']=env['BOER_CUSTOMER_ROW_LOG']
proc=None
failed=False
records=[]
def start(log):
    global proc
    reserved=[socket.socket(),socket.socket()]
    for item in reserved:item.bind(('127.0.0.1',0))
    port,tcp=[item.getsockname()[1] for item in reserved]
    for item in reserved:item.close()
    proc=subprocess.Popen([server,'--headless','--db',str(folder/'app.db'),'--data-dir',str(folder),'--port',str(port),'--tcp-port',str(tcp)],env=env,stdout=log,stderr=subprocess.STDOUT)
    for _ in range(100):
        if proc.poll() is not None:raise RuntimeError('test server exited '+str(proc.returncode))
        try:
            urllib.request.urlopen(f'http://127.0.0.1:{port}/api/healthz',timeout=1).close()
            return CustomerRun(str(port),'异常退出补测：仅自有临时Qt进程SIGKILL和SQLite，非真实断电/磁盘损坏')
        except OSError:time.sleep(.1)
    raise RuntimeError('readiness timeout')
try:
    with (report/'crash-recovery.log').open('w') as log:
        for cycle in range(1,4):
            r=start(log)
            group=r.data('POST','/group',dict(name=f'恢复分组{cycle}',parentId=1))['id']
            device=r.data('POST','/device',dict(code=f'CRASH{cycle}',name=f'恢复设备{cycle}',groupId=group))['id']
            employee=r.data('POST','/employee',dict(code=f'000{cycle}',name=f'恢复员工{cycle}',phone=f'1380013800{cycle}',groupId=group))['id']
            records.append((group,device,employee,cycle))
            # Kill after successful HTTP responses, without graceful database shutdown.
            old_token=r.token
            proc.kill();proc.wait(timeout=10);proc=None
            db=sqlite3.connect(folder/'app.db')
            integrity=db.execute('PRAGMA integrity_check').fetchall()
            db.close()
            r.check(482,f'异常退出第{cycle}轮：SQLite完整性检查通过',integrity==[('ok',)],integrity)
            failed|=bool(r.failures)
            r=start(log)
            new_token=r.token;r.token=old_token
            r.check(482,f'异常退出第{cycle}轮：重启后未过期旧会话仍可读取原业务数据',r.call('GET','/device/list')[0]==200)
            r.token=new_token
            okay=True
            for g,d,e,n in records:
                okay &= r.data('GET',f'/group/{g}')['name']==f'恢复分组{n}'
                row=r.data('GET',f'/device/{d}');okay &= row['groupId']==g and row['code']==f'CRASH{n}'
                row=r.data('GET',f'/employee/{e}');okay &= row['groupId']==g and row['code']==f'000{n}'
            r.check(482,f'异常退出第{cycle}轮：重启重新登录，已确认写入及关联前导零完整保留',okay)
            failed|=bool(r.failures)
            proc.terminate();proc.wait(timeout=10);proc=None
except Exception as error:
    print(repr(error),flush=True);failed=True
finally:
    if proc is not None and proc.poll() is None:
        proc.kill();proc.wait(timeout=10)
    with (report/'suites.jsonl').open('a') as out:
        out.write(json.dumps(dict(case='crash-recovery',exit=int(failed),testDirectory=str(folder)))+'\n')
print('crash-recovery',int(failed),flush=True)
sys.exit(int(failed))
