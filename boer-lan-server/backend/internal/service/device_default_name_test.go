package service

import (
	"testing"

	"boer-lan-server/internal/model"
)

func TestDefaultDeviceNameDoesNotReplaceNamedDevice(t *testing.T) {
	device := model.Device{Code: "504011", Name: "0504011", InitialName: "0504011"}
	for _, name := range []string{"设备504011", "设备 504011"} {
		if shouldRefreshProtocolDeviceName(device, name) || shouldRefreshProtocolInitialName(device, name) {
			t.Fatalf("default name %q must not replace a named device", name)
		}
	}
	device.Name, device.InitialName = "设备504011", "设备504011"
	if !shouldRefreshProtocolDeviceName(device, "0504011") || !shouldRefreshProtocolInitialName(device, "0504011") {
		t.Fatal("a real protocol name should replace a placeholder")
	}
}
