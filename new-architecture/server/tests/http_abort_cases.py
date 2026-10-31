"""Interrupted sockets/uploads must release request state and temporary files."""
import socket
from pathlib import Path
import sys
import time
from supplement_helpers import CustomerRun,wait_for
r=CustomerRun(sys.argv[1],'非Windows补测：真实HTTP/TCP断开连接及临时上传目录清理；只在本机测试端口')
port=int(sys.argv[1]);folder=Path(sys.argv[3]).parent
half=[]
for i in range(64):
 peer=socket.create_connection(('127.0.0.1',port),timeout=3);peer.sendall(b'GET /api/healthz HTTP/1.1\r\nHost: local\r\nX-Unfinished: ');half.append(peer)
r.check(482,'64条未完成请求并存时正常客户端仍可读取健康接口',r.call('GET','/healthz')[0]==200)
for peer in half:peer.close()
uploads=[]
try:
 for i in range(24):
  peer=socket.create_connection(('127.0.0.1',port),timeout=3)
  header=f'POST /api/pattern/upload HTTP/1.1\r\nHost: local\r\nAuthorization: Bearer {r.token}\r\nContent-Type: multipart/form-data; boundary=abort\r\nContent-Length: 100000\r\n\r\n'.encode();peer.sendall(header+b'--abort\r\nPARTIAL');uploads.append(peer)
 wait_for(lambda:len(list(folder.glob('pattern-upload-*.tmp')))==24)
 r.check(482,'24个中断前上传各有独立临时文件且不阻塞健康接口',len(list(folder.glob('pattern-upload-*.tmp')))==24 and r.call('GET','/healthz')[0]==200)
finally:
 for peer in uploads:peer.close()
wait_for(lambda:not list(folder.glob('pattern-upload-*.tmp')))
r.check(482,'上传连接中断后所有临时文件自动删除且不生成花型记录',not list(folder.glob('pattern-upload-*.tmp')) and r.data('GET','/pattern/list')['total']==0)
for i in range(40):
 with socket.create_connection(('127.0.0.1',port),timeout=3) as peer:
  peer.sendall(b'GET /api/healthz HTTP/1.1\r\nHost: local\r\nX-Large: '+b'a'*66000)
  try:peer.recv(1)
  except (ConnectionResetError,BrokenPipeError):pass
r.check(482,'连续40条超限头请求被关闭后服务仍正常且数据未污染',r.call('GET','/healthz')[0]==200 and r.data('GET','/pattern/list')['total']==0)
sys.exit(int(bool(r.failures)))
