"""Inject an unusable upload path inside an owned temporary test data directory."""
import json
from pathlib import Path
import sys
import urllib.error
import urllib.request
from supplement_helpers import CustomerRun
r=CustomerRun(sys.argv[1],'存储故障补测：仅临时数据目录内以文件占用上传目录；含真实POSIX目录权限故障；不是磁盘满/Windows ACL模拟')
folder=Path(sys.argv[3]).parent/'uploads';folder.mkdir(exist_ok=True)
blocked=folder/'patterns'
assert not blocked.exists(), 'fixture requires an unused temporary upload path'
blocked.write_bytes(b'TEST PATH BLOCKER')
payload=b'ISOLATED-STORAGE-CONTENT'
boundary='storage-failure-test'
body=(f'--{boundary}\r\nContent-Disposition: form-data; name="name"\r\n\r\n恢复上传\r\n--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="storage.NTP"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()+payload+f'\r\n--{boundary}--\r\n'.encode())
def upload():
 req=urllib.request.Request(r.base+'/pattern/upload',body,{'Authorization':'Bearer '+r.token,'Content-Type':'multipart/form-data; boundary='+boundary})
 try:
  with urllib.request.urlopen(req,timeout=10) as response:return response.status,json.load(response)
 except urllib.error.HTTPError as error:return error.code,json.load(error)
try:
 status,result=upload()
 r.check(482,'上传目录无法创建时返回明确失败，不假报保存成功',status==500 and result.get('code')!=0 and '目录' in result.get('message',''),result)
 r.check(482,'失败上传不生成花型记录、不覆盖占位文件',r.data('GET','/pattern/list')['total']==0 and blocked.read_bytes()==b'TEST PATH BLOCKER')
 blocked.unlink()
 status,result=upload()
 r.check(482,'修复临时目录后无需重启即可重新上传成功',status==200 and result.get('code')==0,result)
 if status==200:
  identifier=result['data']['id'];req=urllib.request.Request(r.base+f'/pattern/{identifier}/file',headers={'Authorization':'Bearer '+r.token})
  with urllib.request.urlopen(req,timeout=10) as response:actual=response.read()
  r.check(482,'故障恢复后仅有一条花型且下载字节完整一致',r.data('GET','/pattern/list')['total']==1 and actual==payload)
 # Actual POSIX directory write permission failure, only inside this test's directory.
 if status==200:
  blocked.chmod(0o500)
  try:
   denied,answer=upload()
   r.check(482,'真实POSIX目录不可写时上传明确失败且已有记录不受损',denied==500 and r.data('GET','/pattern/list')['total']==1,answer)
  finally:blocked.chmod(0o700)
  repaired,answer=upload()
  r.check(482,'恢复POSIX目录写权限后无需重启可再次成功上传',repaired==200 and r.data('GET','/pattern/list')['total']==2,answer)
finally:
 if blocked.is_file():blocked.unlink()
sys.exit(int(bool(r.failures)))
