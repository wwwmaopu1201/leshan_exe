"""Disposable Qt server + DT peer: byte fidelity, no retained patterns/tasks/logs."""
import argparse
import json
import os
from pathlib import Path
import socket
import sqlite3
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def packet(number, payload=b'', kind=0x0B2A):
    data = b'DT' + bytes(11) + struct.pack('>HHBBI', kind, number, 1, 1, len(payload)+2) + payload
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return data + struct.pack('>H', crc)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('server')
    parser.add_argument('--skip-expiry', action='store_true')
    args = parser.parse_args()
    http_port, tcp_port = free_port(), free_port()
    while tcp_port == http_port:
        tcp_port = free_port()
    with tempfile.TemporaryDirectory(prefix='boer-direct-test-') as temporary:
        root = Path(temporary)
        db = root/'test.db'
        log_path = root/'server-output.log'
        output = log_path.open('w')
        process = subprocess.Popen([str(Path(args.server).resolve()), '--headless', '--db', str(db), '--data-dir', temporary,
                                    '--port', str(http_port), '--tcp-port', str(tcp_port)],
                                   env=dict(os.environ, BOERLAN_SKIP_TRIAL='1', HTTP_ACCESS_LOG='1'), stdout=output, stderr=subprocess.STDOUT)
        base = f'http://127.0.0.1:{http_port}/api'
        token = ''
        peers = []

        def call(method, path, body=None, auth=None, raw=None, content_type='application/json'):
            data = raw if raw is not None else (json.dumps(body).encode() if body is not None else None)
            request = urllib.request.Request(base+path, data, {'Authorization': 'Bearer '+(token if auth is None else auth), 'Content-Type': content_type}, method=method)
            try:
                with urllib.request.urlopen(request, timeout=10) as response:
                    return response.status, json.load(response).get('data')
            except urllib.error.HTTPError as error:
                return error.code, json.load(error)

        def upload(device, data, name='DIRECTSECRET', path='/pattern/direct-upload', auth=None):
            boundary = 'direct-pattern-test-boundary'
            body = (f'--{boundary}\r\nContent-Disposition: form-data; name="name"\r\n\r\n{name}\r\n'
                    f'--{boundary}\r\nContent-Disposition: form-data; name="deviceId"\r\n\r\n{device}\r\n'
                    f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="fixture.slw"\r\n'
                    'Content-Type: application/octet-stream\r\n\r\n').encode()+data+f'\r\n--{boundary}--\r\n'.encode()
            return call('POST', path, auth=auth, raw=body, content_type='multipart/form-data; boundary='+boundary)

        def state(id, auth=None):
            return call('GET', '/pattern/direct-transfer/'+id, auth=auth)

        def counts():
            with sqlite3.connect(db) as connection:
                return {table: connection.execute('SELECT count(*) FROM '+table).fetchone()[0]
                        for table in ('patterns', 'download_tasks', 'upload_tasks', 'debug_logs', 'pattern_type_catalogs', 'order_no_catalogs', 'device_pattern_files')}

        def untouched(before):
            assert counts() == before, (before, counts())
            assert not list(root.glob('uploads/patterns/*'))
            assert not list(root.glob('pattern-upload-*.tmp'))

        class Peer:
            sequence = 0
            def __init__(self):
                Peer.sequence += 1
                self.sock = socket.create_connection(('127.0.0.1', tcp_port), timeout=5)
                peers.append(self)
                self.read()
                self.code = str(800000+Peer.sequence)
                self.sock.sendall(packet(0x10fa, struct.pack('>II', 1, int(self.code))+b'test-device', 0x1302))
                self.sock.sendall(packet(0x0032, b'\x00', 0x0b29))
                time.sleep(.12)
                status, rows = call('GET', '/device/list?keyword='+self.code)
                assert status == 200 and rows['list'], rows
                self.id = rows['list'][0]['id']
            def exact(self, count):
                result = b''
                while len(result) < count:
                    chunk = self.sock.recv(count-len(result))
                    if not chunk:
                        raise EOFError('Device connection closed')
                    result += chunk
                return result
            def read(self):
                head = self.exact(23)
                return head+self.exact(struct.unpack('>I', head[19:23])[0])
            def command(self, expected):
                while True:
                    data = self.read()
                    kind, number = struct.unpack('>HH', data[13:17])
                    if kind == 0x0b2a and number == expected:
                        return data
                    if kind == 0x0b29 and number == 0x33:
                        self.sock.sendall(packet(0x33, b'\x00', 0x0b29))
            def deliver(self, expected, resend=False):
                self.command(4)
                self.sock.sendall(packet(4))
                received = b''
                while len(received) < len(expected):
                    data = self.command(5)
                    if not received and resend:
                        self.sock.sendall(packet(0xe, bytes([2, data[18]])))
                        assert self.command(5)[23:-2] == data[23:-2]
                    received += data[23:-2]
                    self.sock.sendall(packet(6, b'\x00'))
                assert received == expected
            def close(self):
                self.sock.close()
                if self in peers:
                    peers.remove(self)
                time.sleep(.1)

        try:
            for _ in range(100):
                if process.poll() is not None:
                    raise RuntimeError(log_path.read_text())
                try:
                    status, login = call('POST', '/auth/login', dict(username='admin', password='admin123'))
                    if status == 200:
                        token = login['token']
                        break
                except (OSError, urllib.error.URLError):
                    time.sleep(.05)
            assert token, log_path.read_text()
            assert call('POST', '/user', dict(username='directother', password='test1234', nickname='Other', phone='13800138001', role='admin'))[0] == 200
            other = call('POST', '/auth/login', dict(username='directother', password='test1234'))[1]['token']
            role = call('POST', '/role', dict(name='nodirect', permissions=json.dumps({'home': True, 'fileManagement': False, 'patternFiles': False})))
            assert role[0] == 200, role
            assert call('POST', '/user', dict(username='limiteddt', password='test1234', nickname='Limited', phone='13800138002', role='nodirect'))[0] == 200
            limited = call('POST', '/auth/login', dict(username='limiteddt', password='test1234'))[1]['token']
            peer = Peer()
            before = counts()
            assert upload(peer.id, b'test', auth='')[0] == 401
            assert upload(peer.id, b'test', auth=limited)[0] == 403
            assert upload(peer.id, b'test', name='X'*23)[0] == 409
            assert upload(peer.id, b'')[0] == 400
            assert upload(peer.id, b'x'*(255*65536+1))[0] == 400
            untouched(before)
            print('PASS authentication, permission, name and size checks; no records/files/logs', flush=True)

            content = bytes(range(256))*1400
            status, task = upload(peer.id, content)
            assert status == 200, task
            id = task['id']
            assert state(id, other)[0] == 404
            assert call('DELETE', '/pattern/direct-transfer/'+id, auth=other)[0] == 404
            assert upload(peer.id, b'busy')[0] == 409
            peer.deliver(content, resend=True)
            time.sleep(.05)
            assert state(id)[1]['status'] == 'downloading', 'Must wait for the device write ACK'
            peer.sock.sendall(packet(5, b'\x00'))
            time.sleep(.08)
            status, result = state(id)
            assert status == 200 and result['status'] == 'completed' and result['progress'] == 100, result
            untouched(before)
            assert call('DELETE', '/pattern/direct-transfer/'+id)[0] == 200
            assert state(id)[0] == 404
            print('PASS byte-identical DT delivery + frame retransmission + final write ACK + ownership + busy exclusion', flush=True)
            peer.close()

            peer = Peer();before = counts()
            with sqlite3.connect(db) as connection:
                connection.execute("UPDATE devices SET status='working' WHERE id=?", (peer.id,))
            assert upload(peer.id, b'working')[0] == 409
            with sqlite3.connect(db) as connection:
                connection.execute("UPDATE devices SET status='idle' WHERE id=?", (peer.id,))
            status, task = upload(peer.id, b'nack')
            assert status == 200
            peer.command(4);peer.sock.sendall(packet(4));peer.command(5);peer.sock.sendall(packet(6, b'\x01'))
            time.sleep(.1)
            assert state(task['id'])[1]['status'] == 'failed'
            untouched(before)
            retained_failure = task['id']
            peer.close()
            print('PASS working-device rejection and negative ACK without retained transfer logs', flush=True)

            peer = Peer();before = counts()
            task = upload(peer.id, b'cancel')[1];peer.command(4)
            assert call('DELETE', '/pattern/direct-transfer/'+task['id'])[0] == 200
            assert state(task['id'])[0] == 404
            untouched(before)
            peer.close()
            print('PASS cancel removes in-memory task without adding files or database rows', flush=True)
            assert upload(peer.id, b'offline')[0] == 409

            if not args.skip_expiry:
                orphan, timeout = Peer(), Peer();before = counts()
                lost = upload(orphan.id, b'orphan')[1]['id'];orphan.command(4)
                hanging = upload(timeout.id, b'timeout')[1]['id'];timeout.command(4)
                corrupt = bytearray(packet(6, b'\x00'));corrupt[-1] ^= 1;timeout.sock.sendall(corrupt)
                print('Waiting up to 65 seconds to verify abandoned-client cleanup and machine timeout...', flush=True)
                deadline = time.monotonic()+65
                final = None
                while time.monotonic() < deadline:
                    time.sleep(3)
                    final = state(hanging)
                    state(retained_failure)  # Terminal results must expire even if polled.
                    if final[0] == 200 and final[1]['status'] == 'failed':
                        break
                assert final[0] == 200 and final[1]['status'] == 'failed', final
                assert state(lost)[0] == 404
                assert state(retained_failure)[0] == 404
                untouched(before)
                call('DELETE', '/pattern/direct-transfer/'+hanging)
                orphan.close();timeout.close()
                print('PASS orphan lease expiry, device timeout and malformed ACK produce no retained transfer logs', flush=True)

            peer = Peer()
            status, stored = upload(peer.id, b'normal-persistent', name='normal-control', path='/pattern/upload')
            assert status == 200, stored
            assert Path(stored['filePath']).is_file()
            assert call('POST', '/pattern/download', dict(patternId=stored['id'], deviceIds=[peer.id]))[0] == 200
            peer.deliver(b'normal-persistent');peer.sock.sendall(packet(5, b'\x00'));time.sleep(.1)
            queue = call('GET', '/pattern/queue')[1]['list']
            assert any(task['patternId'] == stored['id'] and task['status'] == 'completed' for task in queue)
            print('PASS original upload/library/delivery queue behavior preserved', flush=True)
            output.flush()
            assert 'DIRECTSECRET' not in log_path.read_text()
            assert '/api/pattern/direct-' not in log_path.read_text()
            print('ALL DIRECT TRANSFER CHECKS PASSED', flush=True)
        finally:
            for peer in list(peers):
                peer.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill();process.wait(timeout=5)
            output.close()


if __name__ == '__main__':
    main()
