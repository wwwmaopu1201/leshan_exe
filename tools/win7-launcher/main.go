package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
)

type appConfig struct {
	mode        string
	title       string
	distDir     string
	backendPath string
	backendPort int
	listenPort  int
}

var (
	defaultMode  = "client"
	defaultTitle = "Boer LAN"
)

func main() {
	cfg := parseFlags()
	baseDir := executableDir()
	if !filepath.IsAbs(cfg.distDir) {
		cfg.distDir = filepath.Join(baseDir, cfg.distDir)
	}
	if cfg.backendPath != "" && !filepath.IsAbs(cfg.backendPath) {
		cfg.backendPath = filepath.Join(baseDir, cfg.backendPath)
	}

	if err := run(cfg, baseDir); err != nil {
		log.Fatalf("%v", err)
	}
}

func parseFlags() appConfig {
	cfg := appConfig{}
	flag.StringVar(&cfg.mode, "mode", defaultMode, "client or server")
	flag.StringVar(&cfg.title, "title", defaultTitle, "browser window title")
	flag.StringVar(&cfg.distDir, "dist", "dist", "frontend dist directory")
	flag.StringVar(&cfg.backendPath, "backend", "backend-server.exe", "backend executable path for server mode")
	flag.IntVar(&cfg.backendPort, "backend-port", 8088, "fallback backend port")
	flag.IntVar(&cfg.listenPort, "listen-port", 0, "local shell HTTP port; 0 picks a free port")
	flag.Parse()
	cfg.mode = strings.ToLower(strings.TrimSpace(cfg.mode))
	return cfg
}

func run(cfg appConfig, baseDir string) error {
	if cfg.mode != "client" && cfg.mode != "server" {
		return fmt.Errorf("invalid mode %q", cfg.mode)
	}
	if info, err := os.Stat(cfg.distDir); err != nil || !info.IsDir() {
		return fmt.Errorf("dist directory not found: %s", cfg.distDir)
	}

	dataDir := filepath.Join(baseDir, "data")
	if err := os.MkdirAll(dataDir, 0755); err != nil {
		return err
	}
	portFile := filepath.Join(dataDir, "backend-port.txt")

	var backend *exec.Cmd
	if cfg.mode == "server" {
		cmd, err := startBackend(cfg, dataDir, portFile)
		if err != nil {
			return err
		}
		backend = cmd
		defer stopProcess(backend)
	}

	listener, err := listenLocal(cfg.listenPort)
	if err != nil {
		return err
	}
	defer listener.Close()

	mux := http.NewServeMux()
	mux.HandleFunc("/__boerlan_win7/backend-port", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, map[string]int{"port": resolveBackendPort(portFile, cfg.backendPort)})
	})
	mux.Handle("/", noCache(http.FileServer(http.Dir(cfg.distDir))))

	server := &http.Server{Handler: mux}
	serverErr := make(chan error, 1)
	go func() {
		err := server.Serve(listener)
		if err != nil && !errors.Is(err, http.ErrServerClosed) {
			serverErr <- err
			return
		}
		serverErr <- nil
	}()

	url := fmt.Sprintf("http://127.0.0.1:%d/", listener.Addr().(*net.TCPAddr).Port)
	log.Printf("%s started: %s", cfg.title, url)

	browserCmd, err := openBrowser(baseDir, cfg.title, url)
	if err != nil {
		log.Printf("Failed to open browser automatically: %v", err)
	}

	signalChan := make(chan os.Signal, 1)
	signal.Notify(signalChan, os.Interrupt)

	if browserCmd != nil {
		waitChan := make(chan error, 1)
		go func() { waitChan <- browserCmd.Wait() }()
		select {
		case <-signalChan:
		case <-waitChan:
		case err := <-serverErr:
			return err
		}
	} else {
		select {
		case <-signalChan:
		case err := <-serverErr:
			return err
		}
	}

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	return server.Shutdown(ctx)
}

func executableDir() string {
	exe, err := os.Executable()
	if err != nil {
		return "."
	}
	return filepath.Dir(exe)
}

func startBackend(cfg appConfig, dataDir, portFile string) (*exec.Cmd, error) {
	if _, err := os.Stat(cfg.backendPath); err != nil {
		return nil, fmt.Errorf("backend executable not found: %s", cfg.backendPath)
	}
	cmd := exec.Command(cfg.backendPath)
	cmd.Dir = filepath.Dir(cfg.backendPath)
	cmd.Env = append(os.Environ(),
		"DATA_DIR="+dataDir,
		"BOERLAN_DATA_DIR="+dataDir,
		"PORT_FILE="+portFile,
		"APP_VERSION=win7-shell",
	)
	cmd.Stdout = logWriter("backend")
	cmd.Stderr = logWriter("backend")
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	log.Printf("Backend started with PID %d", cmd.Process.Pid)
	return cmd, nil
}

func stopProcess(cmd *exec.Cmd) {
	if cmd == nil || cmd.Process == nil {
		return
	}
	_ = cmd.Process.Kill()
	_, _ = cmd.Process.Wait()
}

func listenLocal(port int) (net.Listener, error) {
	address := "127.0.0.1:0"
	if port > 0 {
		address = "127.0.0.1:" + strconv.Itoa(port)
	}
	return net.Listen("tcp", address)
}

func resolveBackendPort(portFile string, fallback int) int {
	raw, err := os.ReadFile(portFile)
	if err == nil {
		if port, parseErr := strconv.Atoi(strings.TrimSpace(string(raw))); parseErr == nil && port > 0 && port <= 65535 {
			return port
		}
	}
	return fallback
}

func writeJSON(w http.ResponseWriter, value interface{}) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(value)
}

func noCache(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store")
		next.ServeHTTP(w, r)
	})
}

func openBrowser(baseDir, title, url string) (*exec.Cmd, error) {
	if runtime.GOOS == "windows" {
		if browser := findBundledBrowser(baseDir); browser != "" {
			profileDir := filepath.Join(baseDir, "browser-profile")
			_ = os.MkdirAll(profileDir, 0755)
			cmd := exec.Command(browser,
				"--app="+url,
				"--user-data-dir="+profileDir,
				"--no-first-run",
				"--disable-features=TranslateUI",
				"--window-size=1440,900",
			)
			cmd.Dir = filepath.Dir(browser)
			if err := cmd.Start(); err != nil {
				return nil, err
			}
			return cmd, nil
		}
		return nil, exec.Command("rundll32", "url.dll,FileProtocolHandler", url).Start()
	}

	candidates := []string{"google-chrome", "chromium", "open", "xdg-open"}
	for _, candidate := range candidates {
		if path, err := exec.LookPath(candidate); err == nil {
			cmd := exec.Command(path, url)
			return nil, cmd.Start()
		}
	}
	return nil, errors.New("no browser command found")
}

func findBundledBrowser(baseDir string) string {
	candidates := []string{
		filepath.Join(baseDir, "browser", "chrome.exe"),
		filepath.Join(baseDir, "browser", "Chrome", "Application", "chrome.exe"),
		filepath.Join(baseDir, "chrome", "chrome.exe"),
	}
	for _, candidate := range candidates {
		if info, err := os.Stat(candidate); err == nil && !info.IsDir() {
			return candidate
		}
	}
	return ""
}

type prefixedWriter string

func logWriter(prefix string) prefixedWriter {
	return prefixedWriter(prefix)
}

func (w prefixedWriter) Write(p []byte) (int, error) {
	text := strings.TrimRight(string(p), "\r\n")
	if text != "" {
		log.Printf("[%s] %s", string(w), text)
	}
	return len(p), nil
}
