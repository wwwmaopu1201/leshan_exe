"""Device/group rows, exercised one operation at a time against a fresh Qt server."""
import json
from pathlib import Path
import sys
import urllib.error
import urllib.parse
import urllib.request

base=f'http://127.0.0.1:{sys.argv[1]}/api'
token=''
failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'


def call(method,path,body=None):
    request=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,
                                   {'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
    try:
        with urllib.request.urlopen(request,timeout=10) as response: return response.status,json.load(response)
    except urllib.error.HTTPError as error: return error.code,json.load(error)


def data(method,path,body=None): return call(method,path,body)[1]['data']


def check(row,name,operation,expect):
    global failures
    try:
        actual=operation(); passed=expect(actual)
    except Exception as error: actual,passed=repr(error),False
    with log.open('a') as out:
        out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',actual=actual,
                                  scope='Qt服务端真实HTTP接口；全新临时SQLite库，非界面点击'),ensure_ascii=False)+'\n')
    print(row,'PASS' if passed else 'FAIL',name,flush=True)
    failures+=not passed


token=data('POST','/auth/login',dict(username='admin',password='admin123'))['token']
g1=data('POST','/group',dict(name='客户分组1',parentId=1))['id']
g2=data('POST','/group',dict(name='客户分组2',parentId=1))['id']
check(70,'实体分组下可以正常新增子分组',lambda:call('POST','/group',dict(name='缝制',parentId=g1)),lambda x:x[0]==200 and x[1]['data']['parentId']==g1)
check(69,'指定总分组为父级时保持实际父级ID',lambda:call('GET','/group/'+str(g1)),lambda x:x[1]['data']['parentId']==1)
check(71,'拒绝将虚拟未分组节点用作实体父级',lambda:call('POST','/group',dict(name='错误子组',parentId='ungrouped')),lambda x:x[0]==400)
root=data('POST','/group',dict(name='实体顶级组',parentId=None))['id']
check(82,'明确选择顶级时父级保存为空，与默认总分组同级',lambda:call('GET','/group/'+str(root)),lambda x:x[1]['data']['parentId'] is None)
alias_root=data('POST','/device/group',dict(name='客户端顶级组',parentId=None))['id']
check(87,'客户端分组路由也保留顶级父级为空',lambda:call('GET','/group/'+str(alias_root)),lambda x:x[1]['data']['parentId'] is None)
check(80,'客户端分组路由同样拒绝同级重名新增',lambda:call('POST','/device/group',dict(name='实体顶级组',parentId=None)),lambda x:x[0]==400)
check(80,'客户端分组路由重命名也拒绝同级重名',lambda:call('PUT','/device/group/'+str(alias_root),dict(name='实体顶级组')),lambda x:x[0]==400)
check(71,'客户端分组路由同样拒绝虚拟父级',lambda:call('POST','/device/group',dict(name='非法父级',parentId='ungrouped')),lambda x:x[0]==400)
root_child=data('POST','/group',dict(name='顶级组的子级',parentId=root))['id']
root_device=data('POST','/device',dict(code='root-line',name='顶级生产线设备',groupId=root_child))['id']
check(21,'新增实体顶级组下设备生产线归属顶级组而非误跳到子级',lambda:call('GET','/statistics/home'),
      lambda x:next(row for row in x[1]['data']['deviceUsage'] if row['deviceId']==root_device)['lineId']==root)
parent=g2
for level in range(10):
    child=data('POST','/group',dict(name='层级'+str(level),parentId=parent))
    check(90,'第'+str(level+1)+'级子分组父级正确',lambda:child,lambda x:x['parentId']==parent)
    parent=child['id']
check(74,'空分组可删除',lambda:call('DELETE','/group/'+str(parent)),lambda x:x[0]==200)
d1=data('POST','/device',dict(code='device-A',name='哈哈哈',groupId=g1))['id']
d2=data('POST','/device',dict(code='device-B',name='板号设备',mainboardSn='board-token',groupId=g2))['id']
check(76,'中文设备名编辑后原文回读，无乱码',lambda:call('PUT','/device/'+str(d1),dict(name='中文设备甲')),lambda x:x[0]==200 and x[1]['data']['name']=='中文设备甲')
for value,label in [('', '空'),('   ','空格'),('设'*101,'超长')]:
    check(79,label+'设备名不能保存',lambda:call('PUT','/device/'+str(d1),dict(name=value)),lambda x:x[0]==400)
check(112,'设备备注超过255字符拒绝',lambda:call('PUT','/device/'+str(d1),dict(remark='备'*256)),lambda x:x[0]==400)
check(95,'设备名子串模糊匹配返回正确设备',lambda:call('GET','/device/list?keyword='+urllib.parse.quote('中文设备')),lambda x:x[1]['data']['total']==1 and x[1]['data']['list'][0]['id']==d1)
check(96,'用户确认双向匹配：较长关键词也匹配包含的短设备名',lambda:call('GET','/device/list?keyword='+urllib.parse.quote('中文设备甲甲')),lambda x:x[1]['data']['total']==1 and x[1]['data']['list'][0]['id']==d1)
check(97,'当前明确标注多字段搜索，主板号匹配正确设备',lambda:call('GET','/device/list?keyword=board-token'),lambda x:x[1]['data']['total']==1 and x[1]['data']['list'][0]['id']==d2)
check(117,'设备移出分组后保留用户重命名',lambda:(call('DELETE','/device/'+str(d1)),call('GET','/device/'+str(d1))),lambda x:x[0][0]==200 and x[1][1]['data']['name']=='中文设备甲' and x[1][1]['data']['groupId'] is None)
check(91,'可以将另一分组设备移动到当前组',lambda:call('POST','/device/move',dict(deviceIds=[d2],groupId=g1)),lambda x:x[0]==200)
check(91,'移动后设备确实属于目标分组',lambda:call('GET','/device/'+str(d2)),lambda x:x[1]['data']['groupId']==g1)
check(91,'包含不存在设备的批量移动拒绝，不能假成功',lambda:call('POST','/device/move',dict(deviceIds=[d2,999999],groupId=g2)),lambda x:x[0] in (400,404))
check(91,'失败的批量移动不应部分修改真实设备',lambda:call('GET','/device/'+str(d2)),lambda x:x[1]['data']['groupId']==g1)
left=data('POST','/device',dict(code='duplicate-A',name='重名设备',groupId=g1))['id']
right=data('POST','/device',dict(code='duplicate-B',name='重名设备',groupId=g2))['id']
check(80,'批量移动也应阻止目标组设备重名',lambda:call('POST','/device/move',dict(deviceIds=[right],groupId=g1)),lambda x:x[0]==400)
clean=data('POST','/group',dict(name='待删设备组',parentId=1))['id']
resident=data('POST','/device',dict(code='resident',name='组内设备',groupId=clean))['id']
check(92,'删除无子组的设备组时将设备移至未分组',lambda:(call('DELETE','/group/'+str(clean)),call('GET','/device/'+str(resident))),lambda x:x[0][0]==200 and x[1][1]['data']['groupId'] is None)
clean2=data('POST','/group',dict(name='客户端路由分组',parentId=1))['id']
resident2=data('POST','/device',dict(code='resident2',name='客户端路由设备',groupId=clean2))['id']
check(92,'客户端device/group路由删除也将设备移至未分组',lambda:(call('DELETE','/device/group/'+str(clean2)),call('GET','/device/'+str(resident2))),lambda x:x[0][0]==200 and x[1][1]['data']['groupId'] is None)
check(74,'客户端device/group路由不能删除总分组',lambda:call('DELETE','/device/group/1'),lambda x:x[0]==400)
for path in ['/device/type-summary','/device/electric-control-type-summary']:
    data('POST',path,dict(value='重复类型'))
    check(109,path+'重复新增拒绝且无假成功',lambda:call('POST',path,dict(value='重复类型')),lambda x:x[0]==400)
    check(110,path+'超过100字符拒绝',lambda:call('POST',path,dict(value='长'*101)),lambda x:x[0]==400)
sys.exit(1 if failures else 0)
