package main

import (
	"flag"
	"fmt"
	"os"
	"strings"
	"time"

	"xv6-labs-2021/tests/host/internal/testrunner"
)

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}

	switch os.Args[1] {
	case "run":
		run(os.Args[2:])
	case "list":
		list(os.Args[2:])
	default:
		fmt.Fprintf(os.Stderr, "unknown command %q\n", os.Args[1])
		usage()
		os.Exit(2)
	}
}

func usage() {
	fmt.Fprintf(os.Stderr, `Usage:
  xv6test list [--suite NAME] [--tags TAG,...]
  xv6test run --suite NAME [--case NAME] [--tags TAG,...] [--log-dir DIR] [--timeout DURATION]

Examples:
  xv6test list --suite smoke
  xv6test list --suite smoke --tags util
  xv6test run --suite smoke --case mmaptest
  xv6test run --suite smoke --tags "syscall,traps"
`)
}

func list(args []string) {
	fs := flag.NewFlagSet("list", flag.ExitOnError)
	suiteName := fs.String("suite", "", "suite name")
	tagList := fs.String("tags", "", "comma-separated tags to filter")
	_ = fs.Parse(args)

	tags := parseTagList(*tagList)
	suites := testrunner.BuiltinSuites()
	if *suiteName != "" {
		suite, ok := suites[*suiteName]
		if !ok {
			fmt.Fprintf(os.Stderr, "unknown suite %q\n", *suiteName)
			os.Exit(2)
		}
		printSuite(suite, tags)
		return
	}

	for _, suite := range suites {
		printSuite(suite, tags)
	}
}

func printSuite(suite testrunner.Suite, tags []string) {
	fmt.Printf("%s\n", suite.Name)
	for _, tc := range suite.Cases {
		if !matchTags(tc.Tags, tags) {
			continue
		}
		fmt.Printf("  %s", tc.Name)
		if len(tc.Tags) > 0 {
			fmt.Printf("  [%s]", strings.Join(tc.Tags, ", "))
		}
		fmt.Println()
	}
}

func run(args []string) {
	fs := flag.NewFlagSet("run", flag.ExitOnError)
	suiteName := fs.String("suite", "smoke", "suite name")
	caseList := fs.String("case", "", "comma-separated case names")
	tagList := fs.String("tags", "", "comma-separated tags to filter")
	logDir := fs.String("log-dir", "build/test-logs", "test log directory")
	timeout := fs.Duration("timeout", 0, "override every selected case timeout")
	_ = fs.Parse(args)

	suites := testrunner.BuiltinSuites()
	suite, ok := suites[*suiteName]
	if !ok {
		fmt.Fprintf(os.Stderr, "unknown suite %q\n", *suiteName)
		os.Exit(2)
	}

	selected, err := selectCases(suite, *caseList, parseTagList(*tagList))
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	if *timeout > 0 {
		for i := range selected {
			selected[i].Timeout = *timeout
		}
	}

	runner := testrunner.Runner{
		LogDir: *logDir,
	}

	start := time.Now()
	var failures int
	for _, tc := range selected {
		result := runner.RunCase(suite.Name, tc)
		if result.Err != nil {
			failures++
			fmt.Printf("FAIL %s/%s (%s)\n", suite.Name, tc.Name, result.Duration.Round(time.Millisecond))
			fmt.Printf("  %v\n", result.Err)
			if result.LogPath != "" {
				fmt.Printf("  log: %s\n", result.LogPath)
			}
			continue
		}
		fmt.Printf("PASS %s/%s (%s)\n", suite.Name, tc.Name, result.Duration.Round(time.Millisecond))
	}

	fmt.Printf("suite %s: %d case(s), %d failure(s), %s\n", suite.Name, len(selected), failures, time.Since(start).Round(time.Millisecond))
	if failures > 0 {
		os.Exit(1)
	}
}

func selectCases(suite testrunner.Suite, caseList string, tags []string) ([]testrunner.Case, error) {
	byName := make(map[string]bool)
	explicitFilter := caseList != "" || len(tags) > 0
	if suite.Name == "smoke" && !explicitFilter {
		tags = []string{"smoke"}
		explicitFilter = true
	}
	if caseList != "" {
		for _, name := range strings.Split(caseList, ",") {
			name = strings.TrimSpace(name)
			if name != "" {
				byName[name] = false
			}
		}
	}

	var selected []testrunner.Case
	for _, tc := range suite.Cases {
		// 无显式过滤时排除 heavy 标签的 case
		if !explicitFilter && matchTags(tc.Tags, []string{"heavy"}) {
			continue
		}
		// tag 过滤: 指定 tags 时，case 必须至少有一个匹配
		if len(tags) > 0 && !matchTags(tc.Tags, tags) {
			continue
		}
		// case 名称过滤: 指定 --case 时只保留名称在列表中的
		if len(byName) > 0 {
			if _, ok := byName[tc.Name]; ok {
				selected = append(selected, tc)
				byName[tc.Name] = true
			}
			continue
		}
		selected = append(selected, tc)
	}

	for name, found := range byName {
		if !found {
			return nil, fmt.Errorf("unknown case %q in suite %q", name, suite.Name)
		}
	}
	return selected, nil
}

// parseTagList 将逗号分隔的标签字符串拆分为切片，去除空白。
func parseTagList(raw string) []string {
	if raw == "" {
		return nil
	}
	var tags []string
	for _, t := range strings.Split(raw, ",") {
		t = strings.TrimSpace(t)
		if t != "" {
			tags = append(tags, t)
		}
	}
	return tags
}

// matchTags 检查 tcTags 中是否至少包含一个 filterTags 中的标签。
// filterTags 为空时匹配所有（不过滤）。
func matchTags(tcTags, filterTags []string) bool {
	if len(filterTags) == 0 {
		return true
	}
	for _, want := range filterTags {
		for _, have := range tcTags {
			if have == want {
				return true
			}
		}
	}
	return false
}
