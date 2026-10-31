"""Pattern logical names must export to valid, unique portable filenames."""
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile
base=f'http://127.0.0.1:{sys.argv[1]}/api';token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
def api(method,path,body=None):
 req=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,{'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
 with urllib.request.urlopen(req,timeout=8) as response:return json.load(response)['data']
def check(row,name,passed,actual=''):
 global failures
 with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',actual=actual,scope='真实Qt服务端HTTP下载；Python标准库独立检查ZIP CRC、路径安全及本机实际解压；不修改逻辑花型名称'),ensure_ascii=False)+'\n')
 print(row,'PASS' if passed else 'FAIL',name,actual,flush=True);failures+=not passed
def upload(name,index):
 boundary='archive-name-test';body=f'--{boundary}\r\nContent-Disposition: form-data; name="name"\r\n\r\n{name}\r\n--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="source.NTP"\r\nContent-Type: application/octet-stream\r\n\r\nPAYLOAD-{index}\r\n--{boundary}--\r\n'.encode();req=urllib.request.Request(base+'/pattern/upload',body,{'Content-Type':'multipart/form-data; boundary='+boundary,'Authorization':'Bearer '+token})
 with urllib.request.urlopen(req) as response:return json.load(response)['data']
def valid(name):return len(name.encode())<=255 and not re.search(r'[<>:"/\\|?*\x00-\x1f]',name) and not name.endswith(('.',' ')) and not re.match(r'^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)',name,re.I)
token=api('POST','/auth/login',dict(username='admin',password='admin123'))['token']
names=['花'*239+'甲','花'*239+'乙','A:B?*<test>','CON','Case','case'];records=[upload(name,i) for i,name in enumerate(names)]
req=urllib.request.Request(base+'/pattern/files/download',headers={'Authorization':'Bearer '+token})
with urllib.request.urlopen(req) as response:archive=zipfile.ZipFile(io.BytesIO(response.read()))
files=archive.namelist();check(177,'长中文及特殊符号花型批量下载文件名在跨平台限制内',all(valid(name) for name in files),[(name,len(name.encode())) for name in files])
check(177,'截断或大小写冲突后ZIP文件名仍唯一，不覆盖其他花型',len({name.casefold() for name in files})==len(records),files)
check(177,'独立ZIP校验CRC正确且导出所有原始内容',archive.testzip() is None and {archive.read(name) for name in files}=={f'PAYLOAD-{i}'.encode() for i in range(len(records))})
try:
 with tempfile.TemporaryDirectory(dir='/private/tmp/boer-qt-customer') as directory:archive.extractall(directory);actual=len(list(Path(directory).iterdir()))
 check(177,'实际解压6个导出花型成功，无名称过长错误',actual==6,actual)
except OSError as error:check(177,'实际解压6个导出花型成功，无名称过长错误',False,str(error))
for original,record in zip(names,records):
 req=urllib.request.Request(base+'/pattern/'+str(record['id'])+'/file',headers={'Authorization':'Bearer '+token})
 with urllib.request.urlopen(req) as response:download=urllib.parse.unquote(response.headers['Content-Disposition'].split("''",1)[1]);response.read()
 check(197,'单文件下载名可保存 '+str(record['id']),valid(download),(download,len(download.encode())))
 check(177,'导出不改花型业务名称 '+str(record['id']),api('GET','/pattern/'+str(record['id']))['name']==original)
sys.exit(int(bool(failures)))
