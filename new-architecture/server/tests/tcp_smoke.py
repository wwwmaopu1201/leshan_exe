"""Run against a disposable server: python3 tests/tcp_smoke.py [http_port] [tcp_port]."""

import json
import socket
import struct
import sys
import time
import urllib.request

HTTP_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18088
TCP_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 18400


def frame(kind, number, payload=b""):
    head = b"DT" + b"\0" * 11 + struct.pack(">HHBBI", kind, number, 1, 1, len(payload) + 2)
    raw = head + payload
    checksum = 0xFFFF
    for byte in raw:
        checksum ^= byte
        for _ in range(8):
            checksum = (checksum >> 1) ^ (0xA001 if checksum & 1 else 0)
    return raw + struct.pack(">H", checksum)


def read_frame(sock):
    data = b""
    while len(data) < 25:
        data += sock.recv(25 - len(data))
    length = struct.unpack(">I", data[19:23])[0]
    while len(data) < 23 + length:
        data += sock.recv(23 + length - len(data))
    return data


base = f"http://127.0.0.1:{HTTP_PORT}/api"
login = urllib.request.Request(base + "/auth/login", data=b'{"username":"admin","password":"admin123"}', headers={"Content-Type": "application/json"})
token = json.load(urllib.request.urlopen(login))["data"]["token"]

with socket.create_connection(("127.0.0.1", TCP_PORT), timeout=5) as sock:
    assert read_frame(sock)[13:17] == struct.pack(">HH", 0x0B2A, 0x0002)
    sock.sendall(frame(0x1302, 0x10FA, struct.pack(">II", 1, 12345) + "测试设备".encode()))
    sock.sendall(frame(0x0B2A, 0x0001))
    assert read_frame(sock)[13:17] == struct.pack(">HH", 0x0B2A, 0x0001)
    sock.sendall(frame(0x0B29, 0x0032, b"\x01"))
    time.sleep(0.1)
    request = urllib.request.Request(base + "/device/list?keyword=12345", headers={"Authorization": "Bearer " + token})
    devices = json.load(urllib.request.urlopen(request))["data"]["list"]
    assert len(devices) == 1 and devices[0]["code"] == "12345" and devices[0]["status"] == "working", devices
    sock.settimeout(13)
    assert read_frame(sock)[13:17] == struct.pack(">HH", 0x0B29, 0x0033)
    request = urllib.request.Request(base + "/statistics/dashboard?deviceId=" + str(devices[0]["id"]),
                                     headers={"Authorization": "Bearer " + token})
    dashboard = json.load(urllib.request.urlopen(request))["data"]
    assert dashboard["runningTime"] > 0 and dashboard["processingTime"] == 0, dashboard

time.sleep(0.1)
request = urllib.request.Request(base + "/device/list?keyword=12345", headers={"Authorization": "Bearer " + token})
devices = json.load(urllib.request.urlopen(request))["data"]["list"]
assert devices[0]["status"] == "working", devices
time.sleep(5.2)
request = urllib.request.Request(base + "/device/list?keyword=12345", headers={"Authorization": "Bearer " + token})
devices = json.load(urllib.request.urlopen(request))["data"]["list"]
assert devices[0]["status"] == "offline", devices
print("TCP smoke: probe, heartbeat, realtime query, status and reconnect grace passed")
