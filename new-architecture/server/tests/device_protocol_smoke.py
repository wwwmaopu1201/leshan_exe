"""Exercise real DT frames against a disposable --headless Qt server."""

import json
import datetime
import socket
import sqlite3
import struct
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

api_port = int(sys.argv[1])
tcp_port = int(sys.argv[2])
database_path = sys.argv[3]
base = f"http://127.0.0.1:{api_port}/api"


def api(method, path, body=None, token=None):
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(base + path, json.dumps(body).encode() if body is not None else None,
                                     headers, method=method)
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.load(response)


def upload_pattern(token, content, name="下载花型", unit_price=0):
    boundary = b"boer-device-smoke"
    body = (b"--" + boundary + b"\r\nContent-Disposition: form-data; name=\"name\"\r\n\r\n"
            + name.encode() + b"\r\n--" + boundary +
            b"\r\nContent-Disposition: form-data; name=\"unitPrice\"\r\n\r\n" +
            str(unit_price).encode() + b"\r\n--" + boundary +
            b"\r\nContent-Disposition: form-data; name=\"file\"; filename=\"download.dst\"\r\n"
            b"Content-Type: application/octet-stream\r\n\r\n" + content + b"\r\n--" + boundary + b"--\r\n")
    request = urllib.request.Request(base + "/pattern/upload", data=body,
                                     headers={"Content-Type": "multipart/form-data; boundary=" + boundary.decode(),
                                              "Authorization": "Bearer " + token}, method="POST")
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.load(response)["data"]


def frame(param_type, param_no, payload=b"", total=1, index=1):
    head = b"DT" + bytes(11) + struct.pack(">HHBBI", param_type, param_no, total, index, len(payload) + 2)
    data = head + payload
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return data + struct.pack(">H", crc)


def read_frame(connection):
    head = connection.recv(23, socket.MSG_WAITALL)
    assert head[:2] == b"DT", head
    length = struct.unpack(">I", head[19:23])[0]
    return head + connection.recv(length, socket.MSG_WAITALL)


token = api("POST", "/auth/login", {"username": "admin", "password": "admin123"})["data"]["token"]
api("POST", "/employee", {"code": "100001", "name": "张三", "phone": "13800138000", "groupId": 1}, token)
production_pattern = upload_pattern(token, b"PRODUCTION-PATTERN", "花型A", 2.5)
with socket.create_connection(("127.0.0.1", tcp_port), timeout=5) as connection:
    connection.settimeout(5)
    probe = read_frame(connection)
    assert probe[13:17] == struct.pack(">HH", 0x0B2A, 2)
    connection.sendall(frame(0x0B2A, 2))
    assert read_frame(connection)[13:17] == struct.pack(">HH", 0x0B2A, 2)
    connection.sendall(frame(0x1302, 0x10FA, struct.pack(">II", 1, 12345) + "测试设备".encode()))
    connection.sendall(frame(0x0B2A, 0x00F2, b"100001".ljust(11,b"\0")))
    assert read_frame(connection)[23] == 0
    connection.sendall(frame(0x0B29, 0x0033, b"\x01" + struct.pack(">H", 8) + "花型A".encode()))
    connection.sendall(frame(0x0B97, 1, struct.pack(">H", 1)))
    time.sleep(0.2)
    devices = api("GET", "/device/list", token=token)["data"]["list"]
    device = next(row for row in devices if row["code"] == "12345")
    assert device["status"] == "working" and device["employeeCode"] == "100001", device
    assert device["currentPatternNo"] == 8 and device["alarmCode"] == "E.025", device
    # Validate the documented realtime fields independently of historical totals.
    connection.sendall(frame(0x1302,0x107C,struct.pack('>H',2000)))
    connection.sendall(frame(0x1301,0x00A3,struct.pack('>H',3500)))
    connection.sendall(frame(0x0B01,0x001F,struct.pack('>I',12345)))
    connection.sendall(frame(0x0B29,0x0003,struct.pack('>II',8,3)))
    time.sleep(.1)
    realtime=api('GET','/device/'+str(device['id']),token=token)['data']
    checks=[(467,'DT上报实时转速2000及最高3500分别保存',realtime['currentSpeed']==2000 and realtime['maxSpeedValue']==3500),
            (118,'DT累计针数12345保持针数单位而非加工件数',realtime['needleCountValue']==12345),
            (468,'协议生产计数字段分别为累计8和当前3',realtime['productionTotal']==8 and realtime['productionCurrent']==3),
            (99,'协议设备型号数字1已单独保存为modelName',realtime['modelName']=='1')]
    import os
    journal=os.environ.get('BOER_CUSTOMER_ROW_LOG')
    for row,name,passed in checks:
        if journal:
            with open(journal,'a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',scope='真实Qt服务端+实际DT帧；协议字段与已知数值核对，未接客户固件'),ensure_ascii=False)+'\n')
        assert passed,(name,realtime)
    workshop = api("POST", "/group", {"name": "测试车间", "parentId": 1}, token)["data"]
    team = api("POST", "/group", {"name": "测试班组", "parentId": workshop["id"]}, token)["data"]
    api("PUT", "/device/" + str(device["id"]), {"groupId": team["id"]}, token)
    home = api("GET", "/statistics/home", token=token)["data"]
    usage = next(item for item in home["deviceUsage"] if item["deviceId"] == device["id"])
    assert usage["groupId"] == team["id"] and usage["lineId"] == workshop["id"], usage
    logs = api("GET", "/system/logs?limit=80", token=token)["data"]
    assert any(item["source"] == "tcp" and "设备已识别" in item["message"] for item in logs), logs
    api("PUT", "/device/" + str(device["id"]),
        {"employeeCode": "100001", "employeeName": "张三"}, token)
    current_user = read_frame(connection)
    assert current_user[13:17] == struct.pack(">HH", 0x0B2A, 0x00F1), current_user
    assert current_user[23:34] == b"100001".ljust(11,b"\0") and len(current_user[23:-2]) == 27, current_user
    connection.sendall(frame(0x0B2A, 0x00F1, b"\x00"))
    connection.sendall(frame(0x0B29, 0x0034))
    connection.sendall(frame(0x0B97, 0x0002, struct.pack(">HB", 5, 1)))
    connection.sendall(frame(0x0B29, 0x0029, b"\x01"))
    time.sleep(0.1)
    logs = api("GET", "/system/logs?limit=80", token=token)["data"]
    assert any("设备已接受当前用户更新" in item["message"] for item in logs), logs
    assert any("设备空闲报警" in item["message"] for item in logs), logs
    api("PUT", "/device/" + str(device["id"]), {"name": "用户命名"}, token)
    connection.sendall(frame(0x1302, 0x10FA, struct.pack(">II", 1, 12345) + "协议新名".encode()))
    time.sleep(0.1)
    renamed = api("GET", "/device/" + str(device["id"]), token=token)["data"]
    assert renamed["name"] == "用户命名" and renamed["initialName"] == "协议新名", renamed
    connection.sendall(frame(0x0B97, 1, b"\x00\x00"))
    time.sleep(0.1)
    cleared = api("GET", "/device/" + str(device["id"]), token=token)["data"]
    assert cleared["alarmCode"] == "", cleared
    start = datetime.datetime.now().replace(hour=12, minute=0, second=0, microsecond=0)
    end = start + datetime.timedelta(seconds=30)
    def bcd_time(value):
        digits = [value.year % 100, value.month, value.day, value.isoweekday(),
                  value.hour, value.minute, value.second]
        return bytes(((number // 10) << 4) | (number % 10) for number in digits)
    name = "花型A".encode().ljust(44, b"\0")
    production = (struct.pack(">IH", 12345, 8) + name + bcd_time(start) + struct.pack(">I", 100) +
                  bcd_time(end) + struct.pack(">I", 120) + b"100001".ljust(8, b"\0") + struct.pack(">H", 0))
    assert len(production) == 82
    for _ in range(2):
        connection.sendall(frame(0x0B2A, 0x000C, production))
        assert read_frame(connection)[23] == 0
    old_start = start - datetime.timedelta(minutes=2)
    old_end = old_start + datetime.timedelta(seconds=20)
    old_production = (struct.pack(">IH", 12345, 8) + "花型A".encode() + bcd_time(old_start) +
                      struct.pack(">I", 200) + bcd_time(old_end) + struct.pack(">I", 230) + struct.pack(">H", 0))
    connection.sendall(frame(0x0B2A, 0x000B, old_production))
    assert read_frame(connection)[23] == 0
    time.sleep(0.2)
    with sqlite3.connect(database_path) as database:
        records = database.execute("SELECT id,pieces,stitches FROM production_records WHERE device_id=?", (device["id"],)).fetchall()
        salaries = database.execute("SELECT source_production_id,salary FROM salary_records WHERE device_id=?", (device["id"],)).fetchall()
    assert len(records) == 2 and sorted(row[2] for row in records) == [20, 30], records
    assert len(salaries) == 2 and {row[0] for row in salaries} == {row[0] for row in records}, salaries
    assert all(row[1] == 2.5 for row in salaries), salaries
    salary = api("GET", "/statistics/salary", token=token)["data"]
    assert salary["total"] == 2 and salary["totalPieces"] == 2 and salary["totalSalary"] == 5, salary
    detail = api("GET", "/statistics/salary/detail?employeeId=1", token=token)["data"]
    assert len(detail) == 2 and {item["sewCount"] for item in detail} == {1}, detail
    fallback = api("GET", "/statistics/salary?startDate=2020-01-01&endDate=2020-01-02&fallbackLatest=1",
                   token=token)["data"]
    assert fallback["total"] == 2 and fallback["fallbackRange"]["endDate"] == datetime.date.today().isoformat(), fallback
    api("PUT", "/pattern/" + str(production_pattern["id"]), {"unitPrice": 3}, token)
    assert api("GET", "/statistics/salary", token=token)["data"]["totalSalary"] == 6
    api("PUT", "/pattern/" + str(production_pattern["id"]), {"unitPrice": 2.5}, token)
    pattern_stats = api("GET", "/statistics/device-patterns?deviceId=" + str(device["id"]), token=token)["data"]
    assert len(pattern_stats["patternTable"]) == 1 and pattern_stats["patternTable"][0]["totalPieces"] == 2, pattern_stats
    assert len(pattern_stats["detailTable"]) == 2, pattern_stats
    process = api("GET", "/statistics/process?deviceId=" + str(device["id"]), token=token)["data"]
    assert process["total"] == 2 and process["overview"]["totalPieces"] == 2, process
    assert process["list"][0]["patternSewCount"] == 2, process
    duration = api("GET", "/statistics/duration?deviceId=" + str(device["id"]), token=token)["data"]
    assert duration["total"] == 2 and duration["runningTime"] > 0, duration
    assert len(duration["durationPie"]) == 3 and len(duration["deviceStats"]) == 1, duration
    dashboard = api("GET", "/statistics/dashboard?deviceId=" + str(device["id"]), token=token)["data"]
    assert dashboard["totalPieces"] == 2 and dashboard["scopeDeviceCount"] == 1, dashboard
    assert len(dashboard["hourlyProduction"]) == 7 and dashboard["totalAlarmCount"] == 1, dashboard
    alarms = api("GET", "/statistics/alarm?deviceId=" + str(device["id"]), token=token)["data"]
    assert alarms["total"] == 1 and alarms["resolvedRate"] == 100, alarms
    assert alarms["list"][0]["alarmCode"] == "E.025", alarms
    for endpoint in ("device-patterns", "process", "duration", "alarm"):
        empty = api("GET", "/statistics/" + endpoint + "?deviceIds=999999", token=token)["data"]
        assert not empty.get("list") and not empty.get("patternTable"), (endpoint, empty)
        by_name = api("GET", "/statistics/" + endpoint + "?deviceName=" +
                      urllib.parse.quote("不存在的设备"), token=token)["data"]
        assert not by_name.get("list") and not by_name.get("patternTable"), (endpoint, by_name)
        if endpoint == "duration":
            assert by_name["totalTime"] == 0, by_name
    missing_salary = api("GET", "/statistics/salary?deviceName=" +
                         urllib.parse.quote("不存在的设备"), token=token)["data"]
    assert missing_salary["total"] == 0, missing_salary
    today = datetime.date.today().isoformat()
    with sqlite3.connect(database_path) as database:
        cursor = database.execute(
            "INSERT INTO production_records(device_id,pattern_id,pattern_no,pattern_name,pieces,running_time,"
            "record_date,created_at) VALUES(?,?,?,?,?,?,?,?)",
            (device["id"], production_pattern["id"], 777, "花型A", 1, 0.5,
             today + " 09:00:00", today + " 09:15:00"))
        timeless_id = cursor.lastrowid
    inferred = api("GET", "/statistics/device-patterns?deviceIds=" + str(device["id"]), token=token)["data"]
    timeless = next(item for item in inferred["detailTable"] if item["id"] == timeless_id)
    assert timeless["startTime"] == today + " 09:15:00" and timeless["endTime"] == today + " 09:45:00", timeless
    with sqlite3.connect(database_path) as database:
        database.execute("DELETE FROM production_records WHERE id=?", (timeless_id,))
    for export_type in ("salary", "process", "duration", "alarm"):
        request = urllib.request.Request(base + "/statistics/export/" + export_type + "?deviceId=" + str(device["id"]),
                                         headers={"Authorization": "Bearer " + token})
        with urllib.request.urlopen(request, timeout=5) as response:
            csv_text = response.read().decode("utf-8-sig")
            assert response.headers["Content-Type"].startswith("text/csv") and len(csv_text.splitlines()) >= 2, csv_text
    request = urllib.request.Request(base + "/statistics/export/salary?mode=merged",
                                     headers={"Authorization": "Bearer " + token})
    with urllib.request.urlopen(request, timeout=5) as response:
        merged_csv = response.read().decode("utf-8-sig")
        assert "员工工号" in merged_csv and "100001" in merged_csv and "5" in merged_csv, merged_csv
    with sqlite3.connect(database_path) as database:
        database.execute(
            "INSERT INTO alarm_records(device_id,alarm_code,alarm_type,description,status,start_time,end_time,duration) "
            "VALUES(?,?,?,?,?,?,?,?)",
            (device["id"], "TEST", "测试重叠报警", "测试重叠报警", "resolved",
             (start + datetime.timedelta(seconds=5)).strftime("%Y-%m-%d %H:%M:%S"),
             (start + datetime.timedelta(seconds=10)).strftime("%Y-%m-%d %H:%M:%S"), 5))
    overlap = api("GET", "/statistics/process?deviceId=" + str(device["id"]), token=token)["data"]
    assert any(item["alarmInfo"] == "测试重叠报警" for item in overlap["list"]), overlap
    connection.sendall(frame(0x0B29, 0x0032, b"\x00"))
    pattern_file = b"DOWNLOAD-SMOKE" * 30
    pattern_row = upload_pattern(token, pattern_file)
    api("POST", "/pattern/download", {"patternId": pattern_row["id"], "deviceIds": [device["id"]]}, token)
    command = read_frame(connection)
    assert command[13:17] == struct.pack(">HH", 0x0B2A, 4), command
    connection.sendall(frame(0x0B2A, 4))
    chunks = []
    for index in range(2):
        chunk = read_frame(connection)
        assert chunk[13:17] == struct.pack(">HH", 0x0B2A, 5), chunk
        chunks.append(chunk[23:-2])
        connection.sendall(frame(0x0B2A, 6, b"\x00"))
    assert b"".join(chunks) == pattern_file, chunks
    connection.sendall(frame(0x0B2A, 5, b"\x00"))
    time.sleep(0.2)
    queue = api("GET", "/pattern/queue", token=token)["data"]
    assert queue["total"] == 1 and queue["list"][0]["status"] == "completed", queue
    refreshed = {}
    def refresh_files():
        refreshed["response"] = api("POST", "/pattern/device-files/refresh?deviceId=" + str(device["id"]), {}, token)
    refresh_thread = threading.Thread(target=refresh_files)
    refresh_thread.start()
    list_request = read_frame(connection)
    assert list_request[13:17] == struct.pack(">HH", 0x0B2A, 9), list_request
    entry = struct.pack(">H", 77) + "中文花型.dst".encode("utf-16le").ljust(44, b"\0")
    connection.sendall(frame(0x0B2A, 9, entry))
    refresh_thread.join(timeout=5)
    assert refreshed["response"]["code"] == 0, refreshed
    files = api("GET", "/pattern/device-files?deviceId=" + str(device["id"]), token=token)["data"]
    assert files["total"] == 1 and files["list"][0]["patternNo"] == 77, files
    assert files["list"][0]["fileName"] == "中文花型.dst", files
    api("POST", "/pattern/device-files/upload",
        {"deviceId": device["id"], "fileIds": [files["list"][0]["id"]]}, token)
    upload_command = read_frame(connection)
    assert upload_command[13:17] == struct.pack(">HH", 0x0B2A, 7), upload_command
    connection.sendall(frame(0x0B2A, 7, b"\x00"))
    uploaded_content = b"DEVICE-UPLOAD" * 30
    for index, chunk in enumerate((uploaded_content[:200], uploaded_content[200:]), start=1):
        connection.sendall(frame(0x0B2A, 0x000D, chunk, total=2, index=index))
        ack = read_frame(connection)
        assert ack[13:17] == struct.pack(">HH", 0x0B2A, 0x000D) and ack[23] == 0, ack
    connection.sendall(frame(0x0B2A, 7, b"\x02"))
    time.sleep(0.2)
    uploaded = api("GET", "/pattern/upload-queue", token=token)["data"]
    assert uploaded["total"] == 1 and uploaded["list"][0]["status"] == "completed", uploaded
    uploaded_id = uploaded["list"][0]["patternId"]
    request = urllib.request.Request(base + f"/pattern/{uploaded_id}/file",
                                     headers={"Authorization": "Bearer " + token})
    with urllib.request.urlopen(request, timeout=5) as response:
        assert response.read() == uploaded_content
    try:
        api("POST", "/pattern/device-files/upload",
            {"deviceId": device["id"], "fileIds": [files["list"][0]["id"]]}, token)
        raise AssertionError("same-name upload should report a conflict")
    except urllib.error.HTTPError as error:
        assert error.code == 409 and json.load(error)["data"]["duplicates"]
    api("POST", "/pattern/device-files/upload",
        {"deviceId": device["id"], "fileIds": [files["list"][0]["id"]],
         "conflictMode": "rename",
         "renameNames": {str(files["list"][0]["id"]): "中文花型新版"}}, token)
    rename_command = read_frame(connection)
    assert rename_command[13:17] == struct.pack(">HH", 0x0B2A, 7), rename_command
    connection.sendall(frame(0x0B2A, 7, b"\x00"))
    connection.sendall(frame(0x0B2A, 0x000D, b"RENAMED-UPLOAD"))
    assert read_frame(connection)[23] == 0
    connection.sendall(frame(0x0B2A, 7, b"\x02"))
    time.sleep(0.2)
    patterns = api("GET", "/pattern/list?keyword=" + urllib.parse.quote("中文花型新版"), token=token)["data"]
    assert patterns["total"] == 1 and patterns["list"][0]["name"] == "中文花型新版", patterns
    removed = {}
    def delete_file():
        removed["response"] = api("DELETE", "/pattern/device-files/" + str(files["list"][0]["id"]), token=token)
    delete_thread = threading.Thread(target=delete_file)
    delete_thread.start()
    delete_command = read_frame(connection)
    assert delete_command[13:17] == struct.pack(">HH", 0x0B2A, 8), delete_command
    connection.sendall(frame(0x0B2A, 8, b"\x00"))
    delete_thread.join(timeout=5)
    assert removed["response"]["code"] == 0, removed
    files = api("GET", "/pattern/device-files?deviceId=" + str(device["id"]), token=token)["data"]
    assert files["total"] == 0, files
    board = api("POST", "/device",
                {"code": "manual-board", "name": "板卡设备", "mainboardSn": "BOARD-1"}, token)["data"]
    with socket.create_connection(("127.0.0.1", tcp_port), timeout=5) as another:
        another.settimeout(5)
        assert read_frame(another)[13:17] == struct.pack(">HH", 0x0B2A, 2)
        another.sendall(frame(0x0B2A, 2))
        assert read_frame(another)[13:17] == struct.pack(">HH", 0x0B2A, 2)
        another.sendall(frame(0x1302, 0x157C, b"BOARD-1"))
        time.sleep(0.2)
        rebound = api("GET", "/device/" + str(board["id"]), token=token)["data"]
        assert rebound["code"] == "manual-board" and rebound["mainboardSn"] == "BOARD-1", rebound
        with sqlite3.connect(database_path) as database:
            pending_count = database.execute(
                "SELECT COUNT(*) FROM devices WHERE code LIKE 'PENDING-%' AND deleted_at IS NULL").fetchone()[0]
        assert pending_count == 0, pending_count
    api("POST","/employee",{"code":"100002","name":"新人","phone":"13900138000","groupId":1},token)
    api("PUT", "/device/" + str(device["id"]),
        {"employeeCode": "100002", "employeeName": "新人"}, token)
    assert read_frame(connection)[13:17] == struct.pack(">HH", 0x0B2A, 0x00F1)
    connection.sendall(frame(0x0B2A, 0x00F1, b"\x00"))
    historical_alarms = api("GET", "/statistics/alarm?deviceId=" + str(device["id"]), token=token)["data"]
    original_alarm = next(item for item in historical_alarms["list"] if item["alarmCode"] == "E.025")
    assert original_alarm["employeeCode"] == "100001" and original_alarm["employeeName"] == "张三", original_alarm
    api("POST", "/pattern/download",
        {"patternId": pattern_row["id"], "deviceIds": [device["id"]]}, token)
    assert read_frame(connection)[13:17] == struct.pack(">HH", 0x0B2A, 4)
    connection.sendall(frame(0x0B2A, 4))
    for _ in range(2):
        assert read_frame(connection)[13:17] == struct.pack(">HH", 0x0B2A, 5)
        connection.sendall(frame(0x0B2A, 6, b"\x00"))
    connection.sendall(frame(0x0B2A, 5, "错误花型".encode("utf-16le") + b"\x00"))
    time.sleep(0.2)
    queue = api("GET", "/pattern/queue", token=token)["data"]
    assert any(item["status"] == "failed" and "名称" in item["message"] for item in queue["list"]), queue
print("DT protocol smoke: identity merge, alarms, production, statistics, CSV, transfers, conflicts and device files passed")
