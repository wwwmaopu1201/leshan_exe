"""Pending handshakes on a shared IP, pre-registration and real-code reconnect."""
import json
from pathlib import Path
import socket
import struct
import sys
import time
import urllib.request
base=f'http://127.0.0.1:{sys.argv[1]}/api';token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
def api(method,path,body=None):
 req=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,{'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
 with urllib.request.urlopen(req,timeout=8) as response:return json.load(response)['data']
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
def handshake():
 peer=socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=5)
 for _ in range(2):
  while True:
   packet=read(peer);kind,no=struct.unpack('>HH',packet[13:17])
   if kind==0xb2a and no==2:break
   if kind==0xb29:peer.sendall(frame(kind,0x32,b'\0'))
  if _==0:peer.sendall(frame(0xb2a,2))
 return peer
def identify(peer,code):peer.sendall(frame(0x1302,0x10fa,struct.pack('>II',1,code)+('协议名称'+str(code)).encode()));peer.sendall(frame(0xb29,0x32,b'\0'))
def rows():return api('GET','/device/list?pageSize=1000')['list']
def check(row,name,passed,actual=''):
 global failures
 with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',actual=actual,scope='真实Qt服务端+同一127.0.0.1源IP多条DT连接；刻意让两路先握手再上报不同设备编号'),ensure_ascii=False)+'\n')
 print(row,'PASS' if passed else 'FAIL',name,flush=True);failures+=not passed
token=api('POST','/auth/login',dict(username='admin',password='admin123'))['token']
registered=api('POST','/device',dict(code='111001',name='预登记',groupId=1))['id']
first=handshake();identify(first,111001);time.sleep(.15)
found=[x for x in rows() if x['code']=='111001'];check(473,'先预登记后协议接入按真实编号合并，不遗留临时影子设备',len(found)==1 and found[0]['id']==registered and found[0]['groupId']==1,found)
api('PUT','/device/'+str(registered),dict(name='用户改名'));first.close();time.sleep(.05)
second=handshake();identify(second,111001);time.sleep(.15);found=[x for x in rows() if x['code']=='111001']
check(473,'普通断开后同编号重连复用原ID并保留用户改名和分组',len(found)==1 and found[0]['id']==registered and found[0]['name']=='用户改名' and found[0]['groupId']==1,found)
a=handshake();b=handshake();pending=[x for x in rows() if x['code'].startswith('PENDING-')]
check(172,'同IP两路未识别握手各自保留独立临时身份，不相互顶掉',len(pending)==2 and len({x['id'] for x in pending})==2,pending)
identify(a,111003);identify(b,111004);time.sleep(.2);allrows=rows();identified=[x for x in allrows if x['code'] in ('111003','111004')]
check(172,'两路后续上报真实编号后均在线且设备ID不同',len(identified)==2 and len({x['id'] for x in identified})==2 and all(x['status']=='idle' for x in identified),identified)
check(473,'身份解析完成后活跃设备列表不残留PENDING影子',not any(x['code'].startswith('PENDING-') for x in allrows),[(x['id'],x['code']) for x in allrows])
for peer in (second,a,b):peer.close()
sys.exit(int(bool(failures)))
