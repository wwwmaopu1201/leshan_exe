"""User-confirmed unique employee phones and ASCII alphanumeric order numbers."""
import json
from pathlib import Path
import sys
import urllib.error
import urllib.request
base=f'http://127.0.0.1:{sys.argv[1]}/api';token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
def call(method,path,body=None,raw=None,content='application/json'):
 req=urllib.request.Request(base+path,raw if raw is not None else json.dumps(body).encode() if body is not None else None,{'Content-Type':content,'Authorization':'Bearer '+token},method=method)
 try:
  with urllib.request.urlopen(req,timeout=10) as response:return response.status,json.load(response)
 except urllib.error.HTTPError as error:return error.code,json.load(error)
def data(method,path,body=None):
 status,value=call(method,path,body);assert status==200,(status,value);return value['data']
def check(row,name,passed,actual=''):
 global failures
 with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',actual=actual,scope='用户确认规则；真实Qt服务端HTTP+临时数据库，覆盖新增/修改/导入/恢复/花型元数据'),ensure_ascii=False)+'\n')
 print(row,'PASS' if passed else 'FAIL',name,flush=True);failures+=not passed
token=data('POST','/auth/login',dict(username='admin',password='admin123'))['token']
a=data('POST','/employee',dict(code='1001',name='员工甲',phone='13800138000',groupId=1))['id']
b=data('POST','/employee',dict(code='1002',name='员工乙',phone='13900138000',groupId=1))['id']
result=call('POST','/employee',dict(code='1003',name='重复',phone='138 0013 8000',groupId=1))
check(313,'新增拒绝其他员工重复手机号，空格规范化后仍判重复',result[0]==400 and '手机号' in result[1]['message'],result)
result=call('PUT','/employee/'+str(b),dict(phone='13800138000'))
check(313,'修改为他人手机号被拒绝且原号码保持不变',result[0]==400 and data('GET','/employee/'+str(b))['phone']=='13900138000')
check(313,'员工修改自身资料可以保留自己的手机号',call('PUT','/employee/'+str(a),dict(name='甲更新',phone='138 0013 8000'))[0]==200)
body=dict(groupId=1,overwriteExisting=True,employees=[dict(code='1002',name='乙覆盖',phone='13800138000')])
result=data('POST','/employee/import',body)
check(313,'覆盖导入也拒绝手机号冲突且保留原资料',result['successCount']==0 and len(result['errors'])==1 and data('GET','/employee/'+str(b))['name']=='员工乙',result)
result=data('POST','/employee/import',dict(groupId=1,employees=[dict(code='1004',name='导入甲',phone='13700138000'),dict(code='1005',name='导入乙',phone='137 0013 8000')]))
check(313,'同一批导入两个相同手机号只成功第一行并报告第二行',result['successCount']==1 and len(result['errors'])==1,result)
data('DELETE','/employee/'+str(a));c=data('POST','/employee',dict(code='1006',name='复用号码',phone='13800138000',groupId=1))['id']
result=data('POST','/employee/import',dict(groupId=1,employees=[dict(code='1001',name='恢复甲',phone='13800138000')]))
check(313,'恢复已删除员工时也检查被活跃员工占用的手机号',result['successCount']==0 and data('GET','/employee/'+str(c))['name']=='复用号码',result)
user=dict(username='phoneuser',nickname='账号手机号',password='123456',role='user')
result=call('POST','/user',user);check(424,'新建账号手机号必填，与客户端个人资料要求一致',result[0]==400 and '手机号' in result[1]['message'],result)
user['phone']='138 0013 8000';account=data('POST','/user',user)
result=call('PUT','/user/'+str(account['id']),dict(phone=''));check(424,'编辑账号禁止清空手机号，保留原规范化号码',result[0]==400 and data('GET','/user/'+str(account['id']))['phone']=='13800138000',result)
route='/pattern/order-summary';data('POST',route,dict(value='Order001'))
for bad in ('订单','A-B','A_B','A B',' A1','A1 ','A\tB','A\nB','１２３','abc!'):
 result=call('POST',route,dict(value=bad));check(163,'新增订单拒绝非法编号 '+repr(bad),result[0]==400,result)
for valid in ('123','abc','ABCxyz009','000001'):
 result=call('POST',route,dict(value=valid));check(163,'订单允许字母数字且保留前导零 '+valid,result[0]==200 and result[1]['data']['value']==valid,result)
result=call('PUT',route,dict(oldValue='Order001',newValue='New-001'));check(257,'单条订单改名拒绝特殊符号且原名称保留',result[0]==400 and any(x['value']=='Order001' for x in data('GET',route)))
result=call('PUT',route,dict(renames=[dict(oldValue='Order001',newValue='Valid001'),dict(oldValue='abc',newValue='含汉字')]))
check(253,'批量订单改名包含非法项时整批不修改',result[0]==400 and any(x['value']=='Order001' for x in data('GET',route)) and not any(x['value']=='Valid001' for x in data('GET',route)))
def upload(order):
 boundary='confirmed-order-test';body=b''
 for key,value in dict(name='规则花型',orderNo=order).items():body+=f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode()
 body+=f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="rule.NTP"\r\nContent-Type: application/octet-stream\r\n\r\nRULE\r\n--{boundary}--\r\n'.encode()
 return call('POST','/pattern/upload',raw=body,content='multipart/form-data; boundary='+boundary)
for value in ('A-B',' A1','订单'):
 result=upload(value);check(163,'上传元数据拒绝非法订单 '+repr(value),result[0]==400,result)
result=upload('Order001');assert result[0]==200,result;identifier=result[1]['data']['id']
for value in ('A-B','A B',' A1','A1 ','订单',123):
 result=call('PUT','/pattern/'+str(identifier),dict(orderNo=value));check(169,'花型修改拒绝非法订单 '+repr(value),result[0]==400 and data('GET','/pattern/'+str(identifier))['orderNo']=='Order001',result)
result=call('POST','/pattern/batch-update',dict(ids=[identifier],orderNo='A-B'))
check(169,'花型批量修改拒绝非法订单且保留原值',result[0]==400 and data('GET','/pattern/'+str(identifier))['orderNo']=='Order001',result)
check(169,'订单元数据允许留空以清除关联',call('PUT','/pattern/'+str(identifier),dict(orderNo=''))[0]==200)
sys.exit(1 if failures else 0)
