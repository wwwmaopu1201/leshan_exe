"""URL reserved characters must be decoded as complete query values."""
import json
from pathlib import Path
import sys
import urllib.error
import urllib.parse
import urllib.request
base=f'http://127.0.0.1:{sys.argv[1]}/api';token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
def call(method,path,body=None):
 req=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,{'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
 try:
  with urllib.request.urlopen(req,timeout=8) as response:return response.status,json.load(response)
 except urllib.error.HTTPError as error:return error.code,json.load(error)
def data(method,path,body=None):
 status,result=call(method,path,body);assert status==200,(status,result);return result['data']
def check(row,name,passed,actual=''):
 global failures
 with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',actual=actual,scope='真实Qt服务端HTTP；百分号编码的完整查询值；登录失败日志/目录/设备搜索，不使用实际客户账号'),ensure_ascii=False)+'\n')
 print(row,'PASS' if passed else 'FAIL',name,flush=True);failures+=not passed
token=data('POST','/auth/login',dict(username='admin',password='admin123'))['token']
for value in ('bad!','bad+plus','bad&and','bad?query','bad#hash','bad%percent','bad_under','bad/slash','坏账号!'):
 result=call('POST','/auth/login',dict(username=value,password='wrong-password'));assert result[0] in (400,401),(value,result)
 rows=data('GET','/auth/login-logs?'+urllib.parse.urlencode(dict(keyword=value,pageSize=100),quote_via=urllib.parse.quote))
 check(475,'登录日志特殊字符按字面精确搜索 '+repr(value),len(rows['list'])==1 and rows['list'][0]['username']==value,rows)
for value in ('类型!+&?#%_/','测试 中文'):
 data('POST','/device/type-summary',dict(value=value));rows=data('GET','/device/type-summary?'+urllib.parse.urlencode(dict(keyword=value),quote_via=urllib.parse.quote))
 check(134,'类型目录保留特殊符号并正确解码搜索 '+repr(value),len(rows)==1 and rows[0]['value']==value,rows)
 device=data('POST','/device',dict(code='special'+str(len(value)),name=value))
 rows=data('GET','/device/list?'+urllib.parse.urlencode(dict(keyword=value),quote_via=urllib.parse.quote))
 check(96,'设备名含保留字符时搜索不丢失原值 '+repr(value),len(rows['list'])==1 and rows['list'][0]['id']==device['id'],rows)
sys.exit(1 if failures else 0)
