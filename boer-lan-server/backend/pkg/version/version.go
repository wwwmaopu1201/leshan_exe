package version

import (
	"os"
	"strings"
)

const Current = "1.0.9"

func Resolve() string {
	if envVersion := Normalize(os.Getenv("APP_VERSION")); envVersion != "" {
		return envVersion
	}
	return Normalize(Current)
}

func Normalize(raw string) string {
	trimmed := strings.TrimSpace(raw)
	trimmed = strings.TrimPrefix(trimmed, "v")
	trimmed = strings.TrimPrefix(trimmed, "V")
	return trimmed
}
