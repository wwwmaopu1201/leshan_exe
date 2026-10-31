"""Build the window report only when every expected final profile/check passed."""
import collections
import csv
import json
from pathlib import Path

root=Path(__file__).resolve().parents[3]
report=root/'docs/qt-customer-regression-2026-10-23'
artifacts=report/'window-compatibility-2026-10-24'
checks={};suites={}
for line in (report/'row-results.jsonl').read_text().splitlines():
    item=json.loads(line)
    if item['name'].startswith('窗口兼容'):checks[item['name']]=item
for line in (report/'suites.jsonl').read_text().splitlines():
    item=json.loads(line);suites[item['case']]=item
expected=[]
for platform,scales in [('offscreen',['1','1.25','1.5','2']),('cocoa',['1','2'])]:
    for scale in scales:
        for side in ('client','server','server-login'):
            expected.append(f'window-{side}-{platform}-{scale}')
assert all(x['exit']==0 for x in suites.values()),[(k,x['exit']) for k,x in suites.items() if x['exit']]
assert len(expected)==18
assert all(name in suites and suites[name]['exit']==0 for name in expected),[(x,suites.get(x,{}).get('exit')) for x in expected]
assert checks and all(x['result']=='PASS' for x in checks.values()),[x for x in checks.values() if x['result']!='PASS']
with (artifacts/'检查明细.jsonl').open('w') as out:
    for name in sorted(checks):out.write(json.dumps(checks[name],ensure_ascii=False)+'\n')
counts=collections.Counter(x['row'] for x in checks.values())
text=f'''# Qt窗口兼容性补测（2026-10-24）

客户端、服务端主界面及独立服务端登录窗共18组配置全部通过。共{len(checks)}项几何/滚动/状态断言，按同名最后结果汇总均通过；这是重复页面、语言、尺寸、缩放的检查次数，**不是新增{len(checks)}条客户问题**。原表345/482仍保留部分通过，未冒充Windows全平台验收。

## 实际测试范围

| 项目 | 范围 |
| --- | --- |
| 客户端 | 21页面，含首页、看板、设备、花型、统计、员工、个人资料、服务支持；另测登录页 |
| 服务端 | 9页面；另用真实进程启动独立登录窗 |
| 语言 | 中文、英文 |
| 请求窗口尺寸 | 客户端1000×650、1120×700、1280×720、1366×768、1600×900、1920×1080；服务端主界面从1120×700起的后5档 |
| 离屏Qt缩放 | 100%、125%、150%、200%，两端窗口及服务端登录逐组运行 |
| 原生macOS | Cocoa平台100%、200% Qt缩放；实际窗口、登录及弹窗。100%时可用逻辑屏幕1800×1067，200%时900×534，记录以日志为准 |
| 状态 | 初次显示屏幕边界、最大化/恢复、低屏双轴滚动可达右下角；离屏100%/200%另测Qt全屏与退出恢复 |
| 弹窗 | 客户端实际新增员工；服务端实际新增设备/账号/角色，中英文；检查标题、底部按钮、主窗变化后的遮罩 |
| 弹层 | 屏幕右下角长选项下拉框，25项滚动访问末项，日期弹层 |
| 提示 | 实际客户端通知/服务端反馈函数生成长提示，验证完整换行及窗口变化后的边界和高度 |

页面布局按上述请求尺寸验证；首次显示和恢复窗口另外按屏幕实际可用区域验证。没有修改系统显示分辨率。Qt缩放因子是测试变量，不等于已经在Windows设置中切换系统DPI。

## 修复和确认

1. 花型页工具栏由单行改为换行布局；修复1000～1366宽下整页横向溢出。表格自身的横向滚动保留，用于访问较多列。
2. 英文首页卡片标题、联系页面地址允许换行，避免把整页撑宽。
3. 主窗口首次显示、最大化恢复及屏幕变更时按可用区域约束；屏幕小于原最小布局时，使用滚动容器访问全部内容。不会通过缩小字体强塞所有字段。
4. 独立服务端登录窗纳入同一适配；小屏首次优先显示登录表单，避免只显示左侧装饰。客户端登录的整块输入框和按钮也验证可通过滚动访问。
5. 弹窗按可用屏幕和主窗口交集限制尺寸、居中；空间不足时表单区域滚动，保存/取消保持可达。遮罩随主窗变化覆盖整窗。
6. 下拉弹层限制最大宽高并校正顶部/底部位置；长选项省略并保留完整提示，末项可滚动访问。
7. 长提示按窗口宽度换行，高度随内容变化，窗口变化和语言切换时重算。清除原固定38像素高度导致多行裁切的限制。
8. 修正本轮弹窗压缩的初版标题策略：截图发现标题被挤窄，已改正，并加入标题可读宽度断言，最终截图复核通过。
9. 修正自动化设置隔离：Qt命名设置构造原先仍可能使用原生偏好；现显式尊重临时INI配置。日常运行仍使用原生设置，不主动清空用户已有偏好。

登录页测试修正了滚动判定：确保输入框完整矩形进入视口，而不只保证文本光标附近可见；也等待布局稳定后断言。花型长名称回归还修正了模拟树点击应先设置当前项，保留原来的省略、Hover全文和按钮边界断言。失败历史保留，未将这类夹具问题混同为业务数据缺陷。

## 18组配置结果

| 套件 | 最后退出码 |
| --- | ---: |
'''
for case in expected:text+=f'| {case} | 0 |\n'
text+='''
客户端标准CTest5/5、服务端4/4通过。目录/员工、真实统计、花型、40项冲突分页、服务端设备文件操作专项及登录认证/关闭重开回归也通过，具体以`suites.jsonl`同名最后结果为准。

## 截图与完整记录

- [原生200%弹窗总览](window-compatibility-2026-10-24/native-200-dialogs-overview.png)
- [原生200%登录页总览](window-compatibility-2026-10-24/native-200-login-overview.png)
- [逐项检查明细](window-compatibility-2026-10-24/检查明细.jsonl)
- 全部截图/套件日志位于`window-compatibility-2026-10-24/`。`before-`图片和`initial-before.log`为修复前留存，不能当作当前结果。
- 原始即时记录仍在`row-results.jsonl`；逐条原表台账为`客户问题逐条记录.csv`。全局UI行的证据单元格仅汇总次数和路径，完整检查没有丢弃。

## 尚未覆盖

未实测Windows真实125%/150%/200%系统缩放、Windows字体差异、不同显示器间拖动后的DPI迁移，也未在macOS切换独立全屏桌面空间。Qt虚拟全屏检查不替代系统全屏空间验收。

极小逻辑屏幕采用滚动访问，不承诺所有内容同时显示在一屏。矩阵中的大部分页面使用预览/合成布局数据；实际业务数据操作另由上述业务回归补充，不能据此宣称任意数据量和任意字体全部验收。

原客户表和正式数据库未修改；本轮没有启动SQL Server或改变暂缓的员工绑定、急停计件规则。
'''
(report/'窗口兼容性报告-2026-10-24.md').write_text(text)
print('Verified',len(checks),'checks across 18 profiles; client/server source row counts:',dict(counts))
