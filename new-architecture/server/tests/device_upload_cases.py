"""Actual DT upload worker: metadata, NTP extension, progress and stale file IDs."""
import json
from pathlib import Path
import socket
import sqlite3
import struct
import threading
import sys
import time
import urllib.error
import urllib.request

base=f'http://127.0.0.1:{sys.argv[1]}/api'; token=''; failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
fixture=Path(__file__).resolve().parents[3]/'boer-lan-server/backend/uploads/patterns/20260505145113.101_波浪线.ntp'


def api(method,path,body=None):
    request=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,
                                   {'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
    try:
        with urllib.request.urlopen(request,timeout=10) as response:return response.status,json.load(response)
    except urllib.error.HTTPError as error:return error.code,json.load(error)


def data(method,path,body=None):return api(method,path,body)[1]['data']


def record(row,name,passed,detail=''):
    global failures
    with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',detail=detail,
        scope='真实Qt服务端+本机DT协议模拟设备；'+('真实NTP样本' if fixture.exists() else '全字节合成样本')),ensure_ascii=False)+'\n')
    print(row,'PASS' if passed else 'FAIL',name,flush=True);failures+=not passed


def frame(kind,number,payload=b'',total=1,index=1):
    raw=b'DT'+bytes(11)+struct.pack('>HHBBI',kind,number,total,index,len(payload)+2)+payload
    crc=0xffff
    for byte in raw:
        crc^=byte
        for _ in range(8):crc=(crc>>1)^(0xA001 if crc&1 else 0)
    return raw+struct.pack('>H',crc)


def exact(peer,count):
    result=b''
    while len(result)<count:
        piece=peer.recv(count-len(result))
        if not piece:raise RuntimeError('DT peer disconnected')
        result+=piece
    return result


def read(peer):
    head=exact(peer,23);return head+exact(peer,struct.unpack('>I',head[19:23])[0])


token=data('POST','/auth/login',dict(username='admin',password='admin123'))['token']
db=sqlite3.connect(sys.argv[3])
# A pre-existing server pattern with metadata that an overwrite must preserve.
db.execute("INSERT INTO patterns(name,file_name,file_path,file_size,pattern_type,stitches,unit_price,order_no,uploaded_at) VALUES('回传花型','old.NTP','',1,'保留类型',72000,2.5,'KEEP-ORDER','2000-01-01 00:00:00')")
pattern_id=db.execute("SELECT id FROM patterns WHERE name='回传花型'").fetchone()[0];db.commit()
with socket.create_connection(('127.0.0.1',int(sys.argv[2])),timeout=10) as peer:
    read(peer);peer.sendall(frame(0x0B2A,2));read(peer)
    peer.sendall(frame(0x1302,0x10FA,struct.pack('>II',1,87654)+'回传设备'.encode()))
    peer.sendall(frame(0x0B29,0x0032,b'\x01'));time.sleep(.1)
    device=data('GET','/device/list?keyword=87654')['list'][0]['id']
    db.execute("INSERT INTO device_pattern_files(device_id,pattern_no,file_name) VALUES(?,77,'回传花型.NTP')",(device,));db.commit()
    old_id=db.execute('SELECT MAX(id) FROM device_pattern_files').fetchone()[0]
    body=dict(deviceId=device,fileIds=[old_id])
    record(200,'未选冲突处理方式时拒绝自动覆盖旧花型',api('POST','/pattern/device-files/upload',body)[0]==409)
    record(198,'显式空重命名在开始传输前拒绝',api('POST','/pattern/device-files/upload',dict(body,conflictMode='rename',renameNames={str(old_id):' '}))[0]==400)
    listing={}
    def refresh():listing['response']=api('POST','/pattern/device-files/refresh',dict(deviceId=device))
    listing_thread=threading.Thread(target=refresh,daemon=True);listing_thread.start()
    assert read(peer)[13:17]==struct.pack('>HH',0x0B2A,9)
    data('POST','/pattern/device-files/upload',dict(body,conflictMode='overwrite'))
    task=data('GET','/pattern/upload-queue')['list'][0]['id']
    paused=api('POST',f'/pattern/upload-queue/{task}/pause',{})[0]==200
    resumed=api('POST',f'/pattern/upload-queue/{task}/resume',{})[0]==200
    record(213,'等待中的回传任务可暂停并继续',paused and resumed)
    entry=struct.pack('>H',77)+'回传花型.NTP'.encode('utf-16le').ljust(44,b'\0')
    entry+=struct.pack('>H',79)+'测试.NTP'.encode('utf-16le').ljust(44,b'\0')
    peer.sendall(frame(0x0B2A,9,entry));listing_thread.join(timeout=5)
    assert listing.get('response',(0,))[0]==200
    names=data('GET','/pattern/device-files?deviceId='+str(device))['list']
    record(220,'UTF16文件名测试.NTP不被误判为合法UTF8而乱码',any(x['fileName']=='测试.NTP' for x in names),[x['fileName'] for x in names])
    content=fixture.read_bytes() if fixture.exists() else bytes(range(256))*30
    def transfer():
        packet=read(peer)
        while packet[13:17]!=struct.pack('>HH',0x0B2A,7):packet=read(peer)
        peer.sendall(frame(0x0B2A,7,b'\x00'))
        for index,chunk in enumerate((content[:300],content[300:]),1):
            peer.sendall(frame(0x0B2A,0x000D,chunk,total=2,index=index));ack=read(peer)
            assert ack[13:17]==struct.pack('>HH',0x0B2A,0x000D) and ack[23]==0
            if index==1:
                queue=data('GET','/pattern/upload-queue')['list'][0]
                record(208,'第一帧后即显示中间进度，未提前完成',queue['status']=='uploading' and 0<queue['progress']<100)
        peer.sendall(frame(0x0B2A,7,b'\x02'));time.sleep(.15)
        return data('GET','/pattern/upload-queue')['list'][0]
    peer.sendall(frame(0x0B29,0x0032,b'\x00'))
    completed=transfer()
    record(213,'继续后真正向设备发送回传指令并完成',completed['status']=='completed')
    pattern=data('GET','/pattern/'+str(pattern_id))
    record(194,'覆盖同名文件保留类型、针数、工价和订单编号',pattern['patternType']=='保留类型' and pattern['stitches']==72000 and pattern['unitPrice']==2.5 and pattern['orderNo']=='KEEP-ORDER')
    record(195,'覆盖同名文件更新上传时间',pattern['uploadedAt']>'2000-01-01 00:00:00')
    record(192,'真实回传覆盖后服务器列表保留既有类型针数工价订单；回传本身不弹元数据维护',pattern['patternType']=='保留类型' and pattern['stitches']==72000 and pattern['unitPrice']==2.5 and pattern['orderNo']=='KEEP-ORDER')
    request=urllib.request.Request(base+f'/pattern/{pattern_id}/file',headers={'Authorization':'Bearer '+token})
    with urllib.request.urlopen(request) as response:
        record(197,'设备NTP回传后下载保持扩展名及完整内容',response.read()==content and '.NTP' in response.headers.get('Content-Disposition',''))
    record(207,'上传队列设备文件名不为空',completed['fileName']=='回传花型.NTP')
    db.execute('UPDATE device_pattern_files SET deleted_at=CURRENT_TIMESTAMP WHERE device_id=?',(device,))
    db.execute("INSERT INTO device_pattern_files(device_id,pattern_no,file_name) VALUES(?,77,'回传花型.NTP')",(device,));db.commit()
    selected=dict(body,selectedFiles=[dict(id=old_id,patternNo=77,fileName='回传花型.NTP')],conflictMode='rename')
    for name,row in [('自定义回传名',196),('再次自定义回传名',199)]:
        data('POST','/pattern/device-files/upload',dict(selected,renameNames={str(old_id):name}))
        finished=transfer();saved=data('GET','/pattern/'+str(finished['patternId']))
        record(row,'设备文件列表ID刷新后仍使用用户指定重命名：'+name,finished['status']=='completed' and saved['name']==name)
    db.execute("CREATE TRIGGER reject_upload_test BEFORE INSERT ON patterns WHEN NEW.name='保存失败夹具' BEGIN SELECT RAISE(ABORT,'INTERNAL_TEST_SQL_204'); END");db.commit()
    data('POST','/pattern/device-files/upload',dict(selected,renameNames={str(old_id):'保存失败夹具'}))
    failed=transfer()
    record(204,'数据库保存失败只显示友好提示，不向队列暴露内部SQL日志',failed['status']=='failed' and 'INTERNAL_TEST_SQL_204' not in failed['message'] and '失败' in failed['message'],failed['message'])
    db.execute('DROP TRIGGER reject_upload_test');db.commit()
    removed={}
    def remove():
        removed['response']=api('DELETE','/pattern/device-files/'+str(old_id),dict(selectedFile=dict(id=old_id,deviceId=device,patternNo=77,fileName='回传花型.NTP')))
    remove_thread=threading.Thread(target=remove,daemon=True);remove_thread.start()
    command=read(peer)
    assert command[13:17]==struct.pack('>HH',0x0B2A,8)
    peer.sendall(frame(0x0B2A,8,b'\x00'));remove_thread.join(timeout=5)
    empty=data('GET','/pattern/device-files?deviceId='+str(device))
    record(223,'设备文件ID刷新后仍可删除正确文件，并清除当前缓存',removed.get('response',(0,))[0]==200 and empty['total']==0)
    record(217,'删除设备内最后一个文件后不再显示历史列表',empty['list']==[])
    db.execute('UPDATE device_pattern_files SET deleted_at=CURRENT_TIMESTAMP WHERE device_id=?',(device,));db.commit()
    queued=data('GET','/pattern/upload-queue')['list'][0]
    record(207,'设备文件缓存清理后队列仍保留文件名快照',queued['fileName']=='回传花型.NTP')
db.close();sys.exit(1 if failures else 0)
