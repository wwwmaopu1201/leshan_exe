"""Build the Qt client's bundled, product-specific user manual."""
from pathlib import Path
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.pagesizes import A4
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfbase.cidfonts import UnicodeCIDFont
from reportlab.platypus import SimpleDocTemplate,Paragraph,Spacer,PageBreak
from xml.sax.saxutils import escape

output=Path(__file__).resolve().parents[1]/'assets/manuals/client-user-manual.pdf'
font_path=Path('/System/Library/Fonts/STHeiti Light.ttc')
if font_path.exists():pdfmetrics.registerFont(TTFont('ManualChinese',str(font_path),subfontIndex=0))
else:pdfmetrics.registerFont(UnicodeCIDFont('STSong-Light'))
font='ManualChinese' if font_path.exists() else 'STSong-Light'
styles={
 'title':ParagraphStyle('title',fontName=font,fontSize=24,leading=36,textColor=colors.HexColor('#17355c'),spaceAfter=20),
 'h1':ParagraphStyle('h1',fontName=font,fontSize=18,leading=28,textColor=colors.HexColor('#17355c'),spaceAfter=18),
 'h2':ParagraphStyle('h2',fontName=font,fontSize=13,leading=22,textColor=colors.HexColor('#2465a0'),spaceBefore=14,spaceAfter=6),
 'body':ParagraphStyle('body',fontName=font,fontSize=10.5,leading=19,spaceAfter=9,wordWrap='CJK'),
 'sub':ParagraphStyle('sub',fontName=font,fontSize=11,leading=19,textColor=colors.HexColor('#66788a'),spaceAfter=16),
}
pages=[
('连接与登录',[
 ('版本与用途','本说明适用于博尔物联网管理软件 Qt 客户端 qt-0.1.0，与当前 Qt 服务端配合使用。主要功能包括设备管理、花型传输、数据统计、员工管理和个人中心。账号实际可见菜单由服务端分配的权限决定。'),
 ('1. 启动服务端','先启动服务端，确认服务端显示的管理 API 地址和设备 TCP 端口。默认管理端口为 8088，默认设备 TCP 端口为 38400；修改过配置时以服务端实际显示为准。'),
 ('2. 登录客户端','在客户端填写服务端 IP 或主机名、账号和密码。管理端口有变化时，在地址后填写“:端口”，例如 192.168.1.10:8088。登录客户端使用管理 API 端口，设备连接使用设备 TCP 端口。两者用途不同。'),
 ('3. 保存与切换账号','可勾选“记住密码”。登录成功后，地址与账号会进入历史列表；切换账号时恢复该服务器下该账号保存的信息。关闭并重新启动客户端后仍需在登录页登录。密码旁的图标用于切换明文与隐藏显示。'),
 ('4. 登录失败时','检查地址是否有内部空格、端口是否正确、服务端是否已启动，以及客户端与服务端网络是否互通。请求超时后可直接修改信息并重试。顶栏分别显示客户端版本、服务端版本、管理地址和设备 TCP 端口。'),
]),
('设备、分组与首页',[
 ('设备列表','进入“设备管理”查看设备编号、名称、状态等信息。设备名搜索支持双向包含：设备名包含搜索词，或搜索词包含完整设备名，均可匹配。列表页可调整每页条数、翻页和选择设备。'),
 ('分组与未分组设备','左侧分组树用于筛选设备。清空选择后恢复全部设备范围；“未分组设备”表示没有所属分组的设备。批量移动时先选择设备，再选择目标组。删除分组后，组内设备移至未分组范围；仍有关联子分组、账号或员工时不能直接删除。'),
 ('编辑和删除','编辑设备时，设备名称不能为空且最多 100 个字符，同组名称不能重复。放弃已修改内容时会提示确认。移出分组与彻底删除设备是不同操作，执行前请阅读确认提示。'),
 ('设备连接','设备端应连接到服务端实际的设备 TCP 端口。设备开机并完成识别后，服务端和客户端会显示设备状态。删除后的设备重新连接时不需要重启服务端。状态会自动更新。“关机”表示服务端当前没有设备连接，检查设备电源和网络后再重试。实际行为仍取决于设备网络和固件。'),
 ('首页与数据看板','首页显示设备、花型和效率统计；效率排名只包含当前开机设备。运行、加工、空闲等时长应结合对应字段查看。看板支持按分组和时间筛选；移动鼠标到图表上可查看日期及数值，运行与加工时长提示包含时分秒。长设备名使用省略显示，悬停可查看全文。'),
]),
('花型管理与传输',[
 ('服务端花型','在“文件管理”添加花型文件，填写名称、类型、针数、工价和订单编号等信息。订单编号只允许字母和数字，禁止空格、汉字及特殊符号。具体支持的文件类型以选择文件窗口和实际设备能力为准。批量修改只改变明确填写或勾选修改的字段；修改工价需选择对应选项。'),
 ('下发到设备','选中服务端花型及目标设备后提交下发任务。到下发队列查看等待、下发中、完成或失败状态。缝纫中的设备会等待空闲，队列未完成时仍可查询已有文件缓存。失败时查看原因，检查设备连接和文件情况后再重试。模拟设备测试不能代替设备端实际缝制验证。'),
 ('设备文件与回传','选择设备后刷新设备花型列表。选中需要的文件进行回传，查看上传队列进度。多个同名冲突按页处理，跨页输入会保留。同名文件默认采用重命名；选择覆盖时，保留服务端已有花型资料并更新文件。可批量删除设备文件；停止后续删除时，已发送的当前操作仍会等待设备应答。遇到同名不同编号且无法明确删除目标时，应在设备端确认处理。'),
 ('任务与记录','队列中的暂停、继续、取消等操作受当前状态限制，不能执行的按钮会置灰。清理完成记录前需要确认。分页只改变显示范围，任务仍在服务端执行。'),
 ('预览限制','当前版本可以查看花型名称、针数、尺寸和其他文件资料。图形预览尚未具备可靠的完整格式解析能力；界面会显示不支持图形预览的提示，不能据此确认设备实际缝制轨迹。'),
]),
('员工、导入与统计导出',[
 ('员工资料','员工工号为 1 至 11 位数字，保留前导零；姓名最多 8 个字符。手机号可输入包含空格的格式，保存时去除空格并校验号码；在用员工的手机号不能重复。分组应选择该员工所属的业务分组。'),
 ('CSV 文件导入','先下载员工导入模板。模板列依次为员工工号、员工姓名、手机号、备注。保存为 CSV 后，在批量导入窗口选择所属分组和文件。包含逗号或换行的备注需要使用标准 CSV 引号包裹。当前文件导入入口读取 CSV 文本，不直接读取 XLSX 工作簿。'),
 ('粘贴表格','可从表格复制上述列并粘贴到“粘贴表格内容”。系统支持制表符分隔，并识别工号标题行。格式错误时先按提示修改数据，再重新导入。'),
 ('覆盖与恢复','需要修改已有工号资料时，选择“覆盖已有工号”；未选择覆盖时会报告重复记录。重新导入已删除的员工会恢复原员工 ID，保留历史关联；若手机号已被他人在用，需要先处理冲突。删除员工后，已有生产和薪资历史仍保留。'),
 ('导出与统计','员工导出默认生成 XLSX，也可选择 CSV。XLSX 中的工号和手机号按文本保存，创建时间列已设置宽度。进入工资、加工、时长、报警等统计页，选择日期和筛选范围后查看或导出；导出的范围以当前页面选择为准。加工明细的平均缝纫速度单位为针/分钟；急停、回零计件口径需由业务负责人确认。'),
]),
('个人中心、支持与常见问题',[
 ('个人中心','基础信息页面可更新当前账号姓名和手机号，并查看自己的登录记录。账号姓名最多 50 个字符。空值或格式错误会在输入项下方提示。修改密码时填写旧密码、新密码和确认密码；三项均可切换明暗文显示。'),
 ('权限与菜单','只有管理员分配的功能才会出现在业务菜单中。没有业务菜单权限的账号仍可进入个人中心和服务支持。需要修改角色或可见范围时，请联系负责配置该服务端的管理员。'),
 ('地图与在线留言','“查看地图”会交给系统默认浏览器打开地图搜索。在线留言会生成邮件草稿交给系统邮件程序，当前不会写入软件内的留言箱；仅打开草稿不代表已发送，需由用户在邮件程序中确认发送。是否成功打开还取决于系统默认程序配置。'),
 ('连接与数据排查','设备显示关机时，检查设备是否开机、服务端地址和设备 TCP 端口、网线及网络。统计为空时，先检查日期、分组范围、账号权限与对应时段是否有设备上报数据。传输失败时查看任务中的具体错误，不以按钮提示代替任务最终结果。'),
 ('本说明的范围','本说明针对当前 Qt 客户端操作，不是旧版客户端或设备制造商的说明书。设备侧设置、具体文件格式与缝制安全操作，应同时参照设备自身的有效操作资料。'),
])]
story=[]
for index,(title,sections) in enumerate(pages):
 if index==0:
  story += [Paragraph('博尔物联网管理软件',styles['title']),Paragraph('Qt 客户端操作说明',styles['h1']),Paragraph('qt-0.1.0  |  2026-10-24',styles['sub'])]
 else:story.append(Paragraph(f'{index+1}. {title}',styles['h1']))
 for heading,text in sections:story.extend([Paragraph(escape(heading),styles['h2']),Paragraph(escape(text),styles['body'])])
 if index<len(pages)-1:story.append(PageBreak())
def footer(canvas,doc):
 canvas.setFont(font,8);canvas.setFillColor(colors.HexColor('#66788a'));canvas.drawString(42,25,'博尔物联网管理软件 · Qt 客户端 qt-0.1.0');canvas.drawRightString(A4[0]-42,25,str(doc.page))
output.parent.mkdir(parents=True,exist_ok=True)
SimpleDocTemplate(str(output),pagesize=A4,rightMargin=44,leftMargin=44,topMargin=40,bottomMargin=44,title='博尔物联网管理软件 Qt 客户端操作说明',author='博尔物联网管理软件').build(story,onFirstPage=footer,onLaterPages=footer)
print(output)
