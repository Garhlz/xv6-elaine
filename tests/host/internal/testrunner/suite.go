package testrunner

import "time"

// Suite 是一组按子系统或用途归类的测试用例集合。
type Suite struct {
	Name  string
	Cases []Case
}

type QemuMode int

const (
	QemuModeNormal QemuMode = iota
	QemuModeNetForward
	QemuModeHostOnly
)

// Case 描述一个可独立执行的测试用例。
//
// 字段:
//
//	Commands    — 在 xv6 shell 中顺序执行的命令列表（QEMU 模式）
//	HostCommand — 宿主机直接执行的 argv（HostOnly 模式，如 []string{"build/notxv6/ph", "2"}）
//	Background  — 测试开始前必须先启动的后台进程（如 make server）
//	Expect      — 必须在输出中出现的 regex 模式列表
//	Count       — 输出中某 regex 必须出现的次数约束
//	Reject      — 不允许在输出中出现的 regex 模式列表
//	Tags        — 分类标签，如 "util", "syscall", "smoke", "heavy"
//	Heavy       — 是否属于重型测试（默认不跑）
//	Artifacts   — 失败时额外保存的文件路径（相对于仓库根目录）
type Case struct {
	Name        string
	Commands    []string
	HostCommand []string
	Background  [][]string
	Expect      []string
	Count       []CountExpectation
	Distinct    []DistinctExpectation
	Reject      []string
	Tags        []string
	Heavy       bool
	Artifacts   []string
	Timeout     time.Duration
	QemuMode    QemuMode
}

// CountExpectation 描述某 regex 模式在输出中应出现的次数。
// Exact > 0 时要求精确匹配；Min > 0 时要求至少出现 Min 次。
type CountExpectation struct {
	Pattern string
	Exact   int
	Min     int
}

// DistinctExpectation 描述某 regex 模式作为完整匹配项时至少应出现多少个不同值。
type DistinctExpectation struct {
	Pattern string
	Min     int
}

// BuiltinSuites 返回所有内置测试套件。
func BuiltinSuites() map[string]Suite {
	smoke := Suite{
		Name: "smoke",
		Cases: []Case{
			{
				Name:       "sleep-no-arguments",
				Commands:   []string{"sleep"},
				Expect:     []string{`(?m)^usage: sleep ticks$`},
				Reject:     commonRejects(),
				Tags:       []string{"util", "smoke"},
				Timeout:    30 * time.Second,
				QemuMode:   QemuModeNormal,
			},
			{
				Name:     "sleep-returns",
				Commands: []string{"sleep; pingpong"},
				Expect: []string{
					`(?m)^\d+: received ping$`,
					`(?m)^\d+: received pong$`,
				},
				Reject:     commonRejects(),
				Tags:       []string{"util", "smoke"},
				Timeout:    30 * time.Second,
				QemuMode:   QemuModeNormal,
			},
			{
				Name:     "pingpong",
				Commands: []string{"pingpong"},
				Expect: []string{
					`(?m)^\d+: received ping$`,
					`(?m)^\d+: received pong$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"util", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "primes",
				Commands: []string{"primes"},
				Expect: []string{
					`(?m)^prime 2$`, `(?m)^prime 3$`, `(?m)^prime 5$`,
					`(?m)^prime 7$`, `(?m)^prime 11$`, `(?m)^prime 13$`,
					`(?m)^prime 17$`, `(?m)^prime 19$`, `(?m)^prime 23$`,
					`(?m)^prime 29$`, `(?m)^prime 31$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"util", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name: "find-current-directory",
				Commands: []string{
					"cat README > go_find_cur",
					"find . go_find_cur",
				},
				Expect:   []string{pathPattern(`\./go_find_cur`)},
				Reject:   commonRejects(),
				Tags:     []string{"util", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name: "find-recursive",
				Commands: []string{
					"mkdir go_find_dir",
					"cat README > go_find_dir/go_find_rec",
					"mkdir go_find_dir/go_find_nest",
					"cat README > go_find_dir/go_find_nest/go_find_rec",
					"find . go_find_rec",
				},
				Expect: []string{
					pathPattern(`\./go_find_dir/go_find_rec`),
					pathPattern(`\./go_find_dir/go_find_nest/go_find_rec`),
				},
				Reject:   commonRejects(),
				Tags:     []string{"util", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name: "xargs",
				Commands: []string{
					"mkdir go_xargs_a",
					"mkdir go_xargs_c",
					"cat README > go_xargs_a/b",
					"cat README > go_xargs_c/b",
					"cat README > b",
					"find . b | xargs grep Version",
				},
				Count:    []CountExpectation{{Pattern: `(?m)^Version 6 \(v6\)\.  xv6 loosely follows the structure and style of v6,$`, Exact: 3}},
				Reject:   commonRejects(),
				Tags:     []string{"util", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "trace-32-grep",
				Commands: []string{"trace 32 grep hello README"},
				Expect: []string{
					`(?m)^\d+: syscall read -> 1023$`,
					`(?m)^\d+: syscall read -> 968$`,
					`(?m)^\d+: syscall read -> 235$`,
					`(?m)^\d+: syscall read -> 0$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"syscall", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "trace-all-grep",
				Commands: []string{"trace 2147483647 grep hello README"},
				Expect: []string{
					`(?m)^\d+: syscall trace -> 0$`,
					`(?m)^\d+: syscall exec -> 3$`,
					`(?m)^\d+: syscall open -> 3$`,
					`(?m)^\d+: syscall read -> 1023$`,
					`(?m)^\d+: syscall read -> 968$`,
					`(?m)^\d+: syscall read -> 235$`,
					`(?m)^\d+: syscall read -> 0$`,
					`(?m)^\d+: syscall close -> 0$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"syscall", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "trace-nothing",
				Commands: []string{"grep hello README"},
				Reject:   append(commonRejects(), `(?m)^.* syscall .*$`),
				Tags:     []string{"syscall", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "trace-children",
				Commands: []string{"trace 2 usertests forkforkfork"},
				Expect:   []string{`(?m)^ALL TESTS PASSED$`},
				Count:    []CountExpectation{{Pattern: `(?m)^\d+: syscall fork -> -?\d+$`, Min: 8}},
				Distinct: []DistinctExpectation{{Pattern: `(?m)^\d+: syscall fork -> -?\d+$`, Min: 2}},
				Reject:   commonRejects(),
				Tags:     []string{"syscall", "smoke"},
				Timeout:  60 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "sysinfotest",
				Commands: []string{"sysinfotest"},
				Expect:   []string{`(?m)^sysinfotest: OK$`},
				Reject:   append(commonRejects(), `(?m)^.* FAIL .*$`),
				Tags:     []string{"syscall", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "pgtbltest",
				Commands: []string{"pgtbltest"},
				Expect: []string{
					`(?m)^ugetpid_test: OK$`,
					`(?m)^pgaccess_test: OK$`,
					`(?m)^pgtbltest: all tests succeeded$`,
				},
				Reject:   append(commonRejects(), `(?m)^pgtbltest: .* failed:`),
				Tags:     []string{"pgtbl", "smoke"},
				Timeout:  300 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "pte-printout",
				Commands: []string{"ls"},
				Expect: []string{
					`(?m)^page table 0x000000008[0-9a-f]+$`,
					`(?m)^\.\.0: pte 0x[0-9a-f]+ pa 0x000000008[0-9a-f]+$`,
					`(?m)^\.\. \.\. \.\.0: pte 0x[0-9a-f]+ pa 0x000000008[0-9a-f]+$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"pgtbl", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "bttest",
				Commands: []string{"bttest"},
				Reject:   commonRejects(),
				Tags:     []string{"traps", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "alarmtest",
				Commands: []string{"alarmtest"},
				Expect: []string{
					`(?m)^test0 passed$`,
					`(?m)^\.*test1 passed$`,
					`(?m)^\.*test2 passed$`,
				},
				Reject:   append(commonRejects(), `(?m)^.* failed.*$`),
				Tags:     []string{"traps", "smoke"},
				Timeout:  60 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "nettests-local",
				Commands: []string{"nettests local"},
				Background: [][]string{
					{"make", "server"},
				},
				Expect: []string{
					`(?m)^testing ping: OK$`,
					`(?m)^testing single-process pings: OK$`,
					`(?m)^testing multi-process pings: OK$`,
					`(?m)^all tests passed\.$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"net", "smoke"},
				Timeout:  60 * time.Second,
				QemuMode: QemuModeNetForward,
			},
			{
				Name:     "nettests-dns",
				Commands: []string{"nettests dns"},
				Expect: []string{
					`(?m)^testing DNS$`,
					`(?m)^DNS OK$`,
					`(?m)^all tests passed\.$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"net", "dns"},
				Timeout:  60 * time.Second,
				QemuMode: QemuModeNetForward,
			},
			{
				Name:     "uthread",
				Commands: []string{"uthread"},
				Expect: []string{
					`(?m)^thread_a started$`,
					`(?m)^thread_b started$`,
					`(?m)^thread_c started$`,
					`(?m)^thread_a: exit after 100$`,
					`(?m)^thread_b: exit after 100$`,
					`(?m)^thread_c: exit after 100$`,
					`(?m)^thread_schedule: no runnable threads$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"thread", "smoke"},
				Timeout:  60 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "cowtest",
				Commands: []string{"cowtest"},
				Expect:   []string{`(?m)^file: ok$`},
				Count: []CountExpectation{
					{Pattern: `(?m)^simple: ok$`, Exact: 2},
					{Pattern: `(?m)^three: ok$`, Exact: 3},
				},
				Reject:   commonRejects(),
				Tags:     []string{"cow", "smoke"},
				Timeout:  120 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "symlinktest",
				Commands: []string{"symlinktest"},
				Expect: []string{
					`(?m)^test symlinks: ok$`,
					`(?m)^test concurrent symlinks: ok$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"fs", "smoke"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "mmaptest",
				Commands: []string{"mmaptest"},
				Expect:   []string{`(?m)^mmaptest: all tests succeeded$`},
				Reject:   append(commonRejects(), `(?m)^mmaptest: .* failed`),
				Tags:     []string{"mmap", "smoke"},
				Timeout:  60 * time.Second,
				QemuMode: QemuModeNormal,
			},
		},
	}

	thread := Suite{
		Name: "thread",
		Cases: []Case{
			{
				Name:     "uthread",
				Commands: []string{"uthread"},
				Expect: []string{
					`(?m)^thread_a started$`,
					`(?m)^thread_b started$`,
					`(?m)^thread_c started$`,
					`(?m)^thread_a: exit after 100$`,
					`(?m)^thread_b: exit after 100$`,
					`(?m)^thread_c: exit after 100$`,
					`(?m)^thread_schedule: no runnable threads$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"thread"},
				Timeout:  60 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:        "ph-safety",
				HostCommand: []string{"build/notxv6/ph", "2"},
				Background:  [][]string{{"make", "ph"}},
				Count: []CountExpectation{
					{Pattern: `(?m)^\d+: 0 keys missing$`, Exact: 2},
				},
				Reject:   commonRejects(),
				Tags:     []string{"thread"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeHostOnly,
			},
			{
				Name:        "barrier",
				HostCommand: []string{"build/notxv6/barrier", "2"},
				Background:  [][]string{{"make", "barrier"}},
				Expect:      []string{`(?m)^OK; passed$`},
				Reject:      commonRejects(),
				Tags:        []string{"thread"},
				Timeout:     30 * time.Second,
				QemuMode:    QemuModeHostOnly,
			},
		},
	}

	cow := Suite{
		Name: "cow",
		Cases: []Case{
			{
				Name:     "cowtest",
				Commands: []string{"cowtest"},
				Expect:   []string{`(?m)^file: ok$`},
				Count: []CountExpectation{
					{Pattern: `(?m)^simple: ok$`, Exact: 2},
					{Pattern: `(?m)^three: ok$`, Exact: 3},
				},
				Reject:   commonRejects(),
				Tags:     []string{"cow"},
				Timeout:  120 * time.Second,
				QemuMode: QemuModeNormal,
			},
		},
	}

	traps := Suite{
		Name: "traps",
		Cases: []Case{
			{
				Name:     "bttest",
				Commands: []string{"bttest"},
				Reject:   commonRejects(),
				Tags:     []string{"traps"},
				Timeout:  30 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "alarmtest",
				Commands: []string{"alarmtest"},
				Expect: []string{
					`(?m)^test0 passed$`,
					`(?m)^\.*test1 passed$`,
					`(?m)^\.*test2 passed$`,
				},
				Reject:   append(commonRejects(), `(?m)^.* failed.*$`),
				Tags:     []string{"traps"},
				Timeout:  60 * time.Second,
				QemuMode: QemuModeNormal,
			},
		},
	}

	mmap := Suite{
		Name: "mmap",
		Cases: []Case{
			{
				Name:     "mmaptest",
				Commands: []string{"mmaptest"},
				Expect: []string{
					`(?m)^test mmap f: OK$`,
					`(?m)^test mmap private: OK$`,
					`(?m)^test mmap read-only: OK$`,
					`(?m)^test mmap read/write: OK$`,
					`(?m)^test mmap dirty: OK$`,
					`(?m)^test not-mapped unmap: OK$`,
					`(?m)^test mmap two files: OK$`,
					`(?m)^fork_test OK$`,
					`(?m)^shared_exit_writeback_test OK$`,
					`(?m)^read_into_fresh_mmap_test OK$`,
					`(?m)^mmaptest: all tests succeeded$`,
				},
				Reject:   append(commonRejects(), `(?m)^mmaptest: .* failed`),
				Tags:     []string{"mmap"},
				Timeout:  180 * time.Second,
				QemuMode: QemuModeNormal,
			},
		},
	}

	net := Suite{
		Name: "net",
		Cases: []Case{
			{
				Name:     "nettests-local",
				Commands: []string{"nettests local"},
				Background: [][]string{
					{"make", "server"},
				},
				Expect: []string{
					`(?m)^testing ping: OK$`,
					`(?m)^testing single-process pings: OK$`,
					`(?m)^testing multi-process pings: OK$`,
					`(?m)^all tests passed\.$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"net"},
				Timeout:  60 * time.Second,
				QemuMode: QemuModeNetForward,
			},
			{
				Name:     "nettests-dns",
				Commands: []string{"nettests dns"},
				Expect: []string{
					`(?m)^testing DNS$`,
					`(?m)^DNS OK$`,
					`(?m)^all tests passed\.$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"net", "dns"},
				Timeout:  60 * time.Second,
				QemuMode: QemuModeNetForward,
			},
		},
	}

	lock := Suite{
		Name: "lock",
		Cases: []Case{
			{
				Name:     "kalloctest",
				Commands: []string{"kalloctest"},
				Expect: []string{
					`(?m)^test1 OK$`,
					`(?m)^test2 OK$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"lock"},
				Timeout:  200 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "sbrkmuch",
				Commands: []string{"usertests sbrkmuch"},
				Expect:   []string{`(?m)^ALL TESTS PASSED$`},
				Reject:   commonRejects(),
				Tags:     []string{"lock"},
				Timeout:  90 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "bcachetest",
				Commands: []string{"bcachetest"},
				Expect: []string{
					`(?m)^test0: OK$`,
					`(?m)^test1 OK$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"lock"},
				Timeout:  90 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "usertests",
				Commands: []string{"usertests"},
				Expect:   []string{`(?m)^ALL TESTS PASSED$`},
				Reject:   commonRejects(),
				Tags:     []string{"lock", "heavy"},
				Timeout:  1500 * time.Second,
				QemuMode: QemuModeNormal,
			},
		},
	}

	fs := Suite{
		Name: "fs",
		Cases: []Case{
			{
				Name:     "bigfile",
				Commands: []string{"bigfile"},
				Expect: []string{
					`(?m)^wrote 65803 blocks$`,
					`(?m)^bigfile done; ok$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"fs", "heavy"},
				Timeout:  900 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "symlinktest",
				Commands: []string{"symlinktest"},
				Expect: []string{
					`(?m)^test symlinks: ok$`,
					`(?m)^test concurrent symlinks: ok$`,
				},
				Reject:   commonRejects(),
				Tags:     []string{"fs"},
				Timeout:  20 * time.Second,
				QemuMode: QemuModeNormal,
			},
			{
				Name:     "usertests",
				Commands: []string{"usertests"},
				Expect:   []string{`(?m)^ALL TESTS PASSED$`},
				Reject:   commonRejects(),
				Tags:     []string{"fs", "heavy"},
				Timeout:  1500 * time.Second,
				QemuMode: QemuModeNormal,
			},
		},
	}

	return map[string]Suite{
		smoke.Name:  smoke,
		thread.Name: thread,
		cow.Name:    cow,
		traps.Name:  traps,
		mmap.Name:   mmap,
		net.Name:    net,
		lock.Name:   lock,
		fs.Name:     fs,
	}
}

func commonRejects() []string {
	return []string{
		`(?m)^panic:`,
		`(?m)^exec .* failed`,
	}
}

func pathPattern(path string) string {
	return `(?m)^(?:\$ )*[ \t]*` + path + `$`
}
