"""Run the native Qt pattern UI against a real Qt server and a controlled DT peer."""
import sqlite3
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time

ready=threading.Event();stop=threading.Event();errors=[]
fixture=Path(__file__).resolve().parents[3]/'boer-lan-server/backend/uploads/patterns/20260505145113.101_波浪线.ntp'
content=fixture.read_bytes() if fixture.exists() else bytes(range(256))*30
print('DT payload fixture:',str(fixture) if fixture.exists() else 'generated binary fixture','bytes=',len(content),flush=True)


def frame(kind,number,payload=b'',total=1,index=1):
    raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,number,total,index,len(payload)+2)+payload
    crc=0xffff
    for byte in raw:
        crc^=byte
        for _ in range(8):crc=(crc>>1)^(0xA001 if crc&1 else 0)
    return raw+struct.pack('>H',crc)


def exact(peer,count):
    raw=b''
    while len(raw)<count:
        chunk=peer.recv(count-len(raw))
        if not chunk:raise EOFError('peer closed')
        raw+=chunk
    return raw


def read(peer):
    header=exact(peer,23);return header+exact(peer,struct.unpack('>I',header[19:23])[0])


def device():
    try:
        with socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=5) as peer:
            read(peer);peer.sendall(frame(0x0B2A,2));read(peer)
            peer.sendall(frame(0x1302,0x10FA,struct.pack('>II',1,88555)+'界面回传设备'.encode()))
            peer.sendall(frame(0x0B29,0x0032,b'\x00'));time.sleep(.15);ready.set()
            peer.settimeout(1)
            while not stop.is_set():
                try:packet=read(peer)
                except socket.timeout:continue
                kind,number=struct.unpack('>HH',packet[13:17])
                if kind==0x0B2A and number==9:
                    payload=b''.join(struct.pack('>H',n)+name.encode('utf-16le').ljust(44,b'\0') for n,name in [(77,'设备花型A.NTP'),(78,'设备花型B.NTP')])
                    peer.sendall(frame(0x0B2A,9,payload))
                elif kind==0x0B2A and number==7:
                    if struct.unpack('>H',packet[23:25])[0]==78:
                        peer.sendall(frame(0x0B2A,7,b'\x01'));time.sleep(.1);return
                    peer.sendall(frame(0x0B2A,7,b'\x00'))
                    peer.sendall(frame(0x0B2A,0x000D,content[:300],2,1))
                    assert read(peer)[13:17]==struct.pack('>HH',0x0B2A,0x000D)
                    time.sleep(2.2)
                    peer.sendall(frame(0x0B2A,0x000D,content[300:],2,2))
                    assert read(peer)[13:17]==struct.pack('>HH',0x0B2A,0x000D)
                    peer.sendall(frame(0x0B2A,7,b'\x02'))
                elif kind==0x0B29:
                    peer.sendall(frame(0x0B29,0x0032,b'\x00'))
    except Exception as error:
        if not stop.is_set():errors.append(repr(error))
        ready.set()


worker=threading.Thread(target=device,daemon=True);worker.start()
if not ready.wait(5) or errors:raise RuntimeError(errors or 'DT peer readiness timeout')
# Deterministic terminal history fixture for translation. Fresh tasks no longer
# expire from the old UTC/local-time bug; DELETE removes tasks, it does not log cancellation.
db=sqlite3.connect(sys.argv[3]);db.execute("INSERT INTO download_tasks(status,message) VALUES('failed','设备响应超时')");db.commit();db.close()
client='/private/tmp/boer-qt-customer/client/boer-qt-client.app/Contents/MacOS/boer-qt-client'
try:
    result=subprocess.run([client,'--pattern-regression-test','--server-host','127.0.0.1','--server-port',sys.argv[1]],
                          env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BOER_PATTERN_FIXTURE=str(fixture) if fixture.exists() else ''),timeout=60)
finally:
    stop.set();worker.join(timeout=2)
print('DT peer errors:',errors,flush=True)
sys.exit(1 if errors or result.returncode else 0)
