"""A complete download->production path, unknown patterns, idle time and employee wire fields."""
import datetime as dt
import hashlib
import json
from pathlib import Path
import socket
import sqlite3
import struct
import sys
import threading
import time
import urllib.request
from zoneinfo import ZoneInfo
base=f'http://127.0.0.1:{sys.argv[1]}/api';token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
def api(method,path,body=None):
 req=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,{'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
 with urllib.request.urlopen(req,timeout=8) as response:return json.load(response).get('data')
def frame(kind,no,payload=b''):
 raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,no,1,1,len(payload)+2)+payload;crc=65535
 for byte in raw:
  crc^=byte
  for _ in range(8):crc=(crc>>1)^(0xa001 if crc&1 else 0)
 return raw+struct.pack('>H',crc)
def exact(peer,n):
 result=b''
 while len(result)<n:
  value=peer.recv(n-len(result))
  if not value:raise EOFError()
  result+=value
 return result
def read(peer):
 head=exact(peer,23);return head+exact(peer,struct.unpack('>I',head[19:23])[0])
def command(peer,no):
 while True:
  packet=read(peer);kind,number=struct.unpack('>HH',packet[13:17])
  if kind==0xb2a and number==no:return packet
  if kind==0xb29:peer.sendall(frame(0xb29,0x33,b'\0\0\0'))
def upload(name,filename,price):
 boundary='production-lifecycle';content=b'TEST-'+name.encode();body=b''
 for key,value in dict(name=name,unitPrice=price).items():body+=f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode()
 body+=f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{filename}"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()+content+f'\r\n--{boundary}--\r\n'.encode()
 req=urllib.request.Request(base+'/pattern/upload',body,{'Content-Type':'multipart/form-data; boundary='+boundary,'Authorization':'Bearer '+token})
 with urllib.request.urlopen(req) as response:return json.load(response)['data']['id'],content
def record(row,name,passed,detail=''):
 global failures
 with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',detail=detail,scope='真实Qt服务端+DT模拟设备，实际批下发后发送生产上报；临时SQLite验证计件/工资/员工/会话'),ensure_ascii=False)+'\n')
 print(row,'PASS' if passed else 'FAIL',name,detail,flush=True);failures+=not passed
def bcd(value):return bytes(((n//10)<<4)|(n%10) for n in (value.year%100,value.month,value.day,value.isoweekday(),value.hour,value.minute,value.second))
def production(name,number,start,end,first=100,last=200,reason=0,unicode=False):
 raw=name.encode('utf-16le' if unicode else 'utf-8').ljust(44,b'\0')
 return struct.pack('>IH',779900,number)+raw+bcd(start)+struct.pack('>I',first)+bcd(end)+struct.pack('>I',last)+b'0001001\0'+struct.pack('>H',reason)
token=api('POST','/auth/login',dict(username='admin',password='admin123'))['token']
employee=api('POST','/employee',dict(code='0001001',name='测试员工',phone='13800138000',groupId=1))['id']
first,content_a=upload('测试','测试.NTP',2.5);second,content_b=upload('第二花型','样本B.NTP',3)
db=sqlite3.connect(sys.argv[3]);clock=dt.datetime.now(ZoneInfo('Asia/Shanghai')).replace(microsecond=0)
with socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=8) as peer:
 peer.settimeout(8);command(peer,2);peer.sendall(frame(0xb2a,2));command(peer,2);peer.sendall(frame(0x1302,0x10fa,struct.pack('>II',1,779900)+'生产设备'.encode()));time.sleep(.1)
 device=api('GET','/device/list?keyword=779900')['list'][0]['id']
 peer.sendall(frame(0xb2a,0xf2,b'0001001'.ljust(11,b'\0')));record(113,'既有工号绑定作为生产夹具（协议适配暂缓）',command(peer,0xf2)[23]==0 and api('GET','/device/'+str(device))['employeeName']=='测试员工')
 api('POST','/pattern/batch-download',dict(patternIds=[first,second],deviceIds=[device]));received=[]
 for _ in range(2):
  packet=command(peer,4);name=packet[23:-2].decode('utf-16le');peer.sendall(frame(0xb2a,4));data=command(peer,5)[23:-2];expected=content_a if name=='测试' else content_b
  assert data==expected;peer.sendall(frame(0xb2a,6,b'\0'));peer.sendall(frame(0xb2a,5,name.encode('utf-16le')+b'\0'));received.append(name)
 time.sleep(.1);tasks=api('GET','/pattern/queue')['list'];record(176,'两花型实际批量下发字节一致并完成，含无后缀UTF16完成回报',set(received)=={'测试','第二花型'} and all(x['status']=='completed' for x in tasks))
 start=clock-dt.timedelta(seconds=100);middle=clock-dt.timedelta(seconds=50)
 packet=production('测试.NTP',7,start,middle,unicode=True)
 for _ in range(2):peer.sendall(frame(0xb2a,0xc,packet));assert command(peer,0xc)[23]==0
 peer.sendall(frame(0xb2a,0xc,production('第二花型',8,middle,clock,200,300)));assert command(peer,0xc)[23]==0;time.sleep(.1)
 rows=db.execute('SELECT pattern_id,pattern_name,pieces FROM production_records ORDER BY id').fetchall()
 salary=api('GET','/statistics/salary')
 record(25,'UTF16中文花型关联正确服务器文件，不产生乱码未知花型',rows[0][0]==first and rows[0][1]=='测试',rows)
 record(176,'批量下发后正常上报计2件5.5元，重复帧不重复计件',len(rows)==2 and salary['totalPieces']==2 and salary['totalSalary']==5.5,salary['summary'])
 started=(dt.datetime.now(dt.timezone.utc)-dt.timedelta(seconds=200)).strftime('%Y-%m-%d %H:%M:%S')
 db.execute('UPDATE device_runtime_sessions SET started_at=?,last_seen_at=CURRENT_TIMESTAMP WHERE device_id=? AND ended_at IS NULL',(started,device));db.commit()
 before=next(x for x in api('GET','/statistics/home')['deviceUsage'] if x['deviceId']==device)
 seen_before=db.execute('SELECT MAX(last_seen_at) FROM device_runtime_sessions WHERE device_id=?',(device,)).fetchone()[0]
 for _ in range(6):peer.sendall(frame(0xb29,0x32,b'\0'));time.sleep(1)
 seen_after=db.execute('SELECT MAX(last_seen_at) FROM device_runtime_sessions WHERE device_id=?',(device,)).fetchone()[0]
 after=next(x for x in api('GET','/statistics/home')['deviceUsage'] if x['deviceId']==device)
 record(33,'无桌面客户端运行时服务端仍按设备保活更新会话时间',seen_after>seen_before,dict(before=seen_before,after=seen_after))
 record(33,'设备待机继续计时，未登录桌面客户端效率仍随开机时长更新',before['efficiency']>0 and after['efficiency']<before['efficiency'],dict(before=before['efficiency'],after=after['efficiency']))
 # A record from the prior name decoder must remain idempotent after the codec fix.
 legacy_start=start-dt.timedelta(minutes=5);legacy_end=middle-dt.timedelta(minutes=5);legacy_name='测试.NTP'.encode('utf-16le').split(b'\0',1)[0].decode('utf-8')
 parts=[str(device),'7',legacy_name,'300','400','0',legacy_start.astimezone(dt.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),legacy_end.astimezone(dt.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')]
 key=hashlib.sha1('|'.join(parts).encode()).hexdigest();stamp=legacy_end.strftime('%Y-%m-%d %H:%M:%S')
 old=db.execute('INSERT INTO production_records(device_id,employee_id,pattern_no,pattern_name,pieces,stitches,running_time,start_time,end_time,start_needle,end_needle,stop_reason,source_key,record_date) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)',(device,employee,7,legacy_name,1,100,50/3600,legacy_start.strftime('%Y-%m-%d %H:%M:%S'),stamp,300,400,0,key,stamp)).lastrowid
 db.execute('INSERT INTO salary_records(employee_id,device_id,pattern_name,pieces,unit_price,salary,total_amount,source_production_id,record_date) VALUES(?,?,?,?,?,?,?,?,?)',(employee,device,legacy_name,1,0,0,0,old,stamp));db.commit()
 peer.sendall(frame(0xb2a,0xc,production('测试.NTP',7,legacy_start,legacy_end,300,400,unicode=True)));assert command(peer,0xc)[23]==0;time.sleep(.1)
 record(176,'编码修复后重放旧去重键记录仍不重复计件或自动改历史工资',db.execute('SELECT COUNT(*) FROM production_records').fetchone()[0]==3 and api('GET','/statistics/salary')['totalSalary']==5.5)
 before_import=db.execute('SELECT COUNT(*) FROM production_records').fetchone()[0];result={}
 def refresh():result['data']=api('POST','/pattern/device-files/refresh',dict(deviceId=device))
 worker=threading.Thread(target=refresh,daemon=True);worker.start();command(peer,9);peer.sendall(frame(0xb2a,9,struct.pack('>H',9)+'U盘花型.NTP'.encode('utf-16le').ljust(44,b'\0')));worker.join(timeout=5)
 record(28,'设备新增U盘文件列表本身不会增加缝纫计数',not worker.is_alive() and db.execute('SELECT COUNT(*) FROM production_records').fetchone()[0]==before_import)
 peer.sendall(frame(0xb2a,0xc,production('U盘花型',9,clock-dt.timedelta(seconds=20),clock,500,600)));assert command(peer,0xc)[23]==0;time.sleep(.1)
 unknown=db.execute("SELECT pieces,pattern_name,unit_price FROM production_records WHERE pattern_no=9").fetchone()
 record(28,'U盘花型正常加工上报会计件，未配置工价时保留原名和零工价',unknown==(1,'U盘花型',0),unknown)
 usage=api('GET','/statistics/home')['patternUsage']
 record(25,'服务器未登记的U盘花型仍按设备原名显示实际加工次数',any(x['name']=='U盘花型' and x['value']==1 for x in usage),usage)
 code='00012345678';name='一二三四五六七八';api('POST','/employee',dict(code=code,name=name,phone='13900138000',groupId=1))
 peer.sendall(frame(0xb2a,0xf2,code.encode()));assert command(peer,0xf2)[23]==0
 peer.sendall(frame(0xb2a,0xf2,b'19999999999'));rejected=command(peer,0xf2)[23]==1
 linked=api('GET','/device/'+str(device));record(113,'既有工号绑定保留11位和未知工号拒绝（适配暂缓）',rejected and linked['employeeCode']==code and linked['employeeName']==name)
 api('PUT','/device/'+str(device),dict(employeeCode=code,employeeName=name));pushed=command(peer,0xf1)[23:-2]
 record(113,'既有11字节工号推送作为夹具（适配暂缓）',len(pushed)==27 and pushed[:11]==code.encode() and pushed[11:].decode('utf-16le')==name)
 peer.sendall(frame(0xb2a,0xf1,b'\0'))
 peer.sendall(frame(0xb29,0x33,b'\0'+struct.pack('>H',99)+'当前花型'.encode()));time.sleep(.1)
 historical_start=clock-dt.timedelta(minutes=10);historical_end=historical_start+dt.timedelta(seconds=20)
 peer.sendall(frame(0xb2a,0xc,production('第二花型',8,historical_start,historical_end,700,800)));assert command(peer,0xc)[23]==0;time.sleep(.1)
 current=api('GET','/device/'+str(device))
 record(25,'补报历史生产不覆盖实时花型，员工规则暂缓',current['currentPatternNo']==99 and current['currentPatternName']=='当前花型',dict(code=current['employeeCode'],name=current['employeeName'],pattern=current['currentPatternName']))
 peer.sendall(frame(0xb2a,0xc,production('第二花型',8,historical_start-dt.timedelta(minutes=1),historical_end-dt.timedelta(minutes=1),800,900,reason=3)));assert command(peer,0xc)[23]==0;time.sleep(.1)
 counted=db.execute('SELECT stop_reason,pieces FROM production_records WHERE stop_reason=3').fetchall()
 with log.open('a') as out:out.write(json.dumps(dict(row=26,name='非0停止原因计件现状已观察，急停回零枚举待明确',result='BLOCKED',blockedStatus='协议定义不足，待确认计件规则',actual=counted,scope='真实Qt服务端+DT生产帧；原因码0/3的现有行为核对',reason='本地协议只列停止原因字段及0的示例，旧代码测试还使用3，未提供正常完成/急停/回零枚举。当前模拟原因3仍计1件；已询问用户编码定义，不猜测修改工资和件数规则。'),ensure_ascii=False)+'\n')
db.close();sys.exit(1 if failures else 0)
