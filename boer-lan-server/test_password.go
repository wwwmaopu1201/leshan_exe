package main

import (
	"fmt"
	"golang.org/x/crypto/bcrypt"
)

func main() {
	// 数据库中的 hash
	hash := "$2a$10$N9qo8uLOickgx2ZMRZoMye/1qZVJR6jLqJE5fBIVGRV0cTvK7mPGK"

	// 测试密码
	password := "admin123"

	// 验证
	err := bcrypt.CompareHashAndPassword([]byte(hash), []byte(password))
	if err == nil {
		fmt.Println("✅ 密码验证成功！hash 对应的密码是:", password)
	} else {
		fmt.Println("❌ 密码验证失败！")
		fmt.Println("错误:", err)

		// 生成正确的 hash
		fmt.Println("\n尝试为 'admin123' 生成新的 hash...")
		newHash, _ := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
		fmt.Println("新 hash:", string(newHash))
	}
}
