package testrunner

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"time"
)

type Runner struct {
	LogDir string
}

const successSettleWindow = 200 * time.Millisecond

type Result struct {
	Duration time.Duration
	LogPath  string
	Err      error
}

func (r Runner) RunCase(suiteName string, tc Case) Result {
	start := time.Now()
	logPath := filepath.Join(r.LogDir, suiteName, tc.Name+".log")
	result := Result{LogPath: logPath}

	if err := os.MkdirAll(filepath.Dir(logPath), 0o755); err != nil {
		result.Duration = time.Since(start)
		result.Err = err
		return result
	}

	var output safeBuffer
	err := runCase(tc, &output)
	if writeErr := os.WriteFile(logPath, output.Bytes(), 0o644); writeErr != nil && err == nil {
		err = writeErr
	}
	if err == nil {
		err = matchOutput(tc, output.String())
	}

	result.Duration = time.Since(start)
	result.Err = err
	return result
}

func runCase(tc Case, output *safeBuffer) error {
	if tc.QemuMode == QemuModeHostOnly {
		return runHostOnlyCase(tc, output)
	}
	return runQEMUCase(tc, output)
}

func runHostOnlyCase(tc Case, output *safeBuffer) error {
	if len(tc.HostCommand) == 0 {
		return fmt.Errorf("host-only case %q has no HostCommand", tc.Name)
	}
	if tc.Timeout <= 0 {
		tc.Timeout = 30 * time.Second
	}

	background, err := startBackground(tc.Background)
	if err != nil {
		return err
	}
	defer stopBackground(background)

	cmd := exec.Command(tc.HostCommand[0], tc.HostCommand[1:]...)
	cmd.SysProcAttr = processGroupAttr()

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return err
	}

	if err := cmd.Start(); err != nil {
		return err
	}

	done := make(chan error, 1)
	go func() {
		done <- cmd.Wait()
	}()

	var wg sync.WaitGroup
	defer func() {
		killProcessGroup(cmd)
		select {
		case <-done:
		case <-time.After(3 * time.Second):
		}
		wg.Wait()
	}()

	wg.Add(2)
	go func() { defer wg.Done(); copyOutput(output, stdout) }()
	go func() { defer wg.Done(); copyOutput(output, stderr) }()

	timer := time.NewTimer(tc.Timeout)
	defer timer.Stop()

	select {
	case err := <-done:
		wg.Wait()
		return err
	case <-timer.C:
		return fmt.Errorf("timeout after %s", tc.Timeout)
	}
}

func runQEMUCase(tc Case, output *safeBuffer) error {
	if tc.Timeout <= 0 {
		tc.Timeout = 30 * time.Second
	}

	background, err := startBackground(tc.Background)
	if err != nil {
		return err
	}
	defer stopBackground(background)

	qemuTarget := "qemu"
	qemuArgs := []string{"--no-print-directory", qemuTarget, "QEMUEXTRA+=-snapshot"}
	if tc.QemuMode == QemuModeNetForward {
		qemuArgs = []string{"--no-print-directory", "qemu", "NETFWD=1", "QEMUEXTRA+=-snapshot"}
	}

	cmd := exec.Command("make", qemuArgs...)
	cmd.SysProcAttr = processGroupAttr()

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return err
	}
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return err
	}

	if err := cmd.Start(); err != nil {
		return err
	}

	done := make(chan error, 1)
	go func() {
		done <- cmd.Wait()
	}()

	stopReaders := make(chan struct{})
	var wg sync.WaitGroup
	defer func() {
		close(stopReaders)
		killProcessGroup(cmd)
		_ = stdin.Close()
		select {
		case <-done:
		case <-time.After(3 * time.Second):
		}
		wg.Wait()
	}()

	events := make(chan string, 128)
	readOutput := func(reader io.Reader) {
		defer wg.Done()
		buf := make([]byte, 4096)
		for {
			n, readErr := reader.Read(buf)
			if n > 0 {
				chunk := string(buf[:n])
				output.WriteString(chunk)
				select {
				case events <- chunk:
				case <-stopReaders:
					return
				}
			}
			if readErr != nil {
				return
			}
		}
	}
	wg.Add(2)
	go readOutput(stdout)
	go readOutput(stderr)

	timer := time.NewTimer(tc.Timeout)
	defer timer.Stop()
	var settleTimer *time.Timer
	var settleChan <-chan time.Time
	stopSettleTimer := func() {
		if settleTimer == nil {
			return
		}
		if !settleTimer.Stop() {
			select {
			case <-settleTimer.C:
			default:
			}
		}
		settleChan = nil
	}
	armSettleTimer := func() {
		if settleTimer == nil {
			settleTimer = time.NewTimer(successSettleWindow)
		} else {
			if !settleTimer.Stop() {
				select {
				case <-settleTimer.C:
				default:
				}
			}
			settleTimer.Reset(successSettleWindow)
		}
		settleChan = settleTimer.C
	}
	defer stopSettleTimer()

	var transcript strings.Builder
	nextCommand := 0
	waitingForPrompt := true
	waitingForCommand := false
	for {
		select {
		case err := <-done:
			if err == nil {
				return fmt.Errorf("qemu exited before test passed")
			}
			return fmt.Errorf("qemu exited before test passed: %w", err)
		case <-timer.C:
			return fmt.Errorf("timeout after %s", tc.Timeout)
		case <-settleChan:
			if nextCommand == len(tc.Commands) && outputMatches(tc, output.String()) {
				return nil
			}
			stopSettleTimer()
		case chunk := <-events:
			transcript.WriteString(chunk)
			if waitingForPrompt && strings.Contains(transcript.String(), "$ ") {
				transcript.Reset()
				waitingForPrompt = false
				if err := sendCommand(stdin, tc.Commands[nextCommand]); err != nil {
					return err
				}
				waitingForCommand = true
				nextCommand++
			}
			if waitingForCommand && waitingForPromptCompletion(transcript.String()) {
				transcript.Reset()
				waitingForCommand = false
				if nextCommand < len(tc.Commands) {
					if err := sendCommand(stdin, tc.Commands[nextCommand]); err != nil {
						return err
					}
					waitingForCommand = true
					nextCommand++
				}
			}
			if nextCommand == len(tc.Commands) {
				if outputMatches(tc, output.String()) {
					armSettleTimer()
				} else {
					stopSettleTimer()
				}
			}
		}
	}
}

func sendCommand(stdin io.Writer, command string) error {
	_, err := fmt.Fprintln(stdin, command)
	return err
}

func waitingForPromptCompletion(text string) bool {
	return strings.HasSuffix(text, "$ ")
}

type safeBuffer struct {
	mu sync.Mutex
	b  bytes.Buffer
}

func (b *safeBuffer) Write(p []byte) (int, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.b.Write(p)
}

func (b *safeBuffer) WriteString(text string) {
	b.mu.Lock()
	defer b.mu.Unlock()
	_, _ = b.b.WriteString(text)
}

func (b *safeBuffer) String() string {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.b.String()
}

func (b *safeBuffer) Bytes() []byte {
	b.mu.Lock()
	defer b.mu.Unlock()
	return append([]byte(nil), b.b.Bytes()...)
}

func startBackground(commands [][]string) ([]*exec.Cmd, error) {
	var started []*exec.Cmd
	for _, argv := range commands {
		if len(argv) == 0 {
			continue
		}
		cmd := exec.Command(argv[0], argv[1:]...)
		cmd.SysProcAttr = processGroupAttr()

		stdout, err := cmd.StdoutPipe()
		if err != nil {
			stopBackground(started)
			return nil, err
		}
		stderr, err := cmd.StderrPipe()
		if err != nil {
			stopBackground(started)
			return nil, err
		}
		if err := cmd.Start(); err != nil {
			stopBackground(started)
			return nil, err
		}
		started = append(started, cmd)
		go copyOutput(io.Discard, stdout)
		go copyOutput(io.Discard, stderr)
	}
	if len(started) > 0 {
		time.Sleep(200 * time.Millisecond)
	}
	return started, nil
}

func stopBackground(commands []*exec.Cmd) {
	for _, cmd := range commands {
		killProcessGroup(cmd)
	}
	for _, cmd := range commands {
		_ = cmd.Wait()
	}
}

func copyOutput(output io.Writer, reader io.Reader) {
	buf := make([]byte, 4096)
	for {
		n, err := reader.Read(buf)
		if n > 0 {
			_, _ = output.Write(buf[:n])
		}
		if err != nil {
			return
		}
	}
}

func matchOutput(tc Case, text string) error {
	for _, pattern := range tc.Expect {
		matched, err := regexp.MatchString(pattern, text)
		if err != nil {
			return err
		}
		if !matched {
			return fmt.Errorf("missing expected pattern %q", pattern)
		}
	}
	for _, expectation := range tc.Count {
		count, err := countMatches(expectation.Pattern, text)
		if err != nil {
			return err
		}
		if expectation.Exact > 0 && count != expectation.Exact {
			return fmt.Errorf("pattern %q matched %d time(s), want exactly %d", expectation.Pattern, count, expectation.Exact)
		}
		if expectation.Min > 0 && count < expectation.Min {
			return fmt.Errorf("pattern %q matched %d time(s), want at least %d", expectation.Pattern, count, expectation.Min)
		}
	}
	for _, expectation := range tc.Distinct {
		count, err := countDistinctMatches(expectation.Pattern, text)
		if err != nil {
			return err
		}
		if expectation.Min > 0 && count < expectation.Min {
			return fmt.Errorf("pattern %q matched %d distinct value(s), want at least %d", expectation.Pattern, count, expectation.Min)
		}
	}
	for _, pattern := range tc.Reject {
		matched, err := regexp.MatchString(pattern, text)
		if err != nil {
			return err
		}
		if matched {
			return fmt.Errorf("rejected pattern matched %q", pattern)
		}
	}
	return nil
}

func outputMatches(tc Case, text string) bool {
	for _, pattern := range tc.Expect {
		matched, err := regexp.MatchString(pattern, text)
		if err != nil || !matched {
			return false
		}
	}
	for _, expectation := range tc.Count {
		count, err := countMatches(expectation.Pattern, text)
		if err != nil {
			return false
		}
		if expectation.Exact > 0 && count != expectation.Exact {
			return false
		}
		if expectation.Min > 0 && count < expectation.Min {
			return false
		}
	}
	for _, expectation := range tc.Distinct {
		count, err := countDistinctMatches(expectation.Pattern, text)
		if err != nil {
			return false
		}
		if expectation.Min > 0 && count < expectation.Min {
			return false
		}
	}
	for _, pattern := range tc.Reject {
		matched, err := regexp.MatchString(pattern, text)
		if err != nil || matched {
			return false
		}
	}
	return true
}

func countMatches(pattern string, text string) (int, error) {
	re, err := regexp.Compile(pattern)
	if err != nil {
		return 0, err
	}
	return len(re.FindAllStringIndex(text, -1)), nil
}

func countDistinctMatches(pattern string, text string) (int, error) {
	re, err := regexp.Compile(pattern)
	if err != nil {
		return 0, err
	}
	matches := re.FindAllString(text, -1)
	distinct := make(map[string]struct{}, len(matches))
	for _, match := range matches {
		distinct[match] = struct{}{}
	}
	return len(distinct), nil
}

func processGroupAttr() *syscall.SysProcAttr {
	if runtime.GOOS == "windows" {
		return nil
	}
	return &syscall.SysProcAttr{Setpgid: true}
}

func killProcessGroup(cmd *exec.Cmd) {
	if cmd.Process == nil {
		return
	}
	if runtime.GOOS == "windows" {
		_ = cmd.Process.Kill()
		return
	}
	_ = syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
}
