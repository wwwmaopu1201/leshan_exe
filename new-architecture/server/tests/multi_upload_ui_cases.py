"""Two real sequential uploads expose aggregate 25%, 75%, 100% in the actual UI."""
import os
import socket
import struct
import subprocess
import sys
import threading
import time
ready=threading.Event();stop=threading.Event();errors=[];received=[]
def frame(kind,no,payload=b'',total=1,index=1):
 raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,no,total,index,len(payload)+2)+payload;crc=65535
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
def ack(peer):
 while True:
  packet=read(peer);kind,no=struct.unpack('>HH',packet[13:17])
  if kind==0xb2a and no==0xd:return
  if kind==0xb29:peer.sendall(frame(kind,0x32,b'\0'))
def device():
 try:
  with socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=5) as peer:
   read(peer);peer.sendall(frame(0xb2a,2));read(peer);peer.sendall(frame(0x1302,0x10fa,struct.pack('>II',1,88555)+'多任务回传设备'.encode()));peer.sendall(frame(0xb29,0x32,b'\0'));time.sleep(.15);ready.set();peer.settimeout(.5)
   while not stop.is_set():
    try:packet=read(peer)
    except socket.timeout:continue
    kind,no=struct.unpack('>HH',packet[13:17])
    if kind==0xb2a and no==9:peer.sendall(frame(kind,no,b''.join(struct.pack('>H',77+n)+name.encode('utf-16le').ljust(44,b'\0') for n,name in enumerate(['设备花型A.NTP','设备花型B.NTP']))))
    elif kind==0xb2a and no==7:
     number=struct.unpack('>H',packet[23:25])[0];received.append(number);body=(b'A' if number==77 else b'B')*1200;peer.sendall(frame(kind,no,b'\0'));peer.sendall(frame(0xb2a,0xd,body[:600],2,1));ack(peer);time.sleep(2.2);peer.sendall(frame(0xb2a,0xd,body[600:],2,2));ack(peer);peer.sendall(frame(kind,no,b'\2'))
    elif kind==0xb29:peer.sendall(frame(kind,0x32,b'\0'))
 except Exception as error:
  if not stop.is_set():errors.append(repr(error))
  ready.set()
worker=threading.Thread(target=device,daemon=True);worker.start()
if not ready.wait(5) or errors:raise RuntimeError(errors or 'DT peer readiness timeout')
client='/private/tmp/boer-qt-customer/client/boer-qt-client.app/Contents/MacOS/boer-qt-client'
try:result=subprocess.run([client,'--pattern-regression-test','--server-host','127.0.0.1','--server-port',sys.argv[1]],env=dict(os.environ,BOER_TEST_MULTI_UPLOAD='1'),timeout=60)
finally:stop.set();worker.join(timeout=2)
print('received file numbers:',received,'errors:',errors,flush=True)
if result.returncode==0:assert sorted(received)==[77,78] and not errors
sys.exit(result.returncode if result.returncode else int(bool(errors)))
