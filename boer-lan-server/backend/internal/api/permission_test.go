package api

import (
	"reflect"
	"testing"

	"boer-lan-server/internal/model"

	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

func TestLoadUserPermissionMapPrefersRolePermissions(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:permission_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Role{}, &model.User{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	role := model.Role{
		Name:        "user",
		Permissions: `{"deviceManagement":true}`,
	}
	if err := db.Create(&role).Error; err != nil {
		t.Fatalf("create role: %v", err)
	}

	user := model.User{
		Username:    "test1",
		Password:    "hashed",
		Nickname:    "测试",
		Phone:       "13800000000",
		Role:        "user",
		Permissions: `{"fileManagement":true}`,
	}
	if err := db.Create(&user).Error; err != nil {
		t.Fatalf("create user: %v", err)
	}

	got, err := loadUserPermissionMap(db, user.ID, user.Role)
	if err != nil {
		t.Fatalf("load user permission map: %v", err)
	}

	want := map[string]bool{
		"deviceManagement": true,
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("expected role permissions %v, got %v", want, got)
	}
}
