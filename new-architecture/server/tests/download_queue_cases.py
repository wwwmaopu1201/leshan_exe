"""Stop/interrupt real DT transfers, and keep later queued work runnable."""
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
    with urllib.request.urlopen(req,timeout=8) as response:return json.load(response).get('data')
def frame(number,payload=b'',kind=0x0b2a):
    raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,number,1,1,len(payload)+2)+payload;crc=65535
    for byte in raw:
        crc^=byte
        for _ in range(8):crc=(crc>>1)^(0xa001 if crc&1 else 0)
    return raw+struct.pack('>H',crc)
def exact(sock,n):
    result=b''
    while len(result)<n:
        data=sock.recv(n-len(result))
        if not data:raise EOFError()
        result+=data
    return result
def read(sock):
    head=exact(sock,23);return head+exact(sock,struct.unpack('>I',head[19:23])[0])
def command(sock,number):
    while True:
        packet=read(sock)
        if packet[13:17]==struct.pack('>HH',0x0b2a,number):return packet
        if packet[13:17]==struct.pack('>HH',0x0b29,0x33):sock.sendall(frame(0x33,b'\0',0x0b29))
def connect(working=False):
    sock=socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=5);sock.settimeout(3);command(sock,2)
    sock.sendall(frame(0x10fa,struct.pack('>II',1,887766)+'队列测试设备'.encode(),0x1302))
    sock.sendall(frame(0x32,b'\1' if working else b'\0',0x0b29));time.sleep(.1);return sock
def upload(name):
    boundary='queue-test';content=bytes(range(256))*128
    body=(f'--{boundary}\r\nContent-Disposition: form-data; name="name"\r\n\r\n{name}\r\n--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="queue.dst"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()+content+f'\r\n--{boundary}--\r\n'.encode()
    req=urllib.request.Request(base+'/pattern/upload',body,{'Content-Type':'multipart/form-data; boundary='+boundary,'Authorization':'Bearer '+token})
    with urllib.request.urlopen(req) as response:return json.load(response)['data']['id']
def queue():return api('GET','/pattern/queue')['list']
def task(identifier):return next((x for x in queue() if x['id']==identifier),{})
def wait(test,seconds=3):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=test()
        if value:return value
        time.sleep(.03)
    return False
def check(row,name,passed,detail=''):
    global failures
    item=dict(row=row,name=name,result='PASS' if passed else 'FAIL',detail=detail,scope='真实Qt服务端+DT模拟设备，实际下发数据帧/断连/确认；实机固件行为另验')
    with log.open('a') as out:out.write(json.dumps(item,ensure_ascii=False)+'\n')
    print(row,item['result'],name,detail,flush=True);failures+=not passed
def submit(pattern):
    before={x['id'] for x in queue()};api('POST','/pattern/download',dict(patternId=pattern,deviceIds=[device]))
    return next(x['id'] for x in queue() if x['id'] not in before)
def begin(sock,pattern):
    identifier=submit(pattern);command(sock,4);sock.sendall(frame(4));first=command(sock,5);assert len(first)>25;return identifier

token=api('POST','/auth/login',dict(username='admin',password='admin123'))['token']
sock=connect();device=api('GET','/device/list?keyword=887766')['list'][0]['id'];pattern=upload('队列控制')
identifier=begin(sock,pattern);sock.close()
check(263,'下发中设备断电断连，任务及时变失败而非永远下发中',bool(wait(lambda:task(identifier).get('status')=='failed')),task(identifier))
for operation,row in [('cancel',264),('pause',264)]:
    sock=connect();identifier=begin(sock,pattern)
    if operation=='cancel':api('DELETE','/pattern/queue/'+str(identifier))
    else:api('POST','/pattern/queue/'+str(identifier)+'/pause',{})
    sock.sendall(frame(6,b'\0'));extra=0
    try:
        while True:
            packet=read(sock)
            if packet[13:17]==struct.pack('>HH',0x0b2a,5):extra+=1
    except (EOFError,socket.timeout,ConnectionResetError):pass
    check(row,operation+'接口完成后迟到ACK不能继续发送下一数据帧',extra==0,dict(extraFrames=extra))
    check(row,operation+'后任务不会错误变完成',not task(identifier) if operation=='cancel' else task(identifier).get('status')=='paused')
    sock.close()
paused_task=identifier
api('POST','/pattern/queue/'+str(paused_task)+'/resume',{});started=time.monotonic();sock=connect();command(sock,4)
check(258,'暂停任务继续后5秒内重新开始，不需要等待10分钟',task(paused_task).get('status')=='downloading' and time.monotonic()-started<5)
sock.close();wait(lambda:task(paused_task).get('status')=='failed')
to_cancel=submit(pattern);api('POST','/pattern/queue/'+str(to_cancel)+'/pause',{});api('DELETE','/pattern/queue/'+str(to_cancel));new_task=submit(pattern)
started=time.monotonic();sock=connect();command(sock,4)
check(259,'暂停再取消后新任务5秒内开始，不被旧任务阻塞10分钟',not task(to_cancel) and task(new_task).get('status')=='downloading' and time.monotonic()-started<5)
sock.close();wait(lambda:task(new_task).get('status')=='failed')
sock=connect(working=True);first=submit(pattern);second=submit(pattern)
api('POST','/pattern/queue/'+str(first)+'/pause',{});sock.sendall(frame(0x32,b'\0',0x0b29));command(sock,4)
check(265,'第一个等待任务暂停，设备空闲后下一任务仍开始下发',bool(wait(lambda:task(second).get('status')=='downloading')) and task(first).get('status')=='paused')
sock.close();sys.exit(1 if failures else 0)
