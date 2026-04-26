package alarmcatalog

import (
	"fmt"
	"strconv"
	"strings"
)

type Descriptor struct {
	Code        string
	Description string
}

func (a Descriptor) Display() string {
	if a.Code == "" && a.Description == "" {
		return "报警"
	}
	if a.Code == "" {
		return a.Description
	}
	if a.Description == "" {
		return a.Code
	}
	return fmt.Sprintf("%s - %s", a.Code, a.Description)
}

var byID = map[uint16]Descriptor{
	1:   {"E.025", "右踏板被踩下时打开电源"},
	2:   {"E.035", "左踏板被踩下时打开电源"},
	3:   {"E.050", "电源接通后机头倾倒"},
	4:   {"E.055", "电源接通前机头倾倒"},
	5:   {"E.100", "电源接通后，主轴电机找不到零位"},
	6:   {"E.110", "起针位置不在允许范围内。转动手轮直至报警消失"},
	7:   {"E.111", "主轴工作异常"},
	8:   {"E.200", "电源接通后，X轴电机找不到零位"},
	9:   {"E.201", "X轴电机送布异常"},
	10:  {"E.210", "电源接通后，Y轴电机找不到零位"},
	11:  {"E.211", "Y轴电机送布异常"},
	12:  {"E.300", "电源接通后，抬压脚电机找不到零位"},
	13:  {"E.301", "抬压脚电机运行异常"},
	14:  {"E.500", "倍率设置过程中缝纫花型超出了压脚框的设置缝纫范围"},
	15:  {"E.501", "当前花型超出了压脚框的设置缝纫范围"},
	16:  {"E.512", "花型未缝完"},
	17:  {"E.551", "显示花型名与实际不符"},
	18:  {"E.552", "花型整理错误"},
	19:  {"E.553", "花型数据结构错误"},
	20:  {"E.700", "急停"},
	21:  {"E.701", "下载数据文件"},
	22:  {"E.702", "检查外部存储器"},
	23:  {"E.703", "正在复制中"},
	24:  {"E.710", "外部存储器未识别"},
	25:  {"E.711", "外部存储器无花型文件"},
	26:  {"E.712", "电磁铁未保护"},
	27:  {"E.713", "机器无花型文件"},
	28:  {"E.714", "空间不够,请先删除花型"},
	29:  {"E.715", "外部存储器花型数据过大"},
	30:  {"E.716", "内存整理中"},
	31:  {"E.717", "花型删除中"},
	32:  {"E.718", "花型超过压脚框缝纫范围 ,按撤销键撤销"},
	33:  {"E.719", "输入的三点在一条直线上，不能生成圆弧"},
	34:  {"E.720", "没有生成线型"},
	35:  {"E.721", "花型总针数超过了3000"},
	36:  {"E.DEL", "确定要删除吗"},
	37:  {"E.SAM", "花型重名，是否覆盖？"},
	38:  {"E.OVE", "存储器满，是否删除？"},
	39:  {"E.ZHB", "速度速度请按照100 步长设置"},
	40:  {"E.DOTNOMUTIL", "点输入段不能添加多重缝"},
	41:  {"E.SPACENOMUTIL", "空移段不能添加多重缝"},
	42:  {"E.DOTNOWAVE", "点输入段不能添加人字缝"},
	43:  {"E.SPACENOWAVE", "空移段不能添加人字缝"},
	44:  {"E.DOTNOREINF", "点输入段不能添加加固缝"},
	45:  {"E.SPACENOREINF", "空移段不能添加加固缝"},
	46:  {"E.OVERPRESSERFRAME", "图形超过压脚框范围,请重新设置"},
	47:  {"E.DELNODOT", "本段仅余两点，不能删除该点!"},
	48:  {"E.PITCHOVER_DEL", "超过最大针距，不能删除该点!"},
	49:  {"E.PITCHOVER_MOVE", "超过最大针距，请重新移动改点!"},
	50:  {"E.PITCHOVER_ADD", "超过最大针距，请重新添加该点!"},
	51:  {"E.STARTNOCODE", "起始点无附加功能!"},
	52:  {"E.PITCHNUMOVER", "花型针数过多，不能进行此项操作!"},
	53:  {"E.PCPATTERN", "上位机花型，不能进行此项操作!"},
	54:  {"E.CIRREWORK", "循环花型不能进行此项操作!"},
	55:  {"E.SPEEDEND", "针速率设置完成!"},
	56:  {"E.D025", "踏板踩到二档时打开电源!"},
	57:  {"E.D035", "踏板踩到一档时打开电源!"},
	58:  {"E.PRESS_UP", "压框没压下!"},
	59:  {"E.FIRST_TOZERO", "先回原点!"},
	60:  {"E.PRODUCT_COUNT", "生产量计数器上限到达!"},
	61:  {"E.BASELINE_COUNT", "底线计数器上限到达!"},
	62:  {"E.MPRESS_DOWN", "中压脚未抬起!"},
	63:  {"E.722", "正在复制中!"},
	64:  {"E.DUANXIAN", "断线!"},
	65:  {"E.DOWNLOAD_PROCCESS", "下载程序!"},
	66:  {"E.NO_PROCCESS", "外部存储器无程序文件!"},
	67:  {"E.REST", "确定参数复位?"},
	68:  {"E.AUTOCHANGE", "花型位置超范围，是否自动修正到合适位置"},
	69:  {"E.LOADOK", "参数导入完成"},
	70:  {"E.INIPAROK", "参数设置完成"},
	71:  {"E.NO_PARAFILE", "外部存储器无参数文件"},
	72:  {"E.FULL_PARAFILE", "内部存储器满，需要删除参数文件"},
	73:  {"E.DOWNPAROK", "参数导出完成"},
	74:  {"E.CUR_PARAFILE", "正在使用该参数文件，不能删除"},
	75:  {"E.DEL_ALL", "正在使用该参数文件，不能删除"},
	76:  {"E.REST_NOW", "正在复位中"},
	77:  {"E.113", "主轴驱动器报警!"},
	78:  {"E.203", "X轴驱动器报警"},
	79:  {"E.213", "Y 轴驱动器报警"},
	80:  {"E.GAS_ALARM", "气压不足报警!"},
	81:  {"E.TEMPLATE_OFFSET", "模板位置偏移!"},
	82:  {"E.TEMPLATE_UNIDENTIFIED", "模板未识别!"},
	83:  {"E.202", "X轴电机送布未完成"},
	84:  {"E.212", "Y轴电机送布未完成"},
	85:  {"E.AUTO_SIM", "自动模拟缝纫"},
	86:  {"E.303", "Z轴驱动器报警"},
	87:  {"E.TENSION_MODE", "绕线模式"},
	88:  {"E.TO_FIRSTPITCH", "确认花型后，移动到花型起缝点"},
	89:  {"E.XCHANGE", "确认交换"},
	90:  {"E.204", "X轴2驱动器报警"},
	91:  {"E.214", "Y轴2驱动器报警"},
	92:  {"E.304", "Z轴2驱动器报警"},
	93:  {"E.313", "V轴1驱动器报警"},
	94:  {"E.RETURN", "数据没有保存，是否退出？"},
	95:  {"E.HEAD_UP", "机头未上升"},
	96:  {"E.HEAD_DOWN", "机头未下降"},
	97:  {"E.TOZERO_Q1", "Q轴1找不到零位"},
	98:  {"E.TOZERO_Q2", "Q轴2找不到零位"},
	99:  {"E.323", "Q轴1驱动器报警"},
	100: {"E.324", "Q轴2驱动器报警"},
	101: {"E.CUT_UP", "切布刀未在上端"},
	102: {"E.CUT_DOWN", "切布刀未在下端"},
	103: {"E.310", "V轴找不到零位"},
	104: {"E.330", "W轴找不到零位"},
	105: {"E.333", "W轴驱动器报警"},
	106: {"E.HAND_UP", "取料机械手未上升"},
	107: {"E.HAND_DOWN", "取料机械手未下降"},
	108: {"E.SENDBAR_DOWN", "送料顶杆气缸未下降"},
	109: {"E.SEND", "送料机构未到位"},
	110: {"E.GET", "取料机构未到位"},
	111: {"E.CUT_DOWN", "未检测到模板"},
	112: {"E.GET_TEMPLATE", "取料机构未到位"},
	113: {"E.TO_SECORG1", "送料顶杆未上抬"},
	114: {"E.GAS_LOW", "负压不足"},
	115: {"E.GAS_FUL", "负压满未释放"},
	116: {"E.NOTGET_TEMPLATE", "模板未取走"},
	117: {"E.OVERLAY_PTN", "是否覆盖花型"},
	118: {"E.SWEEP_BACK", "拨线未复位"},
	119: {"E.SWEEP_OUT", "拨线器未伸出"},
	120: {"E.CUT_THREAD", "面线未剪断"},
	121: {"E.CUT_SEL", "拨线未伸出,是否继续剪线？"},
	122: {"E.314", "V轴2驱动器报警"},
	123: {"E.334", "W轴2驱动器报警"},
	124: {"E.HAND_UP_2", "取料机械手未上升"},
	125: {"E.HAND_DOWN_2", "取料机械手未下降"},
	126: {"E.SENDBAR_DOWN_2", "送料顶杆气缸未下降"},
	127: {"E.SEND_2", "送料机构未到位"},
	128: {"E.GET_2", "取料机构未到位"},
	129: {"E.DETECT_TEMPLATE_2", "未检测到模板"},
	130: {"E.GET_TEMPLATE_2", "取料机构未到位"},
	131: {"E.TO_SECORG1_2", "送料顶杆未上抬"},
	132: {"E.GAS_LOW_2", "负压不足"},
	133: {"E.GAS_FUL_2", "负压满未释放"},
	134: {"E.NOTGET_TEMPLATE_2", "模板未取走"},
	135: {"E.SET_THIN_PAR", "确认将当前参数设为薄料参数?"},
	136: {"E.REPLACE_CUR_PAR", "正在使用当前参数，替换后会修改复位之后的参数文件，是否确认替换?"},
	137: {"E.FORBID_DEL_PAR", "此参数不能被删除?"},
	138: {"E.SET_THICK_PAR", "确认将当前参数设为厚料参数?"},
	139: {"E.SET_CAMLET_PAR", "确认将当前参数设为羽绒参数?"},
	140: {"E.SET_CUR_PAR", "确认修改当前参数文件吗?"},
	141: {"E.BASELINE_CHANGING", "正在换底线"},
	142: {"E.BASELINE_FAILURE", "换底线未完成"},
	143: {"E.370", "DCM1找不到零位"},
	144: {"E.SET_M_THICK_PAR", "确认将当前参数设为中料参数?"},
	145: {"E.FORBID_DEL_PAR", "确认将当前参数设为特料1参数?"},
	146: {"E.SET_SPECIAL2_MATERIAL_PAR", "确认将当前参数设为特料2参数?"},
	147: {"E.CUT_OPEN", "剪线电机未松开"},
	148: {"E.SYS_BACKUP", "系统参数备份完成"},
	149: {"E.SET_BASEPOS", "设置参考位置"},
	150: {"E.BASEPOS_FIRST", "请先设置头1参考位置"},
	151: {"E.BASEPOS_FINISHED", "已对准好参考点，是否保存？"},
	152: {"E.114", "主轴2驱动器报警"},
	153: {"E.BOBBIN_OUT", "梭壳弹出"},
	154: {"E.322", "Q轴未运行完"},
	155: {"E.325", "转刀未在零位报警"},
	156: {"E.SAM_NAME", "重名,是否覆盖？"},
	157: {"E.SAVE_OVER", "保存完成!"},
	158: {"E.SAVE_FALSE", "操作失败，存储空间不够！"},
	159: {"E.001", "轴1驱动器故障"},
	160: {"E.002", "轴2驱动器故障"},
	161: {"E.003", "轴3驱动器故障"},
	162: {"E.PAT_AUTOMOVE", "花型位置自动居中"},
	163: {"E.CARD_WRITE_OK", "卡写入成功"},
	164: {"E.CARD_WRITE_ERR", "卡写入失败"},
	165: {"E.RFID_ERR", "请检查RFID连接!"},
}

var byCode = buildCodeIndex()

func buildCodeIndex() map[string]Descriptor {
	result := make(map[string]Descriptor, len(byID))
	for _, descriptor := range byID {
		result[strings.ToUpper(descriptor.Code)] = descriptor
	}
	return result
}

func Describe(code uint16) Descriptor {
	if descriptor, ok := byID[code]; ok {
		return descriptor
	}
	return Descriptor{
		Code:        fmt.Sprintf("%d", code),
		Description: "未知报警",
	}
}

func LookupRaw(raw string) (Descriptor, bool) {
	value := strings.TrimSpace(raw)
	if value == "" {
		return Descriptor{}, false
	}
	if parsed, err := strconv.ParseUint(value, 10, 16); err == nil && parsed > 0 {
		descriptor, ok := byID[uint16(parsed)]
		return descriptor, ok
	}
	descriptor, ok := byCode[strings.ToUpper(value)]
	return descriptor, ok
}

func RawCodesFor(value string) []string {
	needle := strings.TrimSpace(value)
	if needle == "" {
		return nil
	}
	if descriptor, ok := LookupRaw(needle); ok {
		return rawCodesForDescriptor(descriptor)
	}
	for id, descriptor := range byID {
		if descriptor.Display() == needle || descriptor.Description == needle {
			return []string{strconv.Itoa(int(id)), descriptor.Code}
		}
	}
	return nil
}

func rawCodesForDescriptor(target Descriptor) []string {
	result := make([]string, 0, 2)
	for id, descriptor := range byID {
		if descriptor == target {
			result = append(result, strconv.Itoa(int(id)))
			break
		}
	}
	if target.Code != "" {
		result = append(result, target.Code)
	}
	return result
}
