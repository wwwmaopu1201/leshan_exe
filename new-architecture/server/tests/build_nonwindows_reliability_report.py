"""Summarize only current passing evidence for the bounded non-Windows round."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[3];report=root/'docs/qt-customer-regression-2026-10-23';folder=report/'nonwindows-reliability-2026-10-24'
suites={}
for line in (report/'suites.jsonl').read_text().splitlines():
 item=json.loads(line);suites[item['case']]=item
expected=['network-client-resilience','concurrency_soak_cases.py','multi_qt_client_cases.py','http_abort_cases.py','queue_clock_cases.py','primary-mysql-queue-clock','primary-mysql-large-devices','primary-mysql-api-smoke','primary-mysql-confirmed-rules','external_mysql_cases.py','external_timer_cases.py','storage_failure_cases.py','disk_full_cases.py','database-full','crash-recovery','multi_device_transfer_cases.py','device_file_queue_ui_cases.py','pattern_ui_cases.py','client-integration','client-catalog-regression','statistics_ui_cases.py','device_stream_recovery_cases.py','device_file_list_recovery_cases.py','client-ctest-final','server-ctest-final']
assert all(name in suites and suites[name]['exit']==0 for name in expected)
assert all(item['exit']==0 for item in suites.values())
prefixes=('网络故障补测 client ','多客户端补测 ','4个真实Qt客户端','多客户端结束后','16并发新增160','16并发相同手机号','16独立登录会话','并发更新后160','300秒16会话','持续请求后数据','64条未完成请求','24个中断前上传','上传连接中断后','连续40条超限头','真实POSIX目录','恢复POSIX目录','上传目录无法创建','失败上传不生成','修复临时目录后','故障恢复后仅有一条','异常退出第','专用32MB虚拟卷','真实磁盘满时','真实磁盘满Qt','释放虚拟卷空间后','磁盘满恢复后','数据库磁盘满时','数据库释放空间后','数据库磁盘满恢复后','SQLite新建离线','SQLite新任务','SQLite真实超过','MySQL新建离线','MySQL新任务','MySQL真实超过','大数据API总数2250','大数据API树不截断','显式设备全模型','扩大设备树读取后仍保持','设备全模型保持','无设备模块权限账号','大数据真实客户端专项入口')
checks={}
for line in (report/'row-results.jsonl').read_text().splitlines():
 item=json.loads(line)
 if item['name'].startswith(prefixes) or ('交互补测 client ' in item['name'] and ('实际API' in item['name'] or '点击超过原2000' in item['name'])) or '真实Qt服务端60秒自动同步定时器' in item.get('scope','') or '真实Qt QMYSQL驱动+隔离MySQL' in item.get('scope',''):
  checks[(item['row'],item['name'])]=item
assert all(x['result']=='PASS' for x in checks.values()),[x for x in checks.values() if x['result']!='PASS']
with (folder/'检查明细.jsonl').open('w') as out:
 for key in sorted(checks):out.write(json.dumps(checks[key],ensure_ascii=False)+'\n')
metrics=json.loads((folder/'soak-metrics.json').read_text())
text=f'''# 非Windows并发、网络及存储补测（2026-10-24）

本轮重点新增及复测{len(checks)}个独立检查点，按同条同名最后结果全部通过。另完成两端标准及相关业务界面回归；当前{suites.__len__()}个已记录套件的同名最后结果均退出0。**检查次数含重复专项，不是新增客户问题数量，也不等于所有非Windows场景均已验收。** 失败、夹具修正及复测历史保留在原始日志中。

## 实测范围与修复

| 项目 | 操作和结果 |
| --- | --- |
| 客户端响应校验 | 真实Qt ApiClient收到HTTP200的HTML、数组、空对象、字符串业务码时明确失败；修复只看HTTP200就假报成功，要求合法JSON对象和数值业务码，新增中英文提示 |
| 旧响应隔离 | 切换地址前发出延迟请求，切换后旧响应不回调；修复有效服务地址变化时清空令牌并递增请求代次。退出登录后迟到请求、401仅触发一次失效、恢复地址均通过 |
| 网络故障及下载 | 真实150ms超时后可重试、503、业务失败、响应截断、退出时取消下载；QSaveFile失败保留原文件。共18个独立客户端故障检查，含受限虚拟卷下载 |
| 并发写入 | 16并发HTTP工作线程新增160设备，ID唯一；并发更新后ID/名称逐一一致。16条同手机号员工请求只有1条成功，其余400拒绝，未产生重复 |
| 持续请求 | 最终功能构建运行{metrics['durationSeconds']}秒、16个HTTP工作线程，共{metrics['requests']:,}次请求，零错误；末尾160台设备及SQLite完整性正常。该压力组使用同一管理员账号的登录令牌，不冒充16种角色 |
| 四个真实Qt客户端 | 4个独立进程、4个不同账号同时连接真实Qt服务端，40秒内轮换8个页面和中英文；每个完成约200次请求，无失败，会话及40台设备一致。是4进程，不是4台物理工作站 |
| HTTP中断清理 | 64个未完成头连接并存仍可正常请求；24个部分上传各用独立临时文件，断开后全部删除、无花型记录；40次超限头连接后服务正常 |
| 队列UTC | SQLite和真实MySQL各测新建任务跨过实际定时器仍等待、暂停/恢复，以及人工设为11分钟前的任务超时；修复UTC数据库时间被当本地时间导致任务立即过期。MySQL仅设置当前连接UTC，不修改数据库全局时区 |
| MySQL规模 | 2250台设备普通分页2000+250、全模型、树、权限以及真实客户端ID1选择一致。消除列表逐设备重复查询分组，改为一次加载分组映射；此前实际客户端6秒内未完成加载，修复后专项通过 |
| MySQL同步 | 七表全量/增量、中文emoji、前导零、软删、目标切换、只读账号拒绝同步通过；实际60秒自动同步→关闭65秒→重开下一周期增量三阶段均通过，无加速时钟 |
| 三设备故障隔离 | 一个DT模拟设备第二任务持续不响应，另外两台独立完成；实际约61秒后仅故障任务超时，已完成文件不被覆盖。真实设备固件待验 |
| 真实目录权限 | 仅临时上传目录chmod为不可写，返回失败且已有记录保持；恢复权限后无需重启可再次上传。覆盖POSIX模式位，不等于Windows ACL或客户网络共享权限 |
| 真实上传空间不足 | 专用32MB HFS+虚拟卷实际写入返回ENOSPC，服务端上传返回失败，无记录/半文件；释放空间后256KB上传和下载逐字节一致 |
| 真实下载空间不足 | Qt客户端向同一受限虚拟卷下载512KB内容，明确失败并保留原文件；没有占满宿主磁盘 |
| 真实数据库空间不足 | 临时SQLite本身放在专用32MB卷中，填满后合法设备新增失败，原设备不变且无半记录；释放空间后同请求成功，integrity_check通过。将原始SQLite英文错误改成检查路径/权限/空间的中英文提示 |
| 异常退出重启 | 三轮只终止自有测试服务进程；数据库完整性、已确认写入/关联/前导零及未过期旧令牌恢复均通过 |

## 持续请求观测

| 指标 | 本次数据 |
| --- | ---: |
| HTTP工作线程 | 16 |
| 运行时长 | {metrics['durationSeconds']}秒 |
| 请求数 | {metrics['requests']:,} |
| 请求错误 | 0 |
| P95响应 | {metrics['p95Ms']}ms |
| 最大响应 | {metrics['maxMs']}ms |
| 首次RSS采样 | {metrics['rssStartKiB']/1024:.1f}MiB |
| 末次RSS采样 | {metrics['rssEndKiB']/1024:.1f}MiB |
| 预热15秒后RSS范围 | {metrics['rssMinAfter15sKiB']/1024:.1f}～{metrics['rssMaxAfter15sKiB']/1024:.1f}MiB |

以上是本机回环网络、160台合成设备、短时运行的观测；不是生产SLA、内存无泄漏证明或24小时稳定性验收。完整5秒采样见[soak-metrics.json](nonwindows-reliability-2026-10-24/soak-metrics.json)。首次预热/缓存增长与后续波动保留，未用任意内存阈值冒充业务要求。

## 相关回归

客户端CTest5/5、服务端4/4通过。API、实际客户端集成、目录员工、统计、花型上传和队列/日志、设备断流重连及多帧设备文件列表恢复均通过。SQL Server未接受许可、未启动。

## 夹具修正

- 并发员工工号最初含字母，全部被业务校验正确拒绝；改为合法数字后再完整运行5分钟，相同手机号竞争符合唯一性规则。最终功能构建另完整运行5分钟，报告使用最后一次指标。
- MySQL定时器专项首次缺少空库boer_timer；补建后按真实分钟周期全量复测通过。
- 四客户端首次构建少了QJsonArray头，旧二进制曾进入通用自测；增加每进程独立完成标记，并在成功构建后重跑，4个专项日志和完成标记均验证，不将早期通用自测充作并发证据。
- 磁盘满首次剩余小块仍可保存几十字节；改用512KB下载。数据库夹具初版备注超过255字被正确拒绝，随后改为合法备注、耗尽剩余小块并要求存储错误；均用同请求释放空间后成功证明不是业务参数失败。
- UTC修复使新任务不再错误失败，旧花型日志翻译检查缺少终态数据；DELETE实际是移除任务，不会生成取消历史。现在独立临时库明确预置一条失败记录验证翻译，保留其余真实传输和队列检查。

## 仍不能独立完成的非Windows范围

- 客户实机：缝纫电控、固件、停机/回零原因码、报警505、屏幕远控授权协议；员工绑定及急停计件仍按用户要求不动。
- 图形预览：缺可靠解析SDK/格式语义，按用户要求先暂缓，不能用猜测图形算通过。
- SQL Server：用户明确不授权启动；没有将此项归为仅Windows限制，也未绕过许可。
- 真实物理断电/介质损坏、休眠锁屏唤醒、真实系统拼音候选窗和辅助技术：本轮未操作用户工作电脑的这些系统状态；事件注入、SIGKILL、ENOSPC仅按各自范围记录。
- 数小时/数天稳定性、远超2250设备和真实多机网络并发：本轮有界测试不能替代。实际系统多屏、DPI、RDP等Windows项目继续保留原原因。

## 证据与清理

[重点逐项明细](nonwindows-reliability-2026-10-24/检查明细.jsonl)、4个Qt进程日志/完成标记、实时row-results.jsonl及套件suites.jsonl均保留；每条完成即记录刷新。主台账仍为195注明范围通过、243部分、4模拟实机待验、23受限暂缓非缺陷、11空项，未判定/待修复失败0。

本次专用MySQL容器codex-boer-qt-regression-mysql-scale-1024已停止并自动删除。32MB虚拟卷已正常卸载，映像文件已删除；宿主磁盘未填满。未主动关闭已有普通客户端，未修改原客户表或正式数据。代码未提交或打包Windows程序。
'''
(report/'非Windows可靠性补测-2026-10-24.md').write_text(text)
print(json.dumps(dict(checks=len(checks),suites=len(suites),rows=sorted({x['row'] for x in checks.values()}))))
