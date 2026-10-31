"""Pattern validation and server-file round trips, with customer row evidence."""
import json
from pathlib import Path
import sys
import urllib.error
import urllib.request

base=f'http://127.0.0.1:{sys.argv[1]}/api'
token=''
failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'


def call(method,path,body=None):
    request=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,
                                   {'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
    try:
        with urllib.request.urlopen(request,timeout=10) as response:return response.status,json.load(response)
    except urllib.error.HTTPError as error:return error.code,json.load(error)


def upload(filename,fields,content=b'PATTERN-CONTENT'):
    boundary='customer-pattern-boundary'
    pieces=[]
    for key,value in fields.items():
        pieces.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode())
    pieces.append(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{filename}"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()+content+b'\r\n')
    pieces.append(f'--{boundary}--\r\n'.encode())
    request=urllib.request.Request(base+'/pattern/upload',b''.join(pieces),
                                   {'Content-Type':'multipart/form-data; boundary='+boundary,'Authorization':'Bearer '+token})
    try:
        with urllib.request.urlopen(request,timeout=10) as response:return response.status,json.load(response)
    except urllib.error.HTTPError as error:return error.code,json.load(error)


def check(row,name,operation,expect):
    global failures
    try:actual=operation();passed=expect(actual)
    except Exception as error:actual,passed=repr(error),False
    with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',actual=actual,
                                                     scope='Qt服务端真实HTTP/文件接口；临时数据，非设备回传'),ensure_ascii=False)+'\n')
    print(row,'PASS' if passed else 'FAIL',name,flush=True)
    failures+=not passed


token=call('POST','/auth/login',dict(username='admin',password='admin123'))[1]['data']['token']
for extension in ('txt','png','pdf','exe'):
    check(165,'拒绝非花型格式.'+extension,lambda:upload('bad.'+extension,{'name':'不合法文件'}),lambda x:x[0]==400)
check(166,'明确传空花型名称应拒绝上传',lambda:upload('empty-name.ntp',{'name':'   '}),lambda x:x[0]==400)
check(167,'超255字花型名称拒绝上传',lambda:upload('long.ntp',{'name':'花'*256}),lambda x:x[0]==400)
for key,value,row in [('patternType','类'*101,159),('orderNo','订'*101,162),('stitches','99999999999999999999',160),
                       ('stitches','-1',160),('stitches','1.5',160),('unitPrice','9999999999999',161),('unitPrice','-0.1',161),('unitPrice','nan',161)]:
    check(row,key+'边界值上传校验'+value[:12],lambda:upload('invalid.ntp',{'name':'校验花型',key:value}),lambda x:x[0]==400)
pattern=upload('original.NTP',dict(name='完整花型',patternType='测试类型',stitches=72000,unitPrice=1.235,orderNo='ORDER1'))[1]['data']
identifier=pattern['id']
for key,value,row in [('name','',166),('name','花'*256,167),('patternType','类'*101,168),('orderNo','订'*101,169),
                       ('stitches',2147483648,160),('stitches',-1,160),('unitPrice',1e10,161),('unitPrice',-1,161)]:
    check(row,key+'编辑边界校验',lambda:call('PUT','/pattern/'+str(identifier),{key:value}),lambda x:x[0]==400)
check(156,'按花型类型筛选返回相应花型',lambda:call('GET','/pattern/list?patternType='+urllib.parse.quote('测试类型')),lambda x:x[1]['data']['total']==1 and x[1]['data']['list'][0]['id']==identifier)
check(157,'合法名称和7.2万针正常保存',lambda:call('GET','/pattern/'+str(identifier)),lambda x:x[1]['data']['name']=='完整花型' and x[1]['data']['stitches']==72000)
def download():
    request=urllib.request.Request(base+'/pattern/'+str(identifier)+'/file',headers={'Authorization':'Bearer '+token})
    with urllib.request.urlopen(request) as response:return response.read().decode(),response.headers.get('Content-Disposition','')
check(197,'服务器NTP文件下载保持字节和扩展名（不含设备回传）',download,lambda x:x[0]=='PATTERN-CONTENT' and '.NTP' in x[1])
check(168,'非法批量编辑拒绝且不修改已有工价',lambda:(call('POST','/pattern/batch-update',dict(ids=[identifier],unitPrice=-1)),call('GET','/pattern/'+str(identifier))),lambda x:x[0][0]==400 and x[1][1]['data']['unitPrice']==1.235)
sys.exit(1 if failures else 0)
