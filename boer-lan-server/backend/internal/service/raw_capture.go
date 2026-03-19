package service

import (
	"fmt"
	"io"
	"strings"
)

type rawCaptureReader struct {
	reader  io.Reader
	limit   int
	total   int
	preview []byte
}

type rawCaptureSnapshot struct {
	Total   int
	Preview []byte
}

func newRawCaptureReader(reader io.Reader, limit int) *rawCaptureReader {
	if limit <= 0 {
		limit = 64
	}
	return &rawCaptureReader{
		reader:  reader,
		limit:   limit,
		preview: make([]byte, 0, limit),
	}
}

func (r *rawCaptureReader) Read(p []byte) (int, error) {
	n, err := r.reader.Read(p)
	if n > 0 {
		r.total += n
		remaining := r.limit - len(r.preview)
		if remaining > 0 {
			if remaining > n {
				remaining = n
			}
			r.preview = append(r.preview, p[:remaining]...)
		}
	}
	return n, err
}

func (r *rawCaptureReader) Reset() {
	r.total = 0
	r.preview = r.preview[:0]
}

func (r *rawCaptureReader) Snapshot() rawCaptureSnapshot {
	preview := make([]byte, len(r.preview))
	copy(preview, r.preview)
	return rawCaptureSnapshot{
		Total:   r.total,
		Preview: preview,
	}
}

func formatRawCapture(snapshot rawCaptureSnapshot) string {
	if snapshot.Total == 0 || len(snapshot.Preview) == 0 {
		return "-"
	}
	return fmt.Sprintf("hex=%s ascii=%q", formatHexBytes(snapshot.Preview), formatASCIIBytes(snapshot.Preview))
}

func formatHexBytes(data []byte) string {
	if len(data) == 0 {
		return "-"
	}
	parts := make([]string, 0, len(data))
	for _, b := range data {
		parts = append(parts, fmt.Sprintf("%02X", b))
	}
	return strings.Join(parts, " ")
}

func formatASCIIBytes(data []byte) string {
	if len(data) == 0 {
		return ""
	}
	buf := make([]byte, len(data))
	for i, b := range data {
		if b >= 32 && b <= 126 {
			buf[i] = b
			continue
		}
		buf[i] = '.'
	}
	return string(buf)
}
