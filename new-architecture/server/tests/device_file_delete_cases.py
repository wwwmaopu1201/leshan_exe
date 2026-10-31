"""Actual client file deletion with delayed ACK, cancellation, refusal and duplicate-name guard."""
import os
import socket
import struct
import subprocess
import sys
import threading
import time
ready=threading.Event();stop=threading.Event();errors=[]
files=[(77,'设备花型A.NTP'),(78,'设备花型B.NTP'),(79,'设备花型C.NTP'),(80,'设备花型D.NTP'),(88,'同名.NTP'),(89,'同名.NTP')];deleted=[];refused=[]
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
def device():
 try:
  with socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=5) as peer:
   read(peer);peer.sendall(frame(0xb2a,2));read(peer);peer.sendall(frame(0x1302,0x10fa,struct.pack('>II',1,88555)+'删除回归设备'.encode()));peer.sendall(frame(0xb29,0x32,b'\0'));time.sleep(.15);ready.set();peer.settimeout(.5)
   while not stop.is_set():
    try:packet=read(peer)
    except socket.timeout:continue
    kind,no=struct.unpack('>HH',packet[13:17]);payload=packet[23:-2]
    if kind==0xb2a and no==9:peer.sendall(frame(kind,no,b''.join(struct.pack('>H',n)+name.encode('utf-16le').ljust(44,b'\0') for n,name in files)))
    elif kind==0xb2a and no==8:
     matches=[(n,name) for n,name in files if name.encode('utf-16le') in payload or name.encode() in payload]
     assert matches,('unrecognized deletion payload',payload.hex())
     n,name=matches[0];assert name!='同名.NTP','ambiguous duplicate-name deletion reached the device'
     if name=='设备花型C.NTP':refused.append(n);time.sleep(.12);peer.sendall(frame(kind,no,b'\1'))
     else:time.sleep(.6);files.remove((n,name));deleted.append(n);peer.sendall(frame(kind,no,b'\0'))
    elif kind==0xb29:peer.sendall(frame(kind,0x32,b'\0'))
 except Exception as error:
  if not stop.is_set():errors.append(repr(error))
  ready.set()
worker=threading.Thread(target=device,daemon=True);worker.start()
if not ready.wait(5) or errors:raise RuntimeError(errors or 'DT peer readiness timeout')
client='/private/tmp/boer-qt-customer/client/boer-qt-client.app/Contents/MacOS/boer-qt-client'
try:result=subprocess.run([client,'--pattern-regression-test','--server-host','127.0.0.1','--server-port',sys.argv[1]],env=dict(os.environ,BOER_TEST_DEVICE_DELETE='1'),timeout=60)
finally:stop.set();worker.join(timeout=2)
print('wire deleted:',deleted,'refused:',refused,'remaining:',files,'errors:',errors,flush=True)
if result.returncode==0:assert sorted(deleted)==[77,78,80] and set(refused)=={79} and len(files)==3 and not errors
sys.exit(result.returncode if result.returncode else 1 if errors else 0)
