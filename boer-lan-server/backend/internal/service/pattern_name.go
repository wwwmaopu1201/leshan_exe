package service

import (
	"path/filepath"
	"strings"
)

var knownPatternFileExtensions = map[string]struct{}{
	".dst": {},
	".mtp": {},
	".nsp": {},
	".ntp": {},
	".sdg": {},
	".sdt": {},
	".slw": {},
	".vdt": {},
	".xdg": {},
}

func TrimKnownPatternFileExtension(fileName string) string {
	fileName = strings.TrimSpace(fileName)
	if fileName == "" {
		return ""
	}

	ext := KnownPatternFileExtension(fileName)
	if ext == "" {
		return fileName
	}
	return strings.TrimSpace(strings.TrimSuffix(fileName, filepath.Ext(fileName)))
}

func KnownPatternFileExtension(fileName string) string {
	fileName = strings.TrimSpace(fileName)
	if fileName == "" {
		return ""
	}

	ext := filepath.Ext(fileName)
	if _, ok := knownPatternFileExtensions[strings.ToLower(ext)]; !ok {
		return ""
	}
	return ext
}
