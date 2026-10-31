"""Actual SQLite ENOSPC on the owned bounded virtual volume; recover without restart."""
import errno
import json
import os
from pathlib import Path
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.request
from supplement_helpers import CustomerRun
root=Path(__file__).resolve().parents[3];report=root/'docs/qt-customer-regression-2026-10-23'
mount=Path('/private/tmp/boer-qt-customer/nonwindows-full-volume')
assert os.path.ismount(mount) and os.stat(mount).st_dev!=os.stat(mount.parent).st_dev
assert os.statvfs(mount).f_blocks*os.statvfs(mount).f_frsize<64*1024*1024
folder=Path(tempfile.mkdtemp(prefix='db-full-owned-',dir=mount));proc=None;failed=False
os.environ['BOER_CUSTOMER_ROW_LOG']=str(report/'row-results.jsonl')
try:
 reservations=[socket.socket(),socket.socket()]
 for s in reservations:s.bind(('127.0.0.1',0))
 port,tcp=[s.getsockname()[1] for s in reservations]
 for s in reservations:s.close()
 with (report/'database-full-server.log').open('w') as log:
  proc=subprocess.Popen(['/private/tmp/boer-qt-customer/server/boer-qt-server.app/Contents/MacOS/boer-qt-server','--headless','--db',str(folder/'db.sqlite'),'--data-dir',str(folder),'--port',str(port),'--tcp-port',str(tcp)],env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BOERLAN_SKIP_TRIAL='1'),stdout=log,stderr=subprocess.STDOUT)
  for _ in range(100):
   if proc.poll() is not None:raise RuntimeError('test server exited '+str(proc.returncode))
   try:urllib.request.urlopen(f'http://127.0.0.1:{port}/api/healthz',timeout=1).close();break
   except OSError:time.sleep(.1)
  else:raise RuntimeError('readiness timeout')
  r=CustomerRun(str(port),'非Windows补测：专用32MB虚拟卷数据库实际ENOSPC，Qt服务端SQLite写入/恢复，非真实硬盘损坏')
  original=r.data('POST','/device',dict(code='BEFORE-FULL',name='空间不足前原始设备',groupId=1))['id']
  full=False
  with (folder/'filler.bin').open('wb',buffering=0) as out:
   try:
    for i in range(1024):out.write(bytes(65536))
   except OSError as error:
    if error.errno!=errno.ENOSPC:raise
    full=True
  with (folder/'filler.bin').open('ab',buffering=0) as out:
   try:
    for i in range(128):out.write(bytes(4096))
   except OSError as error:
    if error.errno!=errno.ENOSPC:raise
  assert full
  # The same request is retried successfully below: this is not validation failure.
  body=dict(code='AFTER-FULL',name='磁盘恢复设备',groupId=1,remark='R'*255)
  status,answer=r.call('POST','/device',body)
  r.check(482,'数据库磁盘满时写入明确失败，原设备保留且无半条新记录',status in (400,500) and answer.get('code')!=0 and ('full' in answer.get('message','').lower() or '空间' in answer.get('message','')) and len(r.data('GET','/device/list?fullModel=1'))==1 and r.data('GET',f'/device/{original}')['name']=='空间不足前原始设备',answer)
  (folder/'filler.bin').unlink()
  saved=r.data('POST','/device',body)
  r.check(482,'数据库释放空间后同一有效请求无需重启成功，长备注完整',saved['code']=='AFTER-FULL' and len(saved['remark'])==255 and len(r.data('GET','/device/list?fullModel=1'))==2)
  conn=sqlite3.connect(folder/'db.sqlite');integrity=conn.execute('PRAGMA integrity_check').fetchall();conn.close()
  r.check(482,'数据库磁盘满恢复后SQLite完整性通过',integrity==[('ok',)],integrity)
  failed=bool(r.failures)
except Exception as error:
 print(repr(error),flush=True);failed=True
finally:
 if proc is not None and proc.poll() is None:
  proc.terminate()
  try:proc.wait(timeout=10)
  except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=10)
 shutil.rmtree(folder)
 with (report/'suites.jsonl').open('a') as out:out.write(json.dumps(dict(case='database-full',exit=int(failed),volume=str(mount)))+'\n')
print('database-full',int(failed),flush=True)
sys.exit(int(failed))
