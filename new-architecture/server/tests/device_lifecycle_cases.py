"""Delete a live DT device, then reconnect without restarting the Qt service."""
import json
from pathlib import Path
import socket
import struct
import sys
import time
import urllib.request
base=f'http://127.0.0.1:{sys.argv[1]}/api';tcp=int(sys.argv[2]);token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
def api(method,path,body=None):
    req=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,{'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
    with urllib.request.urlopen(req,timeout=5) as response:return json.load(response)['data']
def frame(kind,number,payload=b''):
    raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,number,1,1,len(payload)+2)+payload;crc=65535
    for byte in raw:
        crc^=byte
        for _ in range(8):crc=(crc>>1)^(0xA001 if crc&1 else 0)
    return raw+struct.pack('>H',crc)
def read(sock):
    head=sock.recv(23,socket.MSG_WAITALL)
    if len(head)<23:return head
    return head+sock.recv(struct.unpack('>I',head[19:23])[0],socket.MSG_WAITALL)
def connect():
    sock=socket.create_connection(('127.0.0.1',tcp),timeout=5);sock.settimeout(5)
    assert read(sock)[13:17]==struct.pack('>HH',0x0B2A,2)
    sock.sendall(frame(0x0B2A,2));read(sock)
    sock.sendall(frame(0x1302,0x10FA,struct.pack('>II',1,654321)+'中文重连设备'.encode()))
    time.sleep(.2);return sock
def check(row,name,passed,detail=''):
    global failures
    item=dict(row=row,name=name,result='PASS' if passed else 'FAIL',detail=detail,scope='真实Qt服务端+本机DT协议模拟设备；未连接客户实机')
    with log.open('a') as out:out.write(json.dumps(item,ensure_ascii=False)+'\n')
    print(row,item['result'],name,detail,flush=True);failures+=not passed
def device():return [x for x in api('GET','/device/list')['list'] if x['code']=='654321']
token=api('POST','/auth/login',dict(username='admin',password='admin123'))['token']
sock=connect();original=device()[0]
check(447,'UTF8中文DT设备名称按原文显示',original['name']=='中文重连设备',original['name'])
api('DELETE','/device/'+str(original['id'])+'/hard')
check(472,'服务端彻底删除后客户端使用的列表接口不再返回设备',not device())
try:disconnected=sock.recv(23)==b''
except socket.timeout:disconnected=False
check(473,'彻底删除主动关闭旧设备连接',disconnected);sock.close()
sock=connect();reconnected=device()
check(473,'删除后不重启服务端，同编号设备可重新登记连接',len(reconnected)==1 and reconnected[0]['status']=='idle',reconnected)
sock.close();sys.exit(1 if failures else 0)
