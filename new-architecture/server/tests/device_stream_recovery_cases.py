"""Real DT TCP stream fragmentation, noise/CRC recovery and repeated reconnects."""
import socket
import struct
import sys
import time
from supplement_helpers import CustomerRun, wait_for

r=CustomerRun(sys.argv[1], '补测：真实Qt服务端TCP流；刻意拆包/合包/坏CRC/非法长度/同编号重连，非客户实机')
def frame(kind,no,payload=b''):
    raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,no,1,1,len(payload)+2)+payload
    crc=65535
    for byte in raw:
        crc^=byte
        for _ in range(8):crc=(crc>>1)^(0xa001 if crc&1 else 0)
    return raw+struct.pack('>H',crc)
def exact(peer,n):
    data=b''
    while len(data)<n:
        part=peer.recv(n-len(data))
        if not part:raise EOFError()
        data+=part
    return data
def read(peer):
    head=exact(peer,23);return head+exact(peer,struct.unpack('>I',head[19:23])[0])
def handshake():
    peer=socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=4)
    read(peer);peer.sendall(frame(0xb2a,2));read(peer)
    return peer
def devices():return r.data('GET','/device/list?pageSize=1000')['list']
def known():return next((x for x in devices() if x['code']=='778811'),{})
peer=handshake()
raw=frame(0x1302,0x10fa,struct.pack('>II',1,778811)+'拆包中文设备'.encode())
for part in (raw[:1],raw[1:2],raw[2:18],raw[18:23],raw[23:]):peer.sendall(part);time.sleep(.03)
r.check(76,'补测中文识别帧拆成五段后完整解析，不产生乱码',bool(wait_for(lambda:known().get('name')=='拆包中文设备')),known())
identifier=known().get('id')
speed=lambda n:frame(0x1302,0x107c,struct.pack('>H',n))
bad=bytearray(speed(999));bad[-1]^=0xff
peer.sendall(bytes(bad)+speed(2100)+frame(0xb01,0x1f,struct.pack('>I',444)))
r.check(467,'补测坏CRC帧丢弃，紧随粘包的2100RPM与444针正常解析',bool(wait_for(lambda:known().get('currentSpeed')==2100 and known().get('needleCountValue')==444)),known())
invalid=b'DT'+bytes(17)+struct.pack('>I',999999)+bytes(2)
peer.sendall(b'garbage-with-D'+invalid+speed(2200))
r.check(467,'补测噪声及超长非法帧头后恢复后续2200RPM',bool(wait_for(lambda:known().get('currentSpeed')==2200)),known())
peer.close();r.check(191,'补测畸形流处理后断开仍及时离线',bool(wait_for(lambda:known().get('status')=='offline')))
all_reused=True
for number in range(8):
    peer=handshake();peer.sendall(raw+frame(0xb29,0x32,b'\0'))
    online=wait_for(lambda:known().get('status')=='idle' and known().get('id')==identifier)
    all_reused&=bool(online);peer.close();all_reused&=bool(wait_for(lambda:known().get('status')=='offline'))
r.check(473,'补测连续八次连接断开复用设备ID，无重启且无临时影子',all_reused and len(devices())==1 and not any(x['code'].startswith('PENDING-') for x in devices()),[(x['id'],x['code']) for x in devices()])
sys.exit(int(bool(r.failures)))
