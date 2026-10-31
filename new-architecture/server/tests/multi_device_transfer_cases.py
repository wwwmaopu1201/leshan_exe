"""Three simultaneous DT peers; a stalled peer must not block the other two."""
import json
from pathlib import Path
import socket
import struct
import sys
import threading
import time
import urllib.request
base=f'http://127.0.0.1:{sys.argv[1]}/api';tcp=int(sys.argv[2]);token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
payloads={'多设备花型一':bytes(range(256))*80,'多设备花型二':bytes(range(255,-1,-1))*80}
def api(method,path,body=None):
 req=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,{'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
 with urllib.request.urlopen(req,timeout=8) as response:return json.load(response).get('data')
def frame(kind,no,payload=b''):
 raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,no,1,1,len(payload)+2)+payload;crc=65535
 for b in raw:
  crc^=b
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
def upload(name,payload):
 boundary='multi-device-boundary';body=(f'--{boundary}\r\nContent-Disposition: form-data; name="name"\r\n\r\n{name}\r\n--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="multi.ntp"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()+payload+f'\r\n--{boundary}--\r\n'.encode()
 req=urllib.request.Request(base+'/pattern/upload',body,{'Content-Type':'multipart/form-data; boundary='+boundary,'Authorization':'Bearer '+token})
 with urllib.request.urlopen(req) as response:return json.load(response)['data']['id']
def check(row,name,passed,detail=''):
 global failures
 with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',detail=detail,scope='真实Qt服务端+三路并发DT模拟设备；一个设备第二任务持续无响应，实际等待60秒超时'),ensure_ascii=False)+'\n')
 print(row,'PASS' if passed else 'FAIL',name,detail,flush=True);failures+=not passed
stop=threading.Event();states=[dict(code=99801+i,ready=threading.Event(),done=threading.Event(),stalled=threading.Event(),received=[],errors=[]) for i in range(3)]
def peer_run(index):
 state=states[index]
 try:
  with socket.create_connection(('127.0.0.1',tcp),timeout=5) as peer:
   peer.settimeout(1);assert read(peer)[13:17]==struct.pack('>HH',0xb2a,2)
   peer.sendall(frame(0x1302,0x10fa,struct.pack('>II',1,state['code'])+('设备'+str(index)).encode()))
   peer.sendall(frame(0xb29,0x32,b'\0'));state['ready'].set()
   while not stop.is_set():
    try:packet=read(peer)
    except socket.timeout:continue
    kind,no=struct.unpack('>HH',packet[13:17])
    if kind==0xb29:peer.sendall(frame(0xb29,0x33,b'\0\0\0'));continue
    if kind!=0xb2a or no!=4:continue
    if index==1 and state['received']:
     state['stalled'].set();continue
    name=packet[23:-2].decode('utf-16le').rstrip('\0');peer.sendall(frame(0xb2a,4));content=b''
    while len(content)<len(payloads[name]):
     part=read(peer);assert part[13:17]==struct.pack('>HH',0xb2a,5);content+=part[23:-2];peer.sendall(frame(0xb2a,6,b'\0'))
    assert content==payloads[name] and name not in state['received'];state['received'].append(name);peer.sendall(frame(0xb2a,5,b'\0'))
    if len(state['received'])==2:state['done'].set();return
 except EOFError:
  if not state['stalled'].is_set() and not stop.is_set():state['errors'].append('unexpected disconnect')
 except Exception as error:state['errors'].append(repr(error));state['ready'].set()
 finally:state['done'].set()
token=api('POST','/auth/login',dict(username='admin',password='admin123'))['token']
threads=[threading.Thread(target=peer_run,args=(i,),daemon=True) for i in range(3)]
for worker in threads:worker.start()
try:
 for state in states:assert state['ready'].wait(5) and not state['errors'],state['errors']
 time.sleep(.1);devices=api('GET','/device/list')['list'];ids={int(x['code']):x['id'] for x in devices if x['code'].isdigit()}
 patterns=[upload(name,payload) for name,payload in payloads.items()];started=time.monotonic()
 api('POST','/pattern/batch-download',dict(patternIds=patterns,deviceIds=[ids[s['code']] for s in states]))
 good=states[0]['done'].wait(10) and states[2]['done'].wait(10)
 check(172,'一台设备第二任务卡住时另两台仍独立完成全部文件',good and len(states[0]['received'])==2 and len(states[2]['received'])==2 and not states[0]['errors'] and not states[2]['errors'],dict(seconds=round(time.monotonic()-started,2)))
 assert states[1]['stalled'].wait(5),'stalled peer did not receive its second task'
 deadline=time.monotonic()+65;failed=[]
 while time.monotonic()<deadline:
  tasks=api('GET','/pattern/queue')['list'];own=[x for x in tasks if x['deviceId']==ids[states[1]['code']]];failed=[x for x in own if x['status']=='failed']
  if failed:break
  time.sleep(.25)
 check(172,'持续无响应的设备单独超时失败，不把其他设备标错',len(failed)==1 and '超时' in failed[0]['message'] and all(x['status']=='completed' for x in tasks if x['deviceId'] in (ids[states[0]['code']],ids[states[2]['code']])),dict(elapsed=round(time.monotonic()-started,2),failed=failed))
 check(172,'故障设备已完成的第一个文件保持完成，失败不覆盖历史成功',len(states[1]['received'])==1 and len([x for x in own if x['status']=='completed'])==1)
finally:
 stop.set()
 for worker in threads:worker.join(timeout=2)
sys.exit(1 if failures else 0)
