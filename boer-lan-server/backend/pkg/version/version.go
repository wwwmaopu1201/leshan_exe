package version

import (
	"os"
	"strconv"
	"strings"
)

const Current = "1.0.29"

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

func Compare(left, right string) int {
	leftParts := parseParts(left)
	rightParts := parseParts(right)
	maxParts := len(leftParts)
	if len(rightParts) > maxParts {
		maxParts = len(rightParts)
	}

	for index := 0; index < maxParts; index++ {
		leftPart := 0
		if index < len(leftParts) {
			leftPart = leftParts[index]
		}
		rightPart := 0
		if index < len(rightParts) {
			rightPart = rightParts[index]
		}
		if leftPart > rightPart {
			return 1
		}
		if leftPart < rightPart {
			return -1
		}
	}

	return 0
}

func parseParts(raw string) []int {
	normalized := Normalize(raw)
	if normalized == "" {
		return nil
	}

	parts := strings.Split(normalized, ".")
	parsed := make([]int, 0, len(parts))
	for _, part := range parts {
		value, err := strconv.Atoi(part)
		if err != nil {
			value = 0
		}
		parsed = append(parsed, value)
	}
	return parsed
}
