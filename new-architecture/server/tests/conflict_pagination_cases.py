"""Forty distinct device file IDs with identical names; actual conflict dialog paging."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time
ready=threading.Event();stop=threading.Event();errors=[]
def frame(kind,number,payload=b''):
 raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,number,1,1,len(payload)+2)+payload;crc=65535
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
 header=exact(peer,23);return header+exact(peer,struct.unpack('>I',header[19:23])[0])
def device():
 try:
  with socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=5) as peer:
   read(peer);peer.sendall(frame(0xb2a,2));read(peer);peer.sendall(frame(0x1302,0x10fa,struct.pack('>II',1,88555)+'多同名文件设备'.encode()));peer.sendall(frame(0xb29,0x32,b'\0'));time.sleep(.15);ready.set();peer.settimeout(.5)
   while not stop.is_set():
    try:packet=read(peer)
    except socket.timeout:continue
    kind,number=struct.unpack('>HH',packet[13:17])
    if kind==0xb2a and number==9:
     payload=b''.join(struct.pack('>H',77+n)+'设备花型A.NTP'.encode('utf-16le').ljust(44,b'\0') for n in range(40));peer.sendall(frame(kind,number,payload))
    elif kind==0xb2a and number==7:peer.sendall(frame(kind,number,b'\1'))
    elif kind==0xb29:peer.sendall(frame(kind,0x32,b'\0'))
 except Exception as error:
  if not stop.is_set():errors.append(repr(error))
  ready.set()
worker=threading.Thread(target=device,daemon=True);worker.start()
if not ready.wait(5) or errors:raise RuntimeError(errors or 'DT peer readiness timeout')
client='/private/tmp/boer-qt-customer/client/boer-qt-client.app/Contents/MacOS/boer-qt-client'
try:
 result=subprocess.run([client,'--pattern-regression-test','--server-host','127.0.0.1','--server-port',sys.argv[1]],env=dict(os.environ,BOER_TEST_MANY_CONFLICTS='1'),timeout=60)
finally:stop.set();worker.join(timeout=2)
print('DT peer errors:',errors,flush=True)
sys.exit(result.returncode if result.returncode else 1 if errors else 0)
