"""Workbook rows 170, 173, 174, 175: actual byte comparison with a DT peer."""
import json
import socket
import struct
import sys
import time
import urllib.request

base = f'http://127.0.0.1:{sys.argv[1]}/api'
token = ''


def api(path, body=None):
    request = urllib.request.Request(base + path, json.dumps(body).encode() if body is not None else None,
                                     {'Content-Type':'application/json','Authorization':'Bearer '+token})
    return json.load(urllib.request.urlopen(request, timeout=10))['data']


def frame(number, payload=b'', kind=0x0B2A):
    data = b'DT' + bytes(11) + struct.pack('>HHBBI',kind,number,1,1,len(payload)+2) + payload
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return data + struct.pack('>H',crc)


def exact(sock, count):
    data = b''
    while len(data)<count:
        piece = sock.recv(count-len(data))
        if not piece:
            raise RuntimeError('unexpected device disconnect')
        data += piece
    return data


def read(sock):
    head = exact(sock,23)
    return head + exact(sock,struct.unpack('>I',head[19:23])[0])


def upload(name, content):
    boundary = 'customer-boundary'
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="name"\r\n\r\n{name}\r\n'
            f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{name}.dst"\r\n'
            'Content-Type: application/octet-stream\r\n\r\n').encode() + content + f'\r\n--{boundary}--\r\n'.encode()
    request = urllib.request.Request(base+'/pattern/upload',body,
                                     {'Content-Type':'multipart/form-data; boundary='+boundary,'Authorization':'Bearer '+token})
    return json.load(urllib.request.urlopen(request))['data']['id']


token = api('/auth/login',dict(username='admin',password='admin123'))['token']
with socket.create_connection(('127.0.0.1',int(sys.argv[2])), timeout=10) as peer:
    assert read(peer)[13:17] == struct.pack('>HH',0x0B2A,2)
    peer.sendall(frame(0x10FA,struct.pack('>II',1,98765)+'批量设备'.encode(),0x1302))
    peer.sendall(frame(0x0032,b'\x00',0x0B29))
    time.sleep(.1)
    device = api('/device/list?keyword=98765')['list'][0]['id']
    for label, payloads in [('batch-10', [bytes([i])*20480 for i in range(10)]),
                            ('large-354KB',[bytes(range(256))*1416])]:
        ids = [upload(label+'-'+str(i),data) for i,data in enumerate(payloads)]
        started = time.monotonic()
        api('/pattern/batch-download',dict(patternIds=ids,deviceIds=[device]))
        received = []
        for expected in payloads:
            while True:
                command = read(peer)
                if command[13:17] == struct.pack('>HH',0x0B2A,4):
                    break
                # Respond to periodic realtime polling while waiting for the next task.
                if command[13:17] == struct.pack('>HH',0x0B29,0x0033):
                    peer.sendall(frame(0x0033,b'\x00',0x0B29))
            peer.sendall(frame(4))
            content = b''
            while len(content)<len(expected):
                packet = read(peer)
                assert packet[13:17] == struct.pack('>HH',0x0B2A,5), packet[:23]
                content += packet[23:-2]
                peer.sendall(frame(6,b'\x00'))
            assert content in payloads and content not in received, (label,len(content),len(expected))
            received.append(content)
            peer.sendall(frame(5,b'\x00'))
        time.sleep(.2)
        queue = api('/pattern/queue')['list']
        tasks = [q for q in queue if q['patternId'] in ids]
        assert len(received) == len(payloads)
        assert len(tasks) == len(ids) and all(q['status']=='completed' for q in tasks), tasks
        elapsed = time.monotonic()-started
        assert elapsed < 60, elapsed
        print(label, 'PASS',len(received),'files, byte identical, completed tasks, seconds=',round(elapsed,2),flush=True)
