package api

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"boer-lan-server/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

func TestGetDeviceGroupsReturnsFrontendFieldNames(t *testing.T) {
	gin.SetMode(gin.TestMode)

	db, err := gorm.Open(sqlite.Open("file:device_groups_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Group{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	parent := model.Group{Name: "工厂A", SortOrder: 1}
	if err := db.Create(&parent).Error; err != nil {
		t.Fatalf("create parent group: %v", err)
	}
	child := model.Group{Name: "车间A", ParentID: &parent.ID, SortOrder: 2}
	if err := db.Create(&child).Error; err != nil {
		t.Fatalf("create child group: %v", err)
	}

	req := httptest.NewRequest(http.MethodGet, "/device/groups", nil)
	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)
	c.Request = req

	NewDeviceHandler(db).GetDeviceGroups(c)

	if w.Code != http.StatusOK {
		t.Fatalf("expected status 200, got %d, body=%s", w.Code, w.Body.String())
	}

	var body struct {
		Code int `json:"code"`
		Data []struct {
			ID        uint   `json:"id"`
			Name      string `json:"name"`
			ParentID  *uint  `json:"parentId"`
			SortOrder int    `json:"sortOrder"`
		} `json:"data"`
	}
	if err := json.Unmarshal(w.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode response: %v", err)
	}
	if body.Code != 0 {
		t.Fatalf("expected code 0, got %d", body.Code)
	}
	if len(body.Data) != 2 {
		t.Fatalf("expected 2 groups, got %d: %#v", len(body.Data), body.Data)
	}
	if body.Data[0].ID != parent.ID || body.Data[0].Name != "工厂A" || body.Data[0].ParentID != nil {
		t.Fatalf("unexpected parent group payload: %#v", body.Data[0])
	}
	if body.Data[1].ID != child.ID || body.Data[1].Name != "车间A" || body.Data[1].ParentID == nil || *body.Data[1].ParentID != parent.ID {
		t.Fatalf("unexpected child group payload: %#v", body.Data[1])
	}
}

func TestUpdateDeviceUpdatesGroupID(t *testing.T) {
	gin.SetMode(gin.TestMode)

	db, err := gorm.Open(sqlite.Open("file:device_update_group_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Group{}, &model.Device{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	source := model.Group{Name: "原分组"}
	target := model.Group{Name: "目标分组"}
	if err := db.Create(&source).Error; err != nil {
		t.Fatalf("create source group: %v", err)
	}
	if err := db.Create(&target).Error; err != nil {
		t.Fatalf("create target group: %v", err)
	}

	device := model.Device{
		Code:      "D-001",
		Name:      "设备一",
		Type:      model.DefaultDeviceType,
		ModelName: "BM-2000",
		GroupID:   &source.ID,
	}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}

	body, _ := json.Marshal(map[string]interface{}{
		"groupId": target.ID,
	})
	req := httptest.NewRequest(http.MethodPut, "/device/1", bytes.NewReader(body))
	req.Header.Set("Content-Type", "application/json")
	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)
	c.Request = req
	c.Params = gin.Params{{Key: "id", Value: "1"}}

	NewDeviceHandler(db).UpdateDevice(c)

	if w.Code != http.StatusOK {
		t.Fatalf("expected status 200, got %d, body=%s", w.Code, w.Body.String())
	}
	var updated model.Device
	if err := db.First(&updated, device.ID).Error; err != nil {
		t.Fatalf("load updated device: %v", err)
	}
	if updated.GroupID == nil || *updated.GroupID != target.ID {
		t.Fatalf("expected target group %d, got %#v", target.ID, updated.GroupID)
	}

	body, _ = json.Marshal(map[string]interface{}{
		"groupId": nil,
	})
	req = httptest.NewRequest(http.MethodPut, "/device/1", bytes.NewReader(body))
	req.Header.Set("Content-Type", "application/json")
	w = httptest.NewRecorder()
	c, _ = gin.CreateTestContext(w)
	c.Request = req
	c.Params = gin.Params{{Key: "id", Value: "1"}}

	NewDeviceHandler(db).UpdateDevice(c)

	if w.Code != http.StatusOK {
		t.Fatalf("expected status 200 for ungroup, got %d, body=%s", w.Code, w.Body.String())
	}
	if err := db.First(&updated, device.ID).Error; err != nil {
		t.Fatalf("reload updated device: %v", err)
	}
	if updated.GroupID != nil {
		t.Fatalf("expected device to be ungrouped, got %#v", updated.GroupID)
	}
}
