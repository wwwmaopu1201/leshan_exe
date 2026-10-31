"""Publish final interaction results only from passing latest evidence."""
import collections
import json
from pathlib import Path
root=Path(__file__).resolve().parents[3]
report=root/'docs/qt-customer-regression-2026-10-23'
artifacts=report/'interaction-compatibility-2026-10-24'
suites={}
for line in (report/'suites.jsonl').read_text().splitlines():
 item=json.loads(line);suites[item['case']]=item
expected=[f'interaction-{side}-{platform}-{scale}' for platform in ('offscreen','cocoa') for scale in ('1','2') for side in ('client','server')]
expected+=['large_device_consistency_cases.py','crash-recovery','storage_failure_cases.py']
assert all(name in suites and suites[name]['exit']==0 for name in expected)
assert all(x['exit']==0 for x in suites.values()),[(k,v['exit']) for k,v in suites.items() if v['exit']]
checks={}
for line in (report/'row-results.jsonl').read_text().splitlines():
 item=json.loads(line)
 if item['name'].startswith(('交互补测 ','异常退出第','上传目录无法创建时','失败上传不生成','修复临时目录后','故障恢复后仅有一条','大数据API总数2250','大数据API树不截断','显式设备全模型','扩大设备树读取后仍保持','设备全模型保持','无设备模块权限账号','大数据真实客户端专项入口')):
  checks[(item['row'],item['name'])]=item
assert checks and all(x['result']=='PASS' for x in checks.values()),[x for x in checks.values() if x['result']!='PASS']
with (artifacts/'检查明细.jsonl').open('w') as out:
 for key in sorted(checks):out.write(json.dumps(checks[key],ensure_ascii=False)+'\n')
count=len(checks);profiles=sum(x['name'].startswith('交互补测 ') and '实际API' not in x['name'] and '点击超过原2000' not in x['name'] for x in checks.values())
text=f'''# Qt交互、数据规模与异常恢复补测（2026-10-24）

新增{count}个同名去重检查，最后结果全部通过，覆盖原表345、430、438、474、482。检查次数包含两端、平台、缩放等组合，**不代表新增{count}条客户问题或全平台验收**。客户端/服务端交互8组配置、2250设备真实接口与客户端、三轮进程异常退出、上传目录故障专项均退出0。当前全部{len(suites)}个有记录套件的同名最后结果均退出0，历史失败保留。

## 已执行与修复

| 项目 | 实际验证与结果 |
| --- | --- |
| 连续通知 | 成功→错误、错误→成功、5条突发通知及各自到期隐藏；修复客户端旧singleShot提前隐藏新提示，统一重启单一计时器 |
| 键盘提交 | 两端真实设备/员工表单、中英文、共享快速输入；花型多文件上传通过输入框Enter实际提交；修复默认按钮父级注册顺序及关闭×抢默认行为，保留取消按钮获焦Enter取消 |
| 输入法事件 | Qt预编辑、中文/emoji提交、取消预编辑、数值框拒绝中文；不是操作系统拼音候选窗口实测 |
| 焦点与模态 | Tab/ShiftTab、嵌套确认Esc只关内层、恢复原输入焦点、关闭后恢复主窗；Cocoa原生owner阻挡正确释放；修复以QPointer与owner上下文恢复原焦点，避免已销毁窗口和新模态框竞争 |
| 保留输入 | 脏表单切换中英文保留输入，取消丢弃确认后继续编辑；确认丢弃后退出 |
| 最小化/恢复 | 每个交互配置连续3轮，窗口边界与输入保持；未主动锁屏或让用户电脑休眠 |
| 快速关闭 | 每配置离屏30次、Cocoa8次；验证对象/遮罩/原生阻挡释放；3轮真实客户端在迟到HTTP响应前销毁，无销毁后回调 |
| 文件对话框 | 客户端真实保存框Esc取消、不创建文件、焦点回归 |
| 大表格 | 客户端2000条合成行、长中文/emoji元数据、滚动到末行、冻结操作列、勾选ID、中英文与缩放保留选择；本应用局部深色palette下仍按现有浅色样式显示 |
| 服务端1225设备 | 真实数据库、100条分页、跨页勾选、树1225、最后25条包含最早ID1、搜索d0；修复本地模型与树默认1000条截断 |
| 客户端2250设备 | 真实Qt服务端API、两页2000+250、树2250、实际客户端本地模型2250；点击原2000条范围外ID1仍显示资料。新增显式fullModel读取，普通分页响应契约保留；相关监控/统计/花型设备元数据读取同步使用 |
| 权限 | 有deviceInfo权限账号按既有规则跨分组查看完整2250台；无模块权限账号对树及fullModel均403。账号分组仅用于组织管理，不擅改业务可见规则 |
| 进程异常退出 | 三轮仅强制终止自有临时服务进程；每轮SQLite integrity_check正常，重启重新登录可读取所有已确认写入的分组/设备/员工，关联及前导零工号完整 |
| 上传路径故障 | 仅临时目录用文件占用目标上传目录，返回500及目录错误、无虚假花型记录；恢复目录后无需重启可重传且下载字节一致，无需产品改动 |

## 8组交互配置

| 配置 | 最后退出码 |
| --- | ---: |
'''
for case in expected[:8]:text+=f'| {case} | 0 |\n'
text+='''
标准CTest：客户端5/5、服务端4/4通过。关联回归：API、设备分组、客户端目录员工、统计、花型多文件上传、40项冲突分页及原生服务端文件操作均通过。

## 仍需真实环境验证

| 未覆盖项 | 原因 / 验证边界 |
| --- | --- |
| Windows系统DPI、大字体、多屏迁移、远程桌面 | 本机是ARM macOS；Qt缩放和小屏约束不能替代Windows系统环境 |
| 系统拼音/第三方输入法候选窗 | 注入QInputMethodEvent只证明控件提交与校验；需要实际系统输入法交互，未作为已通过 |
| 真实睡眠、锁屏恢复 | 会中断当前电脑及测试，需专用测试机与唤醒/解锁条件；本轮仅最小化恢复 |
| 系统深色/高对比度/辅助功能 | 只改应用局部palette，未切系统偏好；不宣称已适配系统深色、高对比度或屏幕阅读器 |
| 磁盘满、真实ACL/网络共享权限、物理断电 | 测试目录不可创建与SIGKILL不是这些故障的替代；需要受控磁盘/权限或专用机器 |
| 远超2250设备、长期运行、并发性能与内存趋势 | 本轮验证完整性与交互，无万级设备/长时间/多客户端性能基准；完整模型读取仍消耗相应内存与传输量 |
| 本轮新增全量SQL的MySQL/SQL Server规模复测 | 本轮1225/2250规模使用SQLite；旧MySQL同步与业务测试通过不能替代新增规模复测。SQL Server用户明确不授权，未启动 |
| 实机计件/远控/图形预览 | 急停计件和员工绑定按用户要求保持，等待客户；远控缺屏幕协议、预览缺可靠SDK/格式资料，继续保留原限制 |

## 过程纠正和证据

- 大数据基线真实复现1000条树/模型、客户端2000条截断；失败记录保留，同名最终复测PASS。
- 测试最初误把导航索引当数据表索引，已改按实际table定位；角色permissions按既有API应传JSON字符串；此前错误假设“账号分组限制数据”与旧Go明确合同不符，已纠正并在汇总脚本忽略这两项错误断言，原始日志保留。未据此修改业务权限规则。
- 到期提示检查原先可能在同步刷新阻塞Qt事件循环后、已到期计时器尚未分发时立即断言；补入最多1秒事件分发等待，前面的“新提示不能提前消失”严格检查保留。最终8组均通过。
- [逐项检查明细](interaction-compatibility-2026-10-24/检查明细.jsonl)、各配置日志和9张截图保留；已查看截图总览，确认长表格末行、1225计数和早期设备资料。
- 每条检查完成即追加并刷新row-results.jsonl；台账由证据重建。195按注明范围通过/243部分/4模拟实机待验/23受限暂缓非缺陷/11空项保持不变，未判定及待修复失败0。
- 测试数据均为本次隔离临时库；原表、正式数据和已有无测试参数客户端未主动改动。本轮未启动数据库容器，未接受SQL Server许可。代码尚未提交或打包Windows安装程序。
'''
(report/'交互与异常恢复补测-2026-10-24.md').write_text(text)
print(json.dumps(dict(checks=count,suites=len(suites),rows=sorted({x['row'] for x in checks.values()}))))
