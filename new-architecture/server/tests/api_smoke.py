"""Run against a disposable --headless server: python3 tests/api_smoke.py [port]."""

import json
import io
import http.client
import sys
import urllib.error
import urllib.request
import zipfile

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18088
BASE = f"http://127.0.0.1:{PORT}/api"
token = None


def call(method, path, body=None):
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(
        BASE + path,
        data=json.dumps(body).encode() if body is not None else None,
        headers=headers,
        method=method,
    )
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)


def upload(path, filename, content, fields=None):
    boundary = b"boer-smoke-boundary"
    parts = []
    for key, value in (fields or {}).items():
        parts.append(b"--" + boundary + b"\r\nContent-Disposition: form-data; name=\"" +
                     key.encode() + b"\"\r\n\r\n" + str(value).encode() + b"\r\n")
    parts.append(b"--" + boundary + b"\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" +
                 filename.encode() + b"\"\r\nContent-Type: application/octet-stream\r\n\r\n" + content + b"\r\n")
    parts.append(b"--" + boundary + b"--\r\n")
    request = urllib.request.Request(BASE + path, data=b"".join(parts),
                                     headers={"Content-Type": "multipart/form-data; boundary=" + boundary.decode(),
                                              "Authorization": "Bearer " + token}, method="POST")
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.load(response)


status, data = call("GET", "/healthz")
assert status == 200 and data["code"] == 0
status, data = call("GET", "/system/firewall/status")
assert status == 200 and "needsRepair" in data["data"], data
request = urllib.request.Request(BASE + "/system/firewall/repair", data=b"",
                                 headers={"Origin": "https://example.com"}, method="POST")
try:
    urllib.request.urlopen(request, timeout=5)
    raise AssertionError("cross-origin firewall repair should be forbidden")
except urllib.error.HTTPError as error:
    assert error.code == 403
status, data = call("GET", "/group/list")
assert status == 401
status, data = call("POST", "/auth/login", {"username": "admin", "password": "wrong"})
assert status == 401
status, data = call("POST", "/auth/login", {"username": "admin", "password": "admin123"})
assert status == 200 and data["code"] == 0, data
token = data["data"]["token"]
avatar = upload("/auth/avatar", "portrait.png", b"\x89PNG\r\nsmoke")["data"]["avatar"]
with urllib.request.urlopen(f"http://127.0.0.1:{PORT}" + avatar, timeout=5) as response:
    assert response.read() == b"\x89PNG\r\nsmoke"
pattern = upload("/pattern/upload", "sample.dst", b"DST-SMOKE", {"name": "测试花型", "stitches": 12})["data"]
assert pattern["name"] == "测试花型" and pattern["fileSize"] == 9, pattern
chunked_body = (b"--boer-chunked-boundary\r\nContent-Disposition: form-data; name=\"name\"\r\n\r\n"
                + "分块花型".encode() + b"\r\n--boer-chunked-boundary\r\n"
                b"Content-Disposition: form-data; name=\"file\"; filename=\"chunked.dst\"\r\n"
                b"Content-Type: application/octet-stream\r\n\r\nCHUNKED-DST\r\n"
                b"--boer-chunked-boundary--\r\n")
connection = http.client.HTTPConnection("127.0.0.1", PORT, timeout=10)
connection.putrequest("POST", "/api/pattern/upload")
connection.putheader("Authorization", "Bearer " + token)
connection.putheader("Content-Type", "multipart/form-data; boundary=boer-chunked-boundary")
connection.putheader("Transfer-Encoding", "chunked")
connection.endheaders()
for part in (chunked_body[:31], chunked_body[31:97], chunked_body[97:]):
    connection.send(f"{len(part):X}\r\n".encode() + part + b"\r\n")
connection.send(b"0\r\n\r\n")
response = connection.getresponse()
chunked_pattern = json.load(response)["data"]
assert response.status == 200 and chunked_pattern["fileSize"] == 11, chunked_pattern
connection.close()
status, data = call("DELETE", "/pattern/" + str(chunked_pattern["id"]))
assert status == 200 and data["code"] == 0, data
with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/uploads/patterns/" + pattern["fileName"], timeout=5) as response:
    assert response.read() == b"DST-SMOKE"
request = urllib.request.Request(BASE + f"/pattern/{pattern['id']}/file",
                                 headers={"Authorization": "Bearer " + token})
with urllib.request.urlopen(request, timeout=5) as response:
    assert response.read() == b"DST-SMOKE"
request = urllib.request.Request(BASE + "/pattern/files/download",
                                 headers={"Authorization": "Bearer " + token})
with urllib.request.urlopen(request, timeout=5) as response:
    with zipfile.ZipFile(io.BytesIO(response.read())) as archive:
        assert archive.read("测试花型.dst") == b"DST-SMOKE"
status, data = call("POST", "/pattern/batch-update", {"ids": [pattern["id"]], "unitPrice": 1.25})
assert status == 200 and data["data"]["affected"] == 1, data
status, data = call("GET", "/pattern/" + str(pattern["id"]))
assert status == 200 and data["data"]["unitPrice"] == 1.25, data
status, data = call("GET", "/auth/userinfo")
assert status == 200 and data["data"]["username"] == "admin"
status, data = call("GET", "/system/stats")
assert status == 200 and "onlineDeviceCount" in data["data"] and "employeeCount" in data["data"]
status, data = call("GET", "/system/network")
assert status == 200 and isinstance(data["data"], list)
status, data = call("GET", "/system/database/config")
assert status == 200 and data["data"]["dbType"] == "mysql", data
status, data = call("POST", "/system/database/config",
                    {"dbType": "mysql", "host": "127.0.0.1", "port": 3306,
                     "username": "smoke", "database": "smoke", "enabled": False})
assert status == 200 and data["data"]["enabled"] is False, data
status, data = call("GET", "/system/database/sync-status")
assert status == 200 and data["data"]["status"] == "disabled", data
status, data = call("POST", "/system/database/test",
                    {"dbType": "mysql", "host": "127.0.0.1", "port": 3306,
                     "username": "smoke", "database": "smoke"})
assert status == 200 and data["code"] == 1 and data["data"]["success"] is False, data
status, data = call("POST", "/system/config", {"key": "smoke_setting", "value": "hello", "desc": "smoke"})
assert status == 200, data
status, data = call("GET", "/system/config?key=smoke_setting")
assert status == 200 and data["data"]["value"] == "hello", data
status, data = call("POST", "/system/logs", {"level": "info", "message": "smoke log"})
assert status == 200, data
connection = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
connection.request("POST", "/api/system/logs",
                   body=iter([b'{"level":"info",', b'"message":"chunked smoke"}']),
                   headers={"Content-Type": "application/json", "Authorization": "Bearer " + token},
                   encode_chunked=True)
response = connection.getresponse()
chunked_result = json.load(response)
connection.close()
assert response.status == 200 and chunked_result["data"]["message"] == "chunked smoke", chunked_result
status, data = call("GET", "/system/logs?level=info")
assert status == 200 and any(item["message"] == "smoke log" for item in data["data"]), data
status, data = call("POST", "/system/config", {"key": "debug_output_enabled", "value": "false"})
assert status == 200, data
status, data = call("POST", "/system/logs", {"level": "info", "message": "should not save"})
assert status == 200 and data["message"] == "调试输出已关闭", data
status, data = call("GET", "/statistics/production-trend?startDate=2026-09-25&endDate=2026-09-27")
assert status == 200 and len(data["data"]["productionTrend"]) == 3, data
status, data = call("GET", "/statistics/production-trend?startDate=2026-13-25")
assert status == 400
status, data = call("GET", "/statistics/home")
assert status == 200 and len(data["data"]["weeklyEfficiency"]) == 7, data
status, data = call("POST", "/employee/import", {"groupId": 1, "employees": [
    {"code": "1001", "name": "测试员工", "phone": "13800138000"},
    {"code": "1002", "name": "无效号码", "phone": "bad"}]})
assert status == 200 and data["data"]["successCount"] == 1 and len(data["data"]["errors"]) == 1, data
status, data = call("GET", "/employee/export?groupId=1")
assert status == 200 and any(item["code"] == "1001" for item in data["data"]), data
employee_id = next(item["id"] for item in data["data"] if item["code"] == "1001")
status, data = call("PUT", f"/employee/{employee_id}", {"department": "研发"})
assert status == 400, data
status, data = call("POST", "/operator/import", {"operators": [
    {"username": "test_op", "password": "pass1234", "groupId": 1},
    {"username": "bad-name", "password": "pass1234"}]})
assert status == 200 and data["data"]["successCount"] == 1 and len(data["data"]["errors"]) == 1, data
status, data = call("GET", "/operator/export")
assert status == 200 and any(item["username"] == "test_op" and "password" not in item for item in data["data"]), data

status, data = call("POST", "/group", {"name": "smoke-group"})
assert status == 200 and data["data"]["name"] == "smoke-group", data
group_id = data["data"]["id"]
status, data = call("POST", "/group", {"name": "smoke-group"})
assert status == 400, data
status, data = call("POST", "/role", {"name": "12345678901"})
assert status == 400, data
status, data = call("GET", "/group/tree")
assert status == 200 and any(child["id"] == group_id for root in data["data"] for child in root["children"])
status, data = call("POST", "/device/type-summary", {"value": "TestType"})
assert status == 200
status, data = call("PUT", "/device/type-summary", {"oldValue": "TestType", "newValue": "RenamedType"})
assert status == 200
status, data = call("GET", "/device/types")
assert status == 200 and "RenamedType" in data["data"]
status, data = call("DELETE", "/device/type-summary", {"value": "RenamedType"})
assert status == 200

status, data = call("POST", "/device", {"code": "smoke-001", "name": "Test Device", "ip": "127.0.0.2", "groupId": group_id})
assert status == 200 and data["data"]["code"] == "smoke-001", data
device_id = data["data"]["id"]
status, data = call("POST", "/device", {"code": "other-002", "name": "Other Device", "groupId": 1})
assert status == 200, data
other_device_id = data["data"]["id"]
status, data = call("PUT", f"/device/{device_id}", {"name": ""})
assert status == 400, data
status, data = call("POST", "/pattern/download", {"patternId": pattern["id"], "deviceIds": [device_id]})
assert status == 200, data
status, data = call("GET", "/pattern/queue")
assert status == 200 and data["data"]["total"] == 1, data
task_id = data["data"]["list"][0]["id"]
status, data = call("POST", f"/pattern/queue/{task_id}/pause")
assert status == 200, (data,call("GET","/pattern/queue"))
status, data = call("POST", f"/pattern/queue/{task_id}/resume")
assert status == 200, data
status, data = call("DELETE", f"/pattern/queue/{task_id}")
assert status == 200, data
status, data = call("GET", "/device/list?groupId=" + str(group_id))
assert status == 200 and data["data"]["total"] >= 1
status, data = call("GET", "/device/list?name=Test%20Device")
assert status == 200 and data["data"]["total"] == 1 and data["data"]["list"][0]["displayName"] == "Test Device", data
status, data = call("POST", "/user", {"username": "smokeuser", "password": "test1234", "nickname": "测试账号", "phone": "13800138000", "role": "user", "groupId": group_id})
assert status == 200, data
user_id = data["data"]["id"]
status, data = call("PUT", f"/user/{user_id}", {"username": "changed"})
assert status == 400, data
status, data = call("GET", "/user/all?keyword=%E6%B5%8B%E8%AF%95%E8%B4%A6%E5%8F%B7")
assert status == 200 and any(item["nickname"] == "测试账号" for item in data["data"]), data
admin_token = token
status, data = call("POST", "/auth/login", {"username": "smokeuser", "password": "test1234"})
assert status == 200, data
token = data["data"]["token"]
status, data = call("GET", "/user/list")
assert status == 403
status, data = call("GET", "/device/list")
assert status == 200 and data["data"]["total"] == 2
status, data = call("GET", "/pattern/list")
assert status == 200 and data["data"]["total"] == 1, data
own_pattern = upload("/pattern/upload", "own.dst", b"OWN-DST")["data"]
status, data = call("GET", "/pattern/list")
assert status == 200 and data["data"]["total"] == 2 and data["data"]["list"][0]["id"] == own_pattern["id"], data
token = admin_token
limited_permissions = json.dumps({
    "deviceManagement": True, "deviceInfo": False, "remoteMonitoring": True,
    "fileManagement": True, "patternFiles": False,
    "devicePatternFiles": False, "downloadLog": True,
})
status, data = call("POST", "/role", {"name": "limited", "permissions": limited_permissions})
assert status == 200, data
limited_role_id = data["data"]["id"]
status, data = call("POST", "/user", {
    "username": "limited1", "password": "test1234", "nickname": "权限账号", "phone": "13900138000",
    "role": "limited", "groupId": group_id,
})
assert status == 200, data
limited_user_id = data["data"]["id"]
status, data = call("POST", "/auth/login", {"username": "limited1", "password": "test1234"})
assert status == 200, data
token = data["data"]["token"]
assert call("GET", "/device/list")[0] == 200
assert call("GET", "/device/types")[0] == 403
assert call("POST", "/device", {"code": "forbidden", "name": "Forbidden"})[0] == 403
assert call("GET", "/pattern/list")[0] == 403
assert call("GET", "/pattern/queue")[0] == 200
token = admin_token
status, data = call("PUT", f"/device/{device_id}", {"name": "Updated Device"})
assert status == 200 and data["data"]["name"] == "Updated Device"
status, data = call("DELETE", f"/device/{device_id}")
assert status == 200
status, data = call("GET", f"/device/{device_id}")
assert status == 200 and data["data"]["groupId"] is None
status, data = call("DELETE", f"/device/{device_id}/hard")
assert status == 200
status, data = call("DELETE", f"/device/{other_device_id}/hard")
assert status == 200
status, data = call("GET", f"/device/{device_id}")
assert status == 404
status, data = call("DELETE", "/user", {"ids": [user_id]})
assert status == 200
status, data = call("DELETE", "/user", {"ids": [limited_user_id]})
assert status == 200
status, data = call("DELETE", f"/role/{limited_role_id}")
assert status == 200
status, data = call("DELETE", f"/group/{group_id}")
assert status == 200
print("API smoke: health, auth, uploads, system, statistics, import/export, permissions and CRUD passed")
