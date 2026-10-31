"""V1.2 sections 5.33/5.34: the wire user ID is a phone number, not employee code."""
import json
from pathlib import Path
import socket
import sqlite3
import struct
import sys
import time
import urllib.error
import urllib.request
base=f'http://127.0.0.1:{sys.argv[1]}/api';token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
def call(method,path,body=None):
 req=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,{'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
 try:
  with urllib.request.urlopen(req,timeout=8) as response:return response.status,json.load(response)
 except urllib.error.HTTPError as error:return error.code,json.load(error)
def api(method,path,body=None):return call(method,path,body)[1].get('data')
def frame(kind,no,payload=b''):
 raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,no,1,1,len(payload)+2)+payload;crc=65535
 for byte in raw:
  crc^=byte
  for _ in range(8):crc=(crc>>1)^(0xa001 if crc&1 else 0)
 return raw+struct.pack('>H',crc)
def exact(peer,n):
 data=b''
 while len(data)<n:
  value=peer.recv(n-len(data))
  if not value:raise EOFError()
  data+=value
 return data
def command(peer,no):
 while True:
  head=exact(peer,23);packet=head+exact(peer,struct.unpack('>I',head[19:23])[0]);kind,number=struct.unpack('>HH',head[13:17])
  if kind==0xb2a and number==no:return packet
  if kind==0xb29:peer.sendall(frame(0xb29,0x33,b'\0\0\0'))
def record(row,name,passed,detail=''):
 global failures
 with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',detail=detail,scope='真实Qt服务端+DT帧；按用户提供V1.2协议5.33/5.34核对手机号ID，独立临时SQLite'),ensure_ascii=False)+'\n')
 print(row,'PASS' if passed else 'FAIL',name,detail,flush=True);failures+=not passed
token=api('POST','/auth/login',dict(username='admin',password='admin123'))['token']
a=api('POST','/employee',dict(code='1001',name='工号甲',phone='138 0013 8000',groupId=1))
b=api('POST','/employee',dict(code='13800138000',name='工号乙',phone='13900138000',groupId=1))
db=sqlite3.connect(sys.argv[3]);db.execute("UPDATE employees SET phone='138 0013 8000' WHERE code='1001'");db.commit()
with socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=8) as peer:
 peer.settimeout(8);command(peer,2);peer.sendall(frame(0xb2a,2));command(peer,2);peer.sendall(frame(0x1302,0x10fa,struct.pack('>II',1,887799)+'员工协议设备'.encode()));time.sleep(.1)
 device=api('GET','/device/list?keyword=887799')['list'][0]['id'];path='/device/'+str(device)
 peer.sendall(frame(0xb2a,0xf2,b'13800138000'));ack=command(peer,0xf2)[23];current=api('GET',path)
 record(100,'F2手机号与他人工号相同也只按手机号绑定正确员工',ack==0 and current['employeeCode']=='1001' and current['employeeName']=='工号甲',dict(code=current['employeeCode'],name=current['employeeName']))
 peer.sendall(frame(0xb2a,0xf2,b'13900138000'));ack=command(peer,0xf2)[23];current=api('GET',path)
 record(113,'F2合法手机号无需与工号相同即可上工',ack==0 and current['employeeCode']=='13800138000',current['employeeCode'])
 api('PUT',path,dict(employeeCode='1001',employeeName='错误姓名'));packet=command(peer,0xf1)[23:-2];peer.sendall(frame(0xb2a,0xf1,b'\0'));current=api('GET',path)
 record(113,'人工关联工号后姓名来自员工库，不可伪造独立姓名',current['employeeCode']=='1001' and current['employeeName']=='工号甲')
 record(100,'F1真正发送11字节手机号及Unicode姓名，而非工号',packet[:11]==b'13800138000' and packet[11:].decode('utf-16le').rstrip('\0')=='工号甲',dict(wireId=packet[:11].decode(errors='replace')))
 before=api('GET',path)['employeeCode']
 peer.sendall(frame(0xb2a,0xf2,b'138001380009'));ack=command(peer,0xf2)[23]
 record(113,'超长手机号载荷不能截断为有效号码而误绑定',ack==1 and api('GET',path)['employeeCode']==before)
 db.execute("INSERT INTO employees(code,name,phone,group_id) VALUES('9999','重复手机','13800138000',1)");db.commit()
 before=api('GET',path)['employeeCode'];peer.sendall(frame(0xb2a,0xf2,b'13800138000'));ack=command(peer,0xf2)[23]
 record(313,'历史重复手机号匹配多员工时拒绝上工，不随意选择第一个',ack==1 and api('GET',path)['employeeCode']==before)
 result=call('PUT',path,dict(employeeCode='404404',employeeName='不存在'))
 record(113,'未注册工号人工绑定拒绝，不能只改设备缓存',result[0]==400 and api('GET',path)['employeeCode']==before,result[1].get('message'))
 db.close()
sys.exit(1 if failures else 0)
