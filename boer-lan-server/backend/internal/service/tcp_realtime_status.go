package service

func buildRealtimeStatusQueryCommand() *Packet {
	return buildProtocolCommand(PTRealtimeStatus, PNRealtimeStatus, nil)
}

func mapRealtimeDeviceStatus(status byte) (string, bool) {
	switch status {
	case 0x00:
		return "idle", true
	case 0x01:
		return "working", true
	default:
		return "", false
	}
}
