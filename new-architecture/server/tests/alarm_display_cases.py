"""Real DT alarms, wording-only display, clearing, and legacy records."""
import datetime as dt
import json
from pathlib import Path
import socket
import sqlite3
import struct
import sys
import time
import urllib.parse
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
def command(peer,no):
 while True:
  head=exact(peer,23);packet=head+exact(peer,struct.unpack('>I',head[19:23])[0]);kind,number=struct.unpack('>HH',head[13:17])
  if kind==0xb2a and number==no:return packet
  if kind==0xb29:peer.sendall(frame(0xb29,0x33,b'\0\0\0'))
def check(row,name,passed,actual=''):
 global failures
 with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',actual=actual,scope='真实Qt服务端+DT报警帧；本地旧协议映射，SQLite历史数据兼容与加工时间窗口；实机固件另验'),ensure_ascii=False)+'\n')
 print(row,'PASS' if passed else 'FAIL',name,flush=True);failures+=not passed
token=api('POST','/auth/login',dict(username='admin',password='admin123'))['token']
db=sqlite3.connect(sys.argv[3])
with socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=8) as peer:
 peer.settimeout(8);command(peer,2);peer.sendall(frame(0xb2a,2));command(peer,2);peer.sendall(frame(0x1302,0x10fa,struct.pack('>II',1,887755)+'报警显示设备'.encode()));time.sleep(.1)
 device=api('GET','/device/list?keyword=887755')['list'][0]['id'];path='/device/'+str(device);query='?deviceId='+str(device)
 peer.sendall(frame(0xb97,1,struct.pack('>H',59)));time.sleep(.1)
 alarm=api('GET','/statistics/alarm'+query)['list'][0]
 check(291,'协议码59先回原点只显示文案，不拼接E.FIRST_TOZERO编号',alarm['alarmInfo']=='先回原点!' and alarm['alarmCode']=='E.FIRST_TOZERO',alarm)
 check(307,'报警统计饼图类型也只显示文案',api('GET','/statistics/alarm'+query)['alarmTypePie'][0]['name']=='先回原点!')
 now=dt.datetime.now();day=now.date().isoformat();start=(now-dt.timedelta(seconds=59)).strftime('%Y-%m-%d %H:%M:%S');end=(now+dt.timedelta(seconds=1)).strftime('%Y-%m-%d %H:%M:%S')
 db.execute('INSERT INTO production_records(device_id,pattern_name,pieces,stitches,running_time,record_date,start_time,end_time) VALUES(?,?,?,?,?,?,?,?)',(device,'报警加工窗口',1,300,1/60,day,start,end));db.commit()
 processes=api('GET','/statistics/process'+query)['list']
 check(291,'加工明细关联报警时间窗口后也仅显示先回原点文案',processes[0]['alarmInfo']=='先回原点!',processes)
 check(289,'加工明细平均缝纫速度按针数/分钟计算，300针一分钟为300',processes[0]['sewSpeed']==300,processes[0]['sewSpeed'])
 db.execute("UPDATE alarm_records SET description='',alarm_type='E.FIRST_TOZERO - 先回原点!' WHERE device_id=?",(device,));db.commit()
 alarm=api('GET','/statistics/alarm'+query)['list'][0]
 check(307,'兼容旧库仅存拼接类型且缺描述时移除重复编号',alarm['alarmInfo']=='先回原点!',alarm)
 name=urllib.parse.quote('先回原点!');filtered=api('GET','/statistics/alarm'+query+'&alarmType='+name)
 check(305,'按实际报警文案筛选可以查到历史带编号记录',filtered['total']==1,filtered)
 peer.sendall(frame(0xb97,1,struct.pack('>H',0)));time.sleep(.1)
 alarm=api('GET','/statistics/alarm'+query)['list'][0]
 check(291,'收到清除码0后解除当前报警且历史标为已处理',api('GET',path)['alarmCode']=='' and alarm['status']=='已处理')
 peer.sendall(frame(0xb97,1,struct.pack('>H',60)));time.sleep(.1)
 alarm=api('GET','/statistics/alarm'+query)['list'][0]
 check(305,'生产量计数器上限报警按文案展示并保留API原始代码',alarm['alarmInfo']=='生产量计数器上限到达!' and alarm['alarmCode']=='E.PRODUCT_COUNT',alarm)
 peer.sendall(frame(0xb97,1,struct.pack('>H',0)));peer.sendall(frame(0xb97,1,struct.pack('>H',505)));time.sleep(.1)
 alarm=api('GET','/statistics/alarm'+query)['list'][0]
 for row in (305,306):
  with log.open('a') as out:out.write(json.dumps(dict(row=row,name='客户截图505及更换机针报警缺固件映射，不能猜测替换',result='BLOCKED',blockedStatus='缺少固件报警映射，实机待验证',actual=alarm,scope='已核对原表第305/306行截图，并真实发送DT码505观察；现有旧映射仅覆盖1至165',reason='截图显示505未知报警，文字提到更换机针；现有协议和仓库映射没有505定义，也没有客户固件可验证更换机针后的清除条件。保留未知报警与原始码，需固件报警码表和实机复测；已验证59/60及清除码0。'),ensure_ascii=False)+'\n')
 peer.sendall(frame(0xb97,1,struct.pack('>H',0)))
db.close();sys.exit(1 if failures else 0)
