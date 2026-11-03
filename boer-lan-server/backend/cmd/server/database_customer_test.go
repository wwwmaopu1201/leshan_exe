package main

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"sync"
	"testing"
	"time"

	"boer-lan-server/internal/api"
	"boer-lan-server/internal/model"
	"boer-lan-server/internal/service"
	"boer-lan-server/pkg/utils"
	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

// Opt-in regression for a static customer snapshot. Always copy both database
// and WAL before opening SQLite; the supplied source is never opened for writes.
func TestCustomerSnapshotQueriesDuringWriteBacklog(t *testing.T) {
	source := os.Getenv("BOER_TEST_CUSTOMER_DB")
	if source == "" {
		t.Skip("set BOER_TEST_CUSTOMER_DB to a static customer snapshot")
	}
	dest := filepath.Join(t.TempDir(), "customer.db")
	for _, suffix := range []string{"", "-wal"} {
		in, err := os.Open(source + suffix)
		if os.IsNotExist(err) && suffix != "" {
			continue
		}
		if err != nil {
			t.Fatal(err)
		}
		out, err := os.Create(dest + suffix)
		if err != nil {
			in.Close()
			t.Fatal(err)
		}
		_, copyErr := io.Copy(out, in)
		closeErr := out.Close()
		in.Close()
		if copyErr != nil {
			t.Fatal(copyErr)
		}
		if closeErr != nil {
			t.Fatal(closeErr)
		}
	}
	database, err := openSQLiteDatabase(dest, &gorm.Config{Logger: logger.Default.LogMode(logger.Silent)})
	if err != nil {
		t.Fatal(err)
	}
	defer closeDatabase(database)
	var integrity string
	if err := database.Raw("PRAGMA quick_check").Scan(&integrity).Error; err != nil || integrity != "ok" {
		t.Fatalf("snapshot integrity=%s err=%v", integrity, err)
	}
	verifyAPIReadsDuringWriteBacklog(t, database)
}

func TestSQLiteAPIReadsDuringWriteBacklog(t *testing.T) {
	database, err := openSQLiteDatabase(filepath.Join(t.TempDir(), "api.db"), &gorm.Config{Logger: logger.Default.LogMode(logger.Silent)})
	if err != nil {
		t.Fatal(err)
	}
	defer closeDatabase(database)
	if err := database.AutoMigrate(&model.Group{}, &model.Role{}, &model.User{}, &model.Device{}, &model.ProductionRecord{}, &model.ServerConfig{}); err != nil {
		t.Fatal(err)
	}
	if err := database.Create(&model.Group{Name: "总分组"}).Error; err != nil {
		t.Fatal(err)
	}
	verifyAPIReadsDuringWriteBacklog(t, database)
}

func verifyAPIReadsDuringWriteBacklog(t *testing.T, database *gorm.DB) {
	t.Helper()
	previousSecret, previousExpiry := utils.JWTSecret, utils.JWTExpire
	utils.JWTSecret, utils.JWTExpire = "database-regression", 1
	t.Cleanup(func() { utils.JWTSecret, utils.JWTExpire = previousSecret, previousExpiry })
	user := model.User{Username: "__database_regression__", Password: "unused", Role: "admin"}
	if err := database.Create(&user).Error; err != nil {
		t.Fatal(err)
	}
	token, err := utils.GenerateToken(user.ID, user.Username, user.Role)
	if err != nil {
		t.Fatal(err)
	}
	router := gin.New()
	connections := service.NewConnectionManager()
	api.SetupRouter(router, database, "test", 1, 8088, service.NewPatternTransferService(database, connections), connections)
	if err := database.Exec("CREATE TABLE validation_writes(id INTEGER)").Error; err != nil {
		t.Fatal(err)
	}
	tx := database.Begin()
	if tx.Error != nil {
		t.Fatal(tx.Error)
	}
	defer tx.Rollback()
	if err := tx.Exec("INSERT INTO validation_writes VALUES(0)").Error; err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	var wg sync.WaitGroup
	errors := make(chan error, 64)
	for i := 0; i < 64; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			errors <- database.WithContext(ctx).Exec("INSERT INTO validation_writes VALUES(1)").Error
		}()
	}
	for _, path := range []string{"/api/device/list?pageSize=20", "/api/device/tree", "/api/auth/userinfo"} {
		result := make(chan *httptest.ResponseRecorder, 1)
		start := time.Now()
		go func(path string) {
			request := httptest.NewRequest(http.MethodGet, path, nil)
			request.Header.Set("Authorization", "Bearer "+token)
			response := httptest.NewRecorder()
			router.ServeHTTP(response, request)
			result <- response
		}(path)
		select {
		case response := <-result:
			var body struct {
				Code int `json:"code"`
			}
			if err := json.Unmarshal(response.Body.Bytes(), &body); err != nil {
				t.Fatal(err)
			}
			if response.Code != 200 || body.Code != 0 {
				t.Fatalf("%s status=%d business_code=%d", path, response.Code, body.Code)
			}
			t.Logf("%s with 64 queued writes: %s", path, time.Since(start))
		case <-time.After(2 * time.Second):
			t.Fatalf("%s blocked behind writes", path)
		}
	}
	if err := tx.Commit().Error; err != nil {
		t.Fatal(err)
	}
	wg.Wait()
	close(errors)
	for err := range errors {
		if err != nil {
			t.Fatal(err)
		}
	}
	var count int64
	if err := database.Table("validation_writes").Count(&count).Error; err != nil || count != 65 {
		t.Fatalf("writes lost: %d err=%v", count, err)
	}
	repairCtx, stop := context.WithTimeout(context.Background(), time.Second)
	defer stop()
	done := make(chan struct{})
	go func() { service.BackfillProductionDerivedData(database.WithContext(repairCtx)); close(done) }()
	select {
	case <-done:
	case <-time.After(3 * time.Second):
		t.Fatal("customer backfill ignored cancellation")
	}
}
