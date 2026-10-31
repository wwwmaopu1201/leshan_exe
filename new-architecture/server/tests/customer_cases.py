"""Customer workbook cases; row numbers refer to Sheet1 of the supplied workbook."""
import json
from pathlib import Path
import sys
import time
import urllib.request
import urllib.error

base = f'http://127.0.0.1:{sys.argv[1]}/api'
token = ''
failures = 0
report = Path(__file__).resolve().parents[3] / 'docs/qt-customer-regression-2026-10-23/customer-cases.jsonl'


def call(method, path, body=None):
    request = urllib.request.Request(base + path, json.dumps(body).encode() if body is not None else None,
                                     {'Content-Type': 'application/json', 'Authorization': 'Bearer ' + token},
                                     method=method)
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)


def check(name, rows, operation, expected):
    global failures
    try:
        actual = operation()
        passed = expected(actual)
    except Exception as error:
        actual, passed = repr(error), False
    with report.open('a', encoding='utf-8') as out:
        out.write(json.dumps(dict(name=name, rows=rows, result='PASS' if passed else 'FAIL',
                                  actual=actual), ensure_ascii=False) + '\n')
    print(('PASS ' if passed else 'FAIL ') + name, flush=True)
    failures += not passed


token = call('POST', '/auth/login', {'username': 'admin', 'password': 'admin123'})[1]['data']['token']
for route, rows in [('/device/type-summary', [128,130,131,137,139]),
                     ('/device/electric-control-type-summary', [142,143]),
                     ('/pattern/type-summary', [232,233,235,238,240,243]),
                     ('/pattern/order-summary', [246,247,249,252,254,256])]:
    check(route + ' 空名称拒绝', rows, lambda: call('POST', route, {'value': '  '}), lambda x: x[0] == 400)
    call('POST', route, {'value': 'CustomerA'})
    call('POST', route, {'value': 'CustomerB'})
    check(route + ' 重复新增拒绝', rows, lambda: call('POST', route, {'value': 'CustomerA'}), lambda x: x[0] == 400)
    check(route + ' 新增后有更新时间', rows, lambda: call('GET', route + '?keyword=CustomerA'),
          lambda x: x[0] == 200 and len(x[1]['data']) == 1 and x[1]['data'][0]['updateTime'] not in ('-', '', None))
    check(route + ' 重命名为已存在名称拒绝', rows,
          lambda: call('PUT', route, {'oldValue': 'CustomerA', 'newValue': 'CustomerB'}), lambda x: x[0] == 400)
    check(route + ' 关键字搜索排除无关项', rows, lambda: call('GET', route + '?keyword=not-present'), lambda x: x[1]['data'] == [])
    check(route + ' 超100字符拒绝', rows, lambda: call('POST', route, {'value': 'x'*101}), lambda x: x[0] == 400)
    check(route + ' 重命名超100字符拒绝', rows, lambda: call('PUT', route, {'oldValue': 'CustomerA', 'newValue': 'x'*101}), lambda x: x[0] == 400)
    time.sleep(1.05)
    call('POST', route, {'value': 'CustomerZ'})
    check(route + ' 按更新时间倒序', rows, lambda: call('GET', route + '?keyword=Customer'),
          lambda x: x[1]['data'][0]['value'] == 'CustomerZ')

for name, value, rows in [('空设备名', '', [104,106,450]), ('空格设备名', '   ', [104,106,450]),
                          ('超长设备名', '设' * 101, [79,105,450])]:
    check(name, rows, lambda: call('POST', '/device', {'code': name, 'name': value}), lambda x: x[0] == 400)
device = call('POST', '/device', {'code': 'customer-device', 'name': '客户测试设备'})[1]['data']
check('同组设备重名新增拒绝', [80,107,451], lambda: call('POST', '/device', {'code': 'customer-device-2', 'name': '客户测试设备'}), lambda x: x[0] == 400)
other = call('POST', '/device', {'code': 'customer-device-3', 'name': '另一台设备'})[1]['data']
check('同组设备重名编辑拒绝', [80,107,451], lambda: call('PUT', '/device/' + str(other['id']), {'name': '客户测试设备'}), lambda x: x[0] == 400)
check('离线设备花型列表不显示历史', [188,216,217], lambda: call('GET', '/pattern/device-files?deviceId=' + str(device['id'])), lambda x: x[1]['data']['list'] == [] and x[1]['data']['offline'])
check('离线设备刷新明确失败', [188,216,218], lambda: call('POST', '/pattern/device-files/refresh', {'deviceId': device['id']}), lambda x: x[0] == 400)
for field, value, row in [('email', 'testexample.com', 420), ('email', 'a'*101+'@x.com',421), ('nickname', 'a'*51,419), ('password', '',416)]:
    body = dict(username='customer1', password='test123', nickname='测试', role='user', phone='13800138000')
    body[field] = value
    check('账号校验 ' + field + str(row), [row], lambda: call('POST', '/user', body), lambda x: x[0] == 400)
check('账号带空格手机号规范化', [423], lambda: call('POST', '/user', dict(username='customer2',password='test123',nickname='测试',role='user',phone='138 0013 8000')), lambda x: x[0] == 200 and x[1]['data']['phone'] == '13800138000')
for role in call('GET', '/role/list')[1]['data']:
    if role['name'] in ('admin','user'):
        check('默认角色不可删除 ' + role['name'], [408], lambda: call('DELETE', '/role/' + str(role['id'])), lambda x: x[0] == 400)
for path in ['/statistics/production-trend', '/statistics/alarm', '/statistics/process', '/statistics/duration', '/statistics/salary']:
    check(path + ' 非法日期拒绝', [39,43,45,277,279], lambda: call('GET', path + '?startDate=2026-99-99&endDate=2026-10-23'), lambda x: x[0] == 400)
    check(path + ' 超一年跨度拒绝', [45], lambda: call('GET', path + '?startDate=2020-01-01&endDate=2026-10-23'), lambda x: x[0] == 400)
for code, name, phone, row in [('123456789012', '测试', '13800138000', 311),
                               ('100', '长'*9, '13800138000',320),
                               ('100', '测试', 'abc',312)]:
    check('员工导入错误校验 ' + str(row), [row], lambda: call('POST','/employee/import', {'groupId':1,'employees':[dict(code=code,name=name,phone=phone)]}), lambda x: x[0] == 200 and x[1]['data']['successCount'] == 0 and len(x[1]['data']['errors']) == 1)
check('员工导入带空格手机号', [312], lambda: call('POST','/employee/import', {'groupId':1,'employees':[dict(code='100',name='测试',phone='138 0013 8000')]}), lambda x: x[1]['data']['successCount'] == 1)
employees = call('GET','/employee/list')[1]['data']['list']
employee_id = next(e['id'] for e in employees if e['code'] == '100')
call('DELETE','/employee/' + str(employee_id))
check('员工删除后重新导入', [319], lambda: call('POST','/employee/import', {'groupId':1,'employees':[dict(code='100',name='测试',phone='13800138000')]}), lambda x: x[1]['data']['successCount'] == 1)
sys.exit(1 if failures else 0)
