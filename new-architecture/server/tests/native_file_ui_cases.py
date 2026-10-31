"""Native server UI against two real DT peers: deletion and forty-name conflicts."""
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
root=Path(__file__).resolve().parents[3];report=root/'docs/qt-customer-regression-2026-10-23';test=Path(tempfile.mkdtemp(prefix='native-file-ui-',dir='/private/tmp/boer-qt-customer'));portfile=test/'port';done=test/'done';stop=threading.Event();errors=[];deleted=[];refused=[]
files=[(77,'设备花型A.NTP'),(78,'设备花型B.NTP'),(79,'设备花型C.NTP'),(80,'设备花型D.NTP'),(88,'同名.NTP'),(89,'同名.NTP')]
def frame(kind,no,payload=b''):
 raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,no,1,1,len(payload)+2)+payload;crc=65535
 for byte in raw:
  crc^=byte
  for _ in range(8):crc=(crc>>1)^(0xa001 if crc&1 else 0)
 return raw+struct.pack('>H',crc)
def exact(peer,n):
 raw=b''
 while len(raw)<n:
  value=peer.recv(n-len(raw))
  if not value:raise EOFError()
  raw+=value
 return raw
def read(peer):
 head=exact(peer,23);return head+exact(peer,struct.unpack('>I',head[19:23])[0])
def device(port,code):
 try:
  with socket.create_connection(('127.0.0.1',port),timeout=5) as peer:
   read(peer);peer.sendall(frame(0xb2a,2));read(peer);peer.sendall(frame(0x1302,0x10fa,struct.pack('>II',1,code)+('原生文件设备'+str(code)).encode()));peer.sendall(frame(0xb29,0x32,b'\0'));peer.settimeout(.3)
   while not stop.is_set() and not done.exists():
    try:packet=read(peer)
    except socket.timeout:continue
    kind,no=struct.unpack('>HH',packet[13:17]);payload=packet[23:-2]
    if kind==0xb2a and no==9:
     rows=files if code==777001 else [(200+n,'设备花型A.NTP') for n in range(40)]
     peer.sendall(frame(kind,no,b''.join(struct.pack('>H',n)+name.encode('utf-16le').ljust(44,b'\0') for n,name in rows)))
    elif kind==0xb2a and no==8:
     assert code==777001,'unexpected deletion on conflict peer';matches=[(n,name) for n,name in files if name.encode('utf-16le') in payload or name.encode() in payload];assert matches,('unrecognized delete payload',payload.hex());n,name=matches[0];assert name!='同名.NTP','ambiguous deletion reached peer'
     if name=='设备花型C.NTP':refused.append(n);time.sleep(.12);peer.sendall(frame(kind,no,b'\1'))
     else:time.sleep(.6);files.remove((n,name));deleted.append(n);peer.sendall(frame(kind,no,b'\0'))
    elif kind==0xb2a and no==7:peer.sendall(frame(kind,no,b'\1'))
    elif kind==0xb29:peer.sendall(frame(kind,0x32,b'\0'))
 except Exception as error:
  if not stop.is_set() and not done.exists():errors.append(repr(error))
server='/private/tmp/boer-qt-customer/server/boer-qt-server.app/Contents/MacOS/boer-qt-server';env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BOERLAN_SKIP_TRIAL='1',BOER_TEST_SETTINGS_DIR=str(test/'settings'),BOER_TEST_NATIVE_DEVICE='1',BOER_TEST_NATIVE_FILE_IO='1',BOER_TEST_NATIVE_FILE_PORT=str(portfile),BOER_TEST_NATIVE_FILE_DONE=str(done),BOER_CUSTOMER_ROW_LOG=str(report/'row-results.jsonl'))
threads=[];result=99;runtime_error=None
with (report/'native-file-ui.log').open('w') as log:
 proc=subprocess.Popen([server,'--customer-ui-regression'],env=env,stdout=log,stderr=subprocess.STDOUT)
 try:
  deadline=time.monotonic()+30
  while not portfile.exists():
   if proc.poll() is not None:raise RuntimeError('GUI exited before file test: '+str(proc.returncode))
   if time.monotonic()>deadline:raise TimeoutError('GUI file-test port unavailable')
   time.sleep(.05)
  port=int(portfile.read_text())
  for code in (777001,777002):
   worker=threading.Thread(target=device,args=(port,code),daemon=True);threads.append(worker);worker.start()
  result=proc.wait(timeout=60)
 except Exception as error:
  runtime_error=repr(error);print('Native file test runtime error:',runtime_error,flush=True)
 finally:
  stop.set()
  if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
  for worker in threads:worker.join(timeout=2)
print('exit',result,'deleted',deleted,'refused',refused,'remaining',files,'peer errors',errors,flush=True)
passed=result==0 and sorted(deleted)==[77,78,80] and set(refused)=={79} and len(files)==3 and not errors
if passed:
 with (report/'row-results.jsonl').open('a') as out:out.write(json.dumps(dict(row=352,name='活动设备连接及上传任务存在时服务端窗口正常关闭，不崩溃',result='PASS',actual=dict(exit=result,wireDeleted=deleted),scope='实际Qt服务端GUI退出；两条DT连接、40项上传任务；已校验进程退出码0'),ensure_ascii=False)+'\n')
with (report/'suites.jsonl').open('a') as out:out.write(json.dumps(dict(case='native-file-ui',exit=int(not passed),testDirectory=str(test),wireDeleted=deleted,wireRefused=refused,errors=errors,error=runtime_error),ensure_ascii=False)+'\n')
sys.exit(int(not passed))
