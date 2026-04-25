package api

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"reflect"
	"testing"

	"boer-lan-server/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

func TestUpdateRoleSyncsUsersRoleAndPermissions(t *testing.T) {
	gin.SetMode(gin.TestMode)

	db, err := gorm.Open(sqlite.Open("file:role_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Role{}, &model.User{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	oldPermissions := `{"home":true,"dashboard":false}`
	newPermissions := `{"home":true,"dashboard":true,"statistics":true}`

	role := model.Role{
		Name:        "viewer",
		Permissions: oldPermissions,
	}
	if err := db.Create(&role).Error; err != nil {
		t.Fatalf("create role: %v", err)
	}

	user := model.User{
		Username:    "u001",
		Password:    "hashed",
		Nickname:    "测试账号",
		Phone:       "13800000000",
		Role:        "viewer",
		Permissions: oldPermissions,
	}
	if err := db.Create(&user).Error; err != nil {
		t.Fatalf("create user: %v", err)
	}

	body, _ := json.Marshal(map[string]interface{}{
		"name":        "operator",
		"permissions": newPermissions,
	})

	req := httptest.NewRequest(http.MethodPut, "/role/1", bytes.NewReader(body))
	req.Header.Set("Content-Type", "application/json")
	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)
	c.Request = req
	c.Params = gin.Params{{Key: "id", Value: "1"}}

	handler := NewRoleHandler(db)
	handler.UpdateRole(c)

	if w.Code != http.StatusOK {
		t.Fatalf("expected status 200, got %d, body=%s", w.Code, w.Body.String())
	}

	var updatedUser model.User
	if err := db.First(&updatedUser, user.ID).Error; err != nil {
		t.Fatalf("load updated user: %v", err)
	}
	if updatedUser.Role != "operator" {
		t.Fatalf("expected synced user role operator, got %q", updatedUser.Role)
	}
	expectedPermissions := parsePermissionMap(newPermissions)
	actualPermissions := parsePermissionMap(updatedUser.Permissions)
	if !reflect.DeepEqual(actualPermissions, expectedPermissions) {
		t.Fatalf("expected synced user permissions %v, got %v", expectedPermissions, actualPermissions)
	}
}
