"""ENOSPC only on an owned 32MiB mounted disk image, never the host volume."""
import errno
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
from supplement_helpers import CustomerRun
r=CustomerRun(sys.argv[1],'非Windows补测：本次专用32MB磁盘映像真实ENOSPC；服务端上传与Qt客户端QSaveFile原文件保护')
mount=Path('/private/tmp/boer-qt-customer/nonwindows-full-volume')
assert os.path.ismount(mount) and os.stat(mount).st_dev!=os.stat(mount.parent).st_dev,'must be a separately mounted test image'
st=os.statvfs(mount);assert st.f_blocks*st.f_frsize<64*1024*1024,'test volume must be bounded below 64MiB'
target=Path(tempfile.mkdtemp(prefix='boer-owned-',dir=mount));preserved=target/'preserved.ntp';preserved.write_bytes(b'ORIGINAL')
folder=Path(sys.argv[3]).parent/'uploads';folder.mkdir(exist_ok=True);link=folder/'patterns';assert not link.exists();link.symlink_to(target,target_is_directory=True)
filler=target/'filler.bin';payload=bytes(range(256))*1024
boundary='disk-full'
body=(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="full.NTP"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()+payload+f'\r\n--{boundary}--\r\n'.encode())
def upload():
 req=urllib.request.Request(r.base+'/pattern/upload',body,{'Authorization':'Bearer '+r.token,'Content-Type':'multipart/form-data; boundary='+boundary})
 try:
  with urllib.request.urlopen(req,timeout=15) as res:return res.status,json.load(res)
 except urllib.error.HTTPError as err:return err.code,json.load(err)
try:
 full=False;written=0
 with filler.open('wb',buffering=0) as out:
  try:
   for i in range(1024):written+=out.write(bytes(65536))
   out.flush();os.fsync(out.fileno())
  except OSError as error:
   if error.errno!=errno.ENOSPC:raise
   full=True
 r.check(482,'专用32MB虚拟卷实际写入返回ENOSPC，宿主卷未填充',full,dict(bytesWritten=written,freeBytes=os.statvfs(mount).f_bavail*os.statvfs(mount).f_frsize))
 status,result=upload();r.check(482,'真实磁盘满时服务端上传失败且无虚假记录或半文件',status==500 and r.data('GET','/pattern/list')['total']==0 and {x.name for x in target.iterdir()}=={'filler.bin','preserved.ntp'},result)
 log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/nonwindows-reliability-2026-10-24/client-diskfull.log'
 with log.open('w') as out:
  ran=subprocess.run(['/private/tmp/boer-qt-customer/client/boer-qt-client.app/Contents/MacOS/boer-qt-client','--ui-self-test'],env=dict(os.environ,BOER_TEST_NETWORK_RESILIENCE='1',BOER_TEST_FULL_DISK_TARGET=str(preserved)),stdout=out,stderr=subprocess.STDOUT,timeout=40)
 r.check(345,'真实磁盘满Qt客户端下载专项实际执行且通过',ran.returncode==0 and '真实虚拟卷磁盘满下载明确失败并保留原文件 PASS' in log.read_text())
 filler.unlink()
 status,result=upload();r.check(482,'释放虚拟卷空间后无需重启即可重新上传成功',status==200 and result.get('code')==0,result)
 if status==200:
  req=urllib.request.Request(r.base+f"/pattern/{result['data']['id']}/file",headers={'Authorization':'Bearer '+r.token})
  with urllib.request.urlopen(req,timeout=10) as res:actual=res.read()
  r.check(482,'磁盘满恢复后文件256KB逐字节一致且原文件保留',actual==payload and preserved.read_bytes()==b'ORIGINAL' and r.data('GET','/pattern/list')['total']==1)
finally:
 if link.is_symlink():link.unlink()
 shutil.rmtree(target)
sys.exit(int(bool(r.failures)))
