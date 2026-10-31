"""Device file list frames: out-of-order, duplicate, malformed, timeout, empty."""
import socket
import struct
import sys
import threading
from supplement_helpers import CustomerRun, wait_for

r=CustomerRun(sys.argv[1], '补测：真实Qt服务端HTTP+DT模拟设备；多帧设备文件列表异常恢复，非实机')
def frame(kind,no,payload=b'',total=1,index=1):
    raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,no,total,index,len(payload)+2)+payload
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
    header=exact(peer,23);return header+exact(peer,struct.unpack('>I',header[19:23])[0])
def command(peer,no):
    while True:
        packet=read(peer);kind,number=struct.unpack('>HH',packet[13:17])
        if kind==0xb2a and number==no:return packet
        if kind==0xb29:peer.sendall(frame(kind,0x32,b'\0'))
def entry(number,name):return struct.pack('>H',number)+name.encode('utf-16le').ljust(44,b'\0')
with socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=5) as peer:
    command(peer,2);peer.sendall(frame(0xb2a,2));command(peer,2)
    peer.sendall(frame(0x1302,0x10fa,struct.pack('>II',1,779922)+'文件分帧设备'.encode()))
    found=wait_for(lambda:next((x for x in r.data('GET','/device/list')['list'] if x['code']=='779922'),None))
    assert found,'device missing'
    device=found['id']
    def refresh(frames):
        result={}
        def request():
            try:result['response']=r.call('POST','/pattern/device-files/refresh',dict(deviceId=device))
            except Exception as error:result['error']=repr(error)
        worker=threading.Thread(target=request);worker.start();command(peer,9)
        for raw in frames:peer.sendall(raw)
        worker.join(timeout=12)
        assert not worker.is_alive(), 'refresh did not finish'
        assert 'response' in result,result
        return result['response']
    def listing():return r.data('GET','/pattern/device-files?deviceId='+str(device))['list']
    a,b=entry(77,'分帧A.NTP'),entry(78,'分帧B.NTP')
    status,result=refresh([frame(0xb2a,9,b,2,2),frame(0xb2a,9,b,2,2),frame(0xb2a,9,a,2,1)])
    original=listing();snapshot={(x['id'],x['patternNo'],x['fileName']) for x in original}
    r.check(220,'补测设备列表两帧乱序及重复帧，只保存完整两文件且中文编号正确',status==200 and {(x['patternNo'],x['fileName']) for x in original}=={(77,'分帧A.NTP'),(78,'分帧B.NTP')},result)
    for name,frames in [
        ('非法条目长度',[frame(0xb2a,9,b'bad')]),
        ('总帧数不一致',[frame(0xb2a,9,a,2,1),frame(0xb2a,9,b,3,2)]),
        ('缺最后一帧超时',[frame(0xb2a,9,a,2,1)])]:
        status,result=refresh(frames)
        r.check(218,'补测设备列表'+name+'明确失败，不报读取成功',status!=200 and bool(result.get('message')),result)
        r.check(220,'补测设备列表'+name+'保留上次完整缓存，不写入半份列表',
                {(x['id'],x['patternNo'],x['fileName']) for x in listing()}==snapshot)
    status,result=refresh([frame(0xb2a,9)])
    r.check(217,'补测设备实际返回空列表后清除旧缓存，异常后下一次读取仍可恢复',status==200 and listing()==[],result)
sys.exit(int(bool(r.failures)))
